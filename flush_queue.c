/*
 * Copyright 2002 Hans Reiser
 */

#include "debug.h"
#include "tslist.h"
#include "super.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"

#include <linux/bio.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>

/* A flush queue object is an accumulator for keeping jnodes prepared
 * by the jnode_flush() function for writing to disk. Those "queued" jnodes are
 * kept on the flush queue until memory pressure or atom commit asks
 * flush queues to write some or all from their jnodes. */

TS_LIST_DEFINE (fq, flush_queue_t, alink);
TS_LIST_DEFINE(atom,txn_atom,atom_link);

#if REISER4_DEBUG
#   define spin_ordering_pred_fq(fq)  (1)
#endif

SPIN_LOCK_FUNCTIONS(fq, flush_queue_t, guard);

/* The deadlock-safe order for flush queues and atoms is: first lock atom,
 * then lock flush queue object, then lock jnode  */

#define fq_in_use(fq)          ((fq)->state & FQ_IN_USE)
#define fq_ready(fq)           (!fq_in_use(fq))

#define mark_fq_in_use(fq)     do { (fq)->state |= FQ_IN_USE;    } while (0)
#define mark_fq_ready(fq)      do { (fq)->state &= ~FQ_IN_USE;   } while (0)

/* get lock on atom from locked flush queue object */
static txn_atom * atom_get_locked_by_fq (flush_queue_t * fq)
{
	txn_atom * atom;

	assert ("zam-729", spin_fq_is_locked (fq));

	for (;;) {
		atom = fq->atom;

		if (atom == NULL)
			break;

		if (spin_trylock_atom (atom))
			break;

		spin_unlock_fq (fq);
		spin_lock_fq (fq);
	}

	return atom;
}

static void init_fq (flush_queue_t * fq)
{
	xmemset (fq, 0, sizeof *fq);

	atomic_set (&fq->nr_submitted, 0);

	capture_list_init (&fq->prepped);
	capture_list_init (&fq->sent);

	sema_init (&fq->sema, 0);
}

/* create new flush queue object */
static flush_queue_t * fq_create (void)
{
	flush_queue_t * fq;

	fq = reiser4_kmalloc (sizeof (*fq), GFP_KERNEL);

	if (fq)
		init_fq (fq);

	return fq;
}

/* adjust atom's and flush queue's counters of queued nodes */
static void count_enqueued_node (flush_queue_t * fq)
{
	fq->nr_queued ++;
	fq->atom->num_queued ++;
}

static void count_dequeued_node (flush_queue_t *fq)
{
	fq->nr_queued --;
	fq->atom->num_queued --;
}


/* attach flush queue object to the atom */
static void fq_attach (txn_atom * atom, flush_queue_t * fq)
{
	assert ("zam-718", spin_atom_is_locked (atom));
	fq_list_push_front (&atom->flush_queues, fq);
	fq->atom = atom;
}

static void fq_detach_nolock (flush_queue_t * fq)
{
	assert ("zam-732", spin_fq_is_locked (fq));
	assert ("zam-731", spin_atom_is_locked (fq->atom));
	assert ("zam-733", fq_ready(fq));

	fq_list_remove_clean (fq);
	fq->atom = NULL;
}

#if 0
/* detach fq from atom */
static void fq_detach (flush_queue_t * fq)
{
	txn_atom * atom;
	spin_lock_fq (fq);

	atom = atom_get_locked_by_fq (fq);

	if (atom != NULL) {
		fq_detach_nolock (fq);
		spin_unlock_atom (atom);
	}
		
	spin_unlock_fq (fq);
}
#endif

/* destroy flush queue object */
void fq_done (flush_queue_t * fq)
{
	reiser4_kfree (fq, sizeof *fq);
}


/* remove queued node from transaction */
static void uncapture_queued_node (flush_queue_t *fq, jnode *node)
{
	assert ("zam-737", spin_jnode_is_locked (node));
	assert ("zam-738", spin_fq_is_locked (fq));
	assert ("zam-739", fq->atom);
	assert ("zam-740", spin_atom_is_locked (fq->atom));

	trace_on (TRACE_FLUSH, "queued jnode removed from memory\n");

	capture_list_remove_clean (node);
	count_dequeued_node (fq);

	JF_CLR (node, JNODE_FLUSH_QUEUED);
	JF_CLR (node, JNODE_OVRWR);
	JF_CLR (node, JNODE_RELOC);
	JF_CLR (node, JNODE_CREATED);

	node->atom->capture_count --;
	node->atom = NULL;

	spin_unlock_jnode (node);

	jput (node);

	/* atom and fq are still locked at this moment */
}

/* Check if this node must be removed from the transaction, if so, remove
 * it. Return value: 1 if node has been removed from the transaction, 0
 * otherwise. */
static inline int try_uncapture_node (flush_queue_t * fq, jnode * node)
{
	if (JF_ISSET (node, JNODE_HEARD_BANSHEE)) {
		uncapture_queued_node (fq, node);
		return 1;
	}

	return 0;
}

/**
 * Putting jnode into the flush queue. Both atom and jnode should be
 * spin-locked. */
void fq_queue_node (flush_queue_t * fq, jnode * node)
{
	assert ("zam-711", spin_jnode_is_locked (node));
	assert ("zam-713", node->atom != NULL);
	assert ("zam-712", spin_atom_is_locked (node->atom));
	assert ("zam-714", jnode_is_dirty (node));
	assert ("zam-716", fq->atom != NULL);
	assert ("zam-717", fq->atom == node->atom);

	if (JF_ISSET (node, JNODE_FLUSH_QUEUED))
		return;	/* queued already */

	capture_list_remove_clean (node);

	JF_SET (node, JNODE_FLUSH_QUEUED);

	/* Some tricks with lock ordering. */
	spin_unlock_jnode (node);

	spin_lock_fq (fq);
	spin_lock_jnode (node);

	if (JF_ISSET(node, JNODE_WRITEBACK)) {
		capture_list_push_back (&fq->sent, node);
		atomic_inc (&fq->nr_submitted);
	} else {
		capture_list_push_back (&fq->prepped, node);
	}

	count_enqueued_node (fq);
	spin_unlock_fq (fq);
}

/* remove jnode from the flush queue; return 0 if node has been uncaptured;
 * always unlocks jnode */
static int fq_dequeue_node (flush_queue_t *fq, jnode * node)
{
	assert ("zam-724", spin_jnode_is_locked (node));
	assert ("zam-725", fq->atom != NULL);
	assert ("zam-726", spin_atom_is_locked (fq->atom));
	assert ("zam-741", !jnode_is_dirty(node));

	if (try_uncapture_node (fq, node))
		return 0;

	count_dequeued_node (fq);

	JF_CLR (node, JNODE_FLUSH_QUEUED);
	JF_CLR (node, JNODE_DIRTY);

	capture_list_remove (node);
	capture_list_push_back (&fq->atom->clean_nodes, node);

	spin_unlock_jnode (node);

	return 1;
}

/* repeatable process for waiting io completion on a flush queue object */
static int fq_wait_io (flush_queue_t * fq, int * nr_io_errors)
{
	assert ("zam-737", spin_fq_is_locked (fq));
	assert ("zam-738", fq->atom != NULL);
	assert ("zam-739", spin_atom_is_locked (fq->atom));
	assert ("zam-736", fq_ready(fq));

	if (atomic_read (&fq->nr_submitted) != 0) {
		spin_unlock_fq (fq);
		spin_unlock_atom (fq->atom);

		assert ("zam-734", lock_counters () -> spin_locked == 0);

		blk_run_queues();
		down (&fq->sema);

		/* Ask the caller to re-aquire the locks and call this
		 * function again. Note: this technique is commonly used in
		 * the txnmgr code. */
		return -EAGAIN;
	}

	* nr_io_errors += atomic_read (&fq->nr_errors);
	return 0;
}

/* scan fq's io list and dispatch nodes */
static void fq_scan_io_list (flush_queue_t * fq)
{
	jnode * cur;
	txn_atom * atom;

	assert ("zam-740", spin_fq_is_locked (fq));
	atom = fq->atom;
	assert ("zam-741", atom != NULL);
	assert ("zam-742", spin_atom_is_locked (atom));

	cur = capture_list_front (&fq->sent);

	while (! capture_list_end (&fq->sent, cur)) {
		jnode * next = capture_list_next (cur);

		spin_lock_jnode (cur);

		assert ("zam-735", !JF_ISSET(cur, JNODE_WRITEBACK));

		if (JF_ISSET (cur, JNODE_DIRTY)) {
			capture_list_remove (cur);
			capture_list_push_back (&fq->prepped, cur);

			spin_unlock_jnode (cur);
		} else {
			fq_dequeue_node (fq, cur);
		}

		cur = next;
	}
}

/* wait on I/O completion, re-submit dirty nodes to write */
static int fq_finish (flush_queue_t * fq, int * nr_io_errors)
{
	int ret;

	assert ("zam-743", spin_fq_is_locked (fq));
	assert ("zam-744", spin_atom_is_locked (fq->atom));

	ret = fq_wait_io (fq, nr_io_errors);
	if (ret)
		return ret;

	fq_scan_io_list (fq);

	/* check can we release this fq object */
	if (fq->nr_queued) {
		spin_unlock_fq (fq);
		spin_unlock_atom (fq->atom);
		
		/* re-submit nodes to write*/
		ret = fq_write (fq, 0);

		if (ret)
			return ret;

		return -EAGAIN;
	}

	fq_detach_nolock (fq);
	spin_unlock_fq(fq);
	fq_done (fq);

	return 0;
}

/* wait for all i/o for given atom to be completed, actually do one iteration
 * on that and return -EAGAIN if there more iterations needed */
int finish_all_fq (txn_atom * atom, int * nr_io_errors)
{
	flush_queue_t * fq;
	assert ("zam-730", spin_atom_is_locked (atom));

	if (fq_list_empty (&atom->flush_queues))
		return 0;

	for (fq = fq_list_front (&atom->flush_queues);
	     ! fq_list_empty (&atom->flush_queues);
	     fq = fq_list_next (fq))
	{
		spin_lock_fq (fq);

		if (fq_ready(fq)) {
			int ret;

			ret = fq_finish (fq, nr_io_errors);

			if (ret) return ret;

			spin_unlock_atom (atom);

			return -EAGAIN;
		}

		spin_unlock_fq (fq);
	}

	/* All flush queues are in use; atom remains locked */
	return -EBUSY;
}

/* wait all i/o for current atom */
int current_atom_finish_all_fq (void)
{
	txn_atom * atom;
	int nr_io_errors = 0;
	int ret;

	do {
		txn_atom * cur_atom;

		cur_atom = get_current_atom_locked ();
		ret = finish_all_fq (cur_atom, &nr_io_errors);

	} while (ret == -EAGAIN);

	if (ret) 
		return ret;

	spin_unlock_atom (atom);

	if (nr_io_errors) 
		return -EIO;

	return 0;
}


/* change node->atom field for all jnode from given list */
static void fq_queue_change_atom (capture_list_head * list, txn_atom * atom)
{
	jnode * cur;

	for (cur = capture_list_front (list);
	     ! capture_list_end (list, cur);
	     cur = capture_list_next (cur))
	{
		spin_lock_jnode (cur);
		cur->atom = atom; 
		spin_unlock_jnode (cur);
	}
} 

/* support for atom fusion operation */
void fq_fuse (txn_atom * to, txn_atom * from)
{
	assert ("zam-720", spin_atom_is_locked (to));
	assert ("zam-721", spin_atom_is_locked (from));

	{
		flush_queue_t * fq = fq_list_front (&from->flush_queues);

		while (!fq_list_end (&from->flush_queues, fq)) {
			spin_lock_fq (fq);

			fq_queue_change_atom (&fq->prepped, to);
			fq_queue_change_atom (&fq->sent, to);

			fq->atom = to;

			spin_unlock_fq (fq);

			fq = fq_list_next (fq);
		}
	}

	fq_list_splice (&to->flush_queues, &from->flush_queues);

	to->num_queued += from->num_queued;
}

/* bio i/o completion routine */
static int fq_end_io (struct bio * bio, unsigned int bytes_done UNUSED_ARG, 
		      int err UNUSED_ARG)
{
	int i;
	int nr_errors = 0;
	flush_queue_t * fq = bio->bi_private;

	if (bio->bi_size != 0)
		return 1;

	for (i = 0; i < bio->bi_vcnt; i += 1) {
		struct page *pg = bio->bi_io_vec[i].bv_page;

		if (! test_bit (BIO_UPTODATE, & bio->bi_flags)) {
			SetPageError (pg);
			nr_errors ++;
		}

		{
			jnode * node;

			assert ("zam-736", pg != NULL);
			assert ("zam-736", PagePrivate(pg));
			node = (jnode*)(pg->private);

			JF_CLR (node, JNODE_WRITEBACK);
		}

		end_page_writeback (pg);
		page_cache_release (pg);
	}

	if (fq) {
		atomic_add(nr_errors, &fq->nr_errors);

		if (atomic_sub_and_test(bio->bi_vcnt, &fq->nr_submitted))
			up (&fq->sema);
	}

	bio_put (bio);
	return 0;
}

/* Count I/O requests which will be submitted by @bio in given flush queues
 * @fq */
void fq_add_bio (flush_queue_t * fq, struct bio * bio)
{
	bio->bi_private = fq;

	if (fq) {
		atomic_add (bio->bi_vcnt, &fq->nr_submitted);
		bio->bi_end_io  = fq_end_io;
	}
}

/* submitting to write prepared list of jnodes */
static int fq_submit_write (flush_queue_t * fq, jnode * first, int nr)
{
	struct bio * bio;
	struct super_block * s = reiser4_get_current_sb();
	int nr_processed;

	assert ("zam-724", lock_counters()->spin_locked == 0);
	assert ("zam-725", nr != 0);

	if (! (bio = bio_alloc (GFP_KERNEL, nr)) ) 
		return -ENOMEM;

	bio->bi_sector  = *jnode_get_block (first) * (s->s_blocksize >>9);
	bio->bi_bdev    = s->s_bdev;
	bio->bi_vcnt    = nr;
	bio->bi_size    = s->s_blocksize * nr;

	for (nr_processed = 0;
	     nr_processed < nr;
	     nr_processed ++, first = capture_list_next (first))
	{
		struct page * pg;

		pg = jnode_page (first);
		/* FIXME-ZAM: What we should do to protect this page from
		 * being removed from memory? I think that reiser4
		 * releasepage() should look at JNODE_WRITEBACK bit */
		assert ("zam-727", pg != NULL);

		page_cache_get (pg);
		lock_page (pg);

		assert ("zam-728", !PageWriteback (pg));
		SetPageWriteback (pg);
		ClearPageDirty (pg);

		unlock_page (pg);

		jnode_ops (first)->io_hook (first, pg, WRITE);

		bio->bi_io_vec[nr_processed].bv_page   = pg;
		bio->bi_io_vec[nr_processed].bv_len    = s->s_blocksize;
		bio->bi_io_vec[nr_processed].bv_offset = 0;
	}

	fq_add_bio (fq, bio);
	submit_bio (WRITE, bio);

	return nr;
}

/**
 * 1. check whether this node should be written to disk or not; 
 * 2. change its state if yes, dequeue jnode if not; 
 * 3. inform the caller about the decision */
static int fq_prepare_node_for_write (flush_queue_t * fq, jnode * node)
{
	int ret = 0;
	txn_atom * atom;
				
	spin_lock_jnode (node);
	atom = atom_get_locked_by_jnode (node);
	assert ("zam-726", atom != NULL);

	if (!JF_ISSET (node, JNODE_DIRTY)) {
		/* dequeue it */
		fq_dequeue_node (fq, node);
		ret = 1;	/* this node should be skipped */
	} else {

		JF_SET (node, JNODE_WRITEBACK);
		JF_CLR (node, JNODE_DIRTY);

		capture_list_remove (node);
		capture_list_push_back (&fq->sent, node);

		spin_unlock_jnode (node);
	}

	spin_unlock_atom (atom);

	return ret;
}


/**
 * submit @how_many write requests for nodes on the already filled
 * flush queue @fq. There is a feature that any chunk of contiguous
 * blocks are written even if we must submit more requests than
 * @how_many.

 @fq       -- flush queue object which contains jnodes we can (and will) write.
 @how_many -- limit for number of blocks we should write, if 0 -- write all
              blocks.

 @return   -- 0 if success, otherwise -- error code.
*/
int fq_write (flush_queue_t * fq, int how_many)
{
	jnode * first;          /* should point to the first jnode we are going to submit
				 * in one bio */
	jnode * last;	/* should point to the jnode _after_ last to be submitted in that bio */
	int nr_submitted;	/* number of blocks we submit to write in this
				 * fq_write() call */
	int max_blocks;		/* a limit for maximum number of blocks in one bio implied by the
				 * device specific request queue restriction */

	if (capture_list_empty (&fq->prepped))
		return 0;

#if REISER4_USER_LEVEL_SIMULATION
	max_blocks = fq->nr_queued;
#else
	{
		struct super_block * s = reiser4_get_current_sb ();
		max_blocks = bdev_get_queue (s->s_bdev)->max_sectors >> (s->s_blocksize_bits - 9);
	}
#endif

	nr_submitted = 0;

	first = capture_list_front (&fq->prepped);
	last = first;

	/* repeat until either we empty the queue or we submit how_many were requested to be submitted. */
	do {
		int nr_contiguous = 0;
		int ret;
		/* take those nodes from the front of the prepped queue that are a contiguous
		 * sequence of block numbers, not greater than max_blocks (i/o subsystem
		 * limitation), and form a set from them defined by the range from the front of
		 * the queue to cur.  Pass that set to fq_prepare_node_for_write(). */
		for (;;) {
			jnode * cur = last;

			if (capture_list_end (&fq->prepped, cur))
				break;

			if (*jnode_get_block (cur) != *jnode_get_block (first) + nr_contiguous)
				break;

			last = capture_list_next (last);

			if (fq_prepare_node_for_write (fq, cur))
				break;

			if (++ nr_contiguous >= max_blocks)
				break;

		}
		/* take the set we just prepped, and submit it for writing to disk */
		if (nr_contiguous) {
			ret = fq_submit_write (fq, first, nr_contiguous);

			if (ret < 0)
				return ret;

			nr_submitted += ret;
		}

		if (how_many && nr_submitted >= how_many)
			break;

		first = last;
	} while (!capture_list_end (&fq->prepped, last));

	return 0;
}

/* Getting flush queue object for exclusive use by one thread. May require
 * several iterations which is indicated by -EAGAIN return code. */
int fq_get (txn_atom * atom, flush_queue_t ** new_fq)
{
	flush_queue_t * fq;

	assert ("zam-745", spin_atom_is_locked (atom));

	fq = fq_list_front (&atom->flush_queues);
	while (! fq_list_end (&atom->flush_queues, fq)) {
		spin_lock_fq (fq);

		if (fq_ready(fq)) {
			mark_fq_in_use (fq);
			spin_unlock_fq (fq);
			
			if (*new_fq) {
				fq_done (*new_fq);
			}

			*new_fq = fq;

			return 0;
		}

		spin_unlock_fq (fq);

		fq = fq_list_next (fq);
	}

	/* Use previously allocated fq object */
	if (*new_fq) {
		mark_fq_in_use (*new_fq);
		fq_attach (atom, *new_fq);

		return 0;
	}

	spin_unlock_atom (atom);

	*new_fq = fq_create ();

	if (*new_fq == NULL)
		return -ENOMEM;

	return -EAGAIN;
}

/* A wrapper around fq_get for getting a flush queue object for current atom */
flush_queue_t * get_fq_for_current_atom (void)
{
	flush_queue_t * fq = NULL;
	txn_atom * atom;
	int ret;

	do {
		atom = get_current_atom_locked ();
		ret = fq_get (atom, &fq);
	} while (ret == -EAGAIN);
	
	if (ret)
		return ERR_PTR (ret);

	return fq;
}

/* Releasing flush queue object after exclusive use */
static void fq_put_nolock (flush_queue_t * fq)
{
	assert ("zam-747", fq->atom != NULL);
	mark_fq_ready(fq);
}

void fq_put (flush_queue_t * fq)
{
	txn_atom * atom;

	spin_lock_fq (fq);
	atom = atom_get_locked_by_fq (fq);

	assert ("zam-746", atom != NULL);

	fq_put_nolock (fq);
	atom_send_event (atom);

	spin_unlock_fq (fq);
	spin_unlock_atom (atom);
}

/* A part of atom object initialization related to the embedded flush queue
 * list head */
void fq_init_atom (txn_atom * atom)
{
	fq_list_init (&atom->flush_queues);
}

#define atom_has_queues_to_write(atom) \
((atom)->stage < ASTAGE_PRE_COMMIT && !fq_list_empty(&(atom)->flush_queues))  

static int get_enough_fq (txn_atom * atom, flush_queue_t ** result_list, int how_many)
{
	int total_est = 0;

	if (atom_has_queues_to_write (atom)) {
		flush_queue_t * fq;
		for (fq = fq_list_front (&atom->flush_queues);
		     ! fq_list_end (&atom->flush_queues, fq) && (total_est <= how_many);
		     fq = fq_list_next (fq))
		{
			int est;

			spin_lock_fq (fq);

			est = fq->nr_queued - atomic_read (&fq->nr_submitted);

			if (fq_ready (fq) && est > 0) {
				fq->next_to_write = *result_list;
				*result_list = fq;
				mark_fq_in_use (fq);

				total_est += est;
			}

			spin_unlock_fq (fq);
		}
	}

	return total_est;
}

/* Submit nodes to write from single-linked list of flush queues until number
 * of submitted nodes is equal to or greater than @how_many nodes requested
 * for submission */
static int write_list_fq (flush_queue_t * first, int how_many)
{
	int nr_submitted = 0;

	while (first != NULL ) {
		int ret;

		flush_queue_t * cur = first;

		first = cur->next_to_write;
		cur->next_to_write = NULL;

		if (atomic_read (&cur->nr_submitted) == 0)
			fq_scan_io_list (cur);

		if (nr_submitted < how_many) {
			ret = fq_write (cur, how_many - nr_submitted);

			if (ret > 0)
				nr_submitted += ret;
		}

		fq_put (cur);
	}

	return nr_submitted;
}

/* A response to memory pressure */
int fq_writeback (struct super_block *s, jnode * node, int how_many)
{

	txn_atom * atom;
	int total_est = 0;
	flush_queue_t * list_fq_to_write = NULL;

	assert ("zam-747", how_many > 0);

	/* First, we try to get atom we have jnode from, and write dirty
	 * jnodes from atom's flush queues */
	spin_lock_jnode (node);
	atom = atom_get_locked_by_jnode (node);

	if (atom != NULL && atom_has_queues_to_write(atom)) {
		total_est = get_enough_fq (atom, &list_fq_to_write, how_many);
		spin_unlock_atom (atom);
	}

	spin_unlock_jnode (node);

	/* If scanning of one atom was not enough we scan all atoms for flush
	 * queues in proper state */
	if (how_many > total_est) {
		txn_mgr * mgr = &get_super_private(s)->tmgr;

		spin_lock_txnmgr (mgr);

		for (atom = atom_list_front (&mgr->atoms_list);
		     ! atom_list_end (&mgr->atoms_list, atom) && total_est < how_many;
		     atom = atom_list_next (atom))
		{
			spin_lock_atom (atom);
			total_est += get_enough_fq (atom, &list_fq_to_write, how_many);
			spin_unlock_atom (atom);
		}

		spin_unlock_txnmgr (mgr);
	}

	/* Write collected flush queues  */
	if (list_fq_to_write) {
		int nr_submitted;

		nr_submitted = write_list_fq (list_fq_to_write, how_many);

		if (nr_submitted < 0)
			return nr_submitted;

		if (nr_submitted >= how_many)
			return nr_submitted;

		how_many -= nr_submitted;
	}

	/* Could write nothing */
	return 0;
}
