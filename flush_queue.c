/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "tslist.h"
#include "super.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "page_cache.h"

#include <linux/bio.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>

/* A flush queue object is an accumulator for keeping jnodes prepared
   by the jnode_flush() function for writing to disk. Those "queued" jnodes are
   kept on the flush queue until memory pressure or atom commit asks
   flush queues to write some or all from their jnodes. */

TS_LIST_DEFINE(fq, flush_queue_t, alink);
TS_LIST_DEFINE(atom, txn_atom, atom_link);

#if REISER4_DEBUG
#   define spin_ordering_pred_fq(fq)  (1)
#endif

SPIN_LOCK_FUNCTIONS(fq, flush_queue_t, guard);

/* 
   LOCKING: 

   fq->guard spin lock protects fq->atom pointer and nothing else.  fq->prepped
   list, fq->nr_queued protected by atom spin lock.  For modification of
   fq->send both atom spin lock and "in-use" fq state are needed. To safely
   traverse fq->sent list only one locking (fq state or atom spin lock) is
   enough. atom->fq_list and fq->state protected by atom spin lock.

   The deadlock-safe order for flush queues and atoms is: first lock atom, then
   lock flush queue, then lock jnode.
*/

#define fq_in_use(fq)          ((fq)->state & FQ_IN_USE)
#define fq_ready(fq)           (!fq_in_use(fq))

#define mark_fq_in_use(fq)     do { (fq)->state |= FQ_IN_USE;    } while (0)
#define mark_fq_ready(fq)      do { (fq)->state &= ~FQ_IN_USE;   } while (0)

/* get lock on atom from locked flush queue object */
static txn_atom *
atom_get_locked_by_fq(flush_queue_t * fq)
{
	/* This code is similar to atom_locked_by_jnode(), look at it for the
	 * explanation. */
	txn_atom *atom;

	assert("zam-729", spin_fq_is_locked(fq));

	while(1) {
		atom = fq->atom;
		if (atom == NULL)
			break;

		if (spin_trylock_atom(atom))
			break;

		atomic_inc(&atom->refcount);
		spin_unlock_fq(fq);
		LOCK_ATOM(atom);
		spin_lock_fq(fq);

		if (fq->atom == atom) {
			atomic_dec(&atom->refcount);
			break;
		}

		spin_unlock_fq(fq);
		atom_dec_and_unlock(atom);
		spin_lock_fq(fq);
	}

	return atom;
}

static void
init_fq(flush_queue_t * fq)
{
	xmemset(fq, 0, sizeof *fq);

	atomic_set(&fq->nr_submitted, 0);

	capture_list_init(&fq->prepped);
	capture_list_init(&fq->sent);

	sema_init(&fq->sema, 0);
	spin_fq_init(fq);
}

/* create new flush queue object */
static flush_queue_t *
create_fq(void)
{
	flush_queue_t *fq;

	fq = reiser4_kmalloc(sizeof (*fq), GFP_KERNEL);

	if (fq)
		init_fq(fq);

	return fq;
}

/* adjust atom's and flush queue's counters of queued nodes */
static void
count_enqueued_node(flush_queue_t * fq)
{
	fq->nr_queued++;
	fq->atom->num_queued++;
}

static void
count_dequeued_node(flush_queue_t * fq)
{
	fq->nr_queued--;
	fq->atom->num_queued--;
}

/* attach flush queue object to the atom */
static void
attach_fq(txn_atom * atom, flush_queue_t * fq)
{
	assert("zam-718", spin_atom_is_locked(atom));
	fq_list_push_front(&atom->flush_queues, fq);
	fq->atom = atom;
}

static void
detach_fq(flush_queue_t * fq)
{
	assert("zam-731", spin_atom_is_locked(fq->atom));

	spin_lock_fq(fq);
	fq_list_remove_clean(fq);
	fq->atom = NULL;
	spin_unlock_fq(fq);
}

/* destroy flush queue object */
void
done_fq(flush_queue_t * fq)
{
	assert("zam-763", capture_list_empty(&fq->prepped));
	assert("zam-764", capture_list_empty(&fq->sent));
	assert("zam-765", fq->nr_queued == 0);
	assert("zam-766", atomic_read(&fq->nr_submitted) == 0);

	reiser4_kfree(fq, sizeof *fq);
}

/* remove queued node from transaction */
static void
uncapture_queued_node(flush_queue_t * fq, jnode * node)
{
	assert("zam-737", spin_jnode_is_locked(node));
	assert("zam-738", fq_in_use(fq));
	assert("zam-739", fq->atom);
	assert("zam-740", spin_atom_is_locked(fq->atom));

	trace_on(TRACE_FLUSH, "queued jnode removed from memory\n");

	capture_list_remove_clean(node);
	count_dequeued_node(fq);

	JF_CLR(node, JNODE_FLUSH_QUEUED);
	JF_CLR(node, JNODE_OVRWR);
	JF_CLR(node, JNODE_RELOC);
	JF_CLR(node, JNODE_CREATED);

	node->atom->capture_count--;
	node->atom = NULL;

	UNLOCK_JNODE(node);

	jput(node);
}

/* Check if this node must be removed from the transaction, if so, remove
   it. Return value: 1 if node has been removed from the transaction, 0
   otherwise. */
static inline int
try_uncapture_node(flush_queue_t * fq, jnode * node)
{
	assert ("zam-799", fq->atom != NULL);
	assert ("zam-800", spin_atom_is_locked(fq->atom));
	
	if (JF_ISSET(node, JNODE_HEARD_BANSHEE)) {
		uncapture_queued_node(fq, node);
		return 1;
	}

	return 0;
}

/* Putting jnode into the flush queue. Both atom and jnode should be
   spin-locked. */
void
queue_jnode(flush_queue_t * fq, jnode * node)
{
	assert("zam-711", spin_jnode_is_locked(node));
	assert("zam-713", node->atom != NULL);
	assert("zam-712", spin_atom_is_locked(node->atom));
	assert("zam-714", jnode_is_dirty(node));
	assert("zam-716", fq->atom != NULL);
	assert("zam-717", fq->atom == node->atom);
	assert("zam-826", JF_ISSET(node, JNODE_RELOC));

	if (JF_ISSET(node, JNODE_FLUSH_QUEUED))
		return;		/* queued already */

	capture_list_remove_clean(node);

	JF_SET(node, JNODE_FLUSH_QUEUED);

	if (JF_ISSET(node, JNODE_WRITEBACK)) {
		capture_list_push_back(&fq->sent, node);
		atomic_inc(&fq->nr_submitted);
	} else {
		capture_list_push_back(&fq->prepped, node);
	}

	count_enqueued_node(fq);
}

/* remove jnode from the flush queue; return 0 if node has been uncaptured;
   always unlocks jnode */
static int
dequeue_jnode(flush_queue_t * fq, jnode * node)
{
	assert("zam-724", spin_jnode_is_locked(node));
	assert("zam-725", fq->atom != NULL);
	assert("zam-726", spin_atom_is_locked(fq->atom));
	assert("zam-741", !jnode_is_dirty(node));
	assert("zam-754", fq_in_use(fq));

	if (try_uncapture_node(fq, node))
		return 0;

	count_dequeued_node(fq);

	JF_CLR(node, JNODE_FLUSH_QUEUED);
	JF_CLR(node, JNODE_DIRTY);

	capture_list_remove(node);
	capture_list_push_back(&fq->atom->clean_nodes, node);

	UNLOCK_JNODE(node);

	return 1;
}

/* repeatable process for waiting io completion on a flush queue object */
static int
wait_io(flush_queue_t * fq, int *nr_io_errors)
{
	assert("zam-738", fq->atom != NULL);
	assert("zam-739", spin_atom_is_locked(fq->atom));
	assert("zam-736", fq_in_use(fq));

	if (atomic_read(&fq->nr_submitted) != 0) {
		UNLOCK_ATOM(fq->atom);

		assert("nikita-3013", schedulable());

		blk_run_queues();
		down(&fq->sema);

		/* Ask the caller to re-aquire the locks and call this
		   function again. Note: this technique is commonly used in
		   the txnmgr code. */
		return -EAGAIN;
	}

	*nr_io_errors += atomic_read(&fq->nr_errors);
	return 0;
}

/* scan fq's io list and dispatch nodes */
static void
scan_fq_sent_list(flush_queue_t * fq)
{
	jnode *cur;
	txn_atom *atom;

	assert("nikita-2704", fq_in_use(fq));
	atom = fq->atom;
	assert("zam-741", atom != NULL);
	assert("zam-742", spin_atom_is_locked(atom));

	cur = capture_list_front(&fq->sent);

	while (!capture_list_end(&fq->sent, cur)) {
		jnode *next = capture_list_next(cur);

		LOCK_JNODE(cur);

		assert("zam-735", !JF_ISSET(cur, JNODE_WRITEBACK));

		if (JF_ISSET(cur, JNODE_DIRTY)) {
			capture_list_remove(cur);
			capture_list_push_back(&fq->prepped, cur);

			UNLOCK_JNODE(cur);
		} else {
			dequeue_jnode(fq, cur);
		}

		cur = next;
	}
}

/* wait on I/O completion, re-submit dirty nodes to write */
static int
finish_fq(flush_queue_t * fq, int *nr_io_errors)
{
	int ret;
	txn_atom * atom = fq->atom;

	assert("zam-801", atom != NULL);
	assert("zam-744", spin_atom_is_locked(atom));
	assert("zam-762", fq_in_use(fq));

	ret = wait_io(fq, nr_io_errors);
	if (ret)
		return ret;

	scan_fq_sent_list(fq);

	/* check can we release this fq object */
	if (fq->nr_queued) {
		UNLOCK_ATOM(atom);

		/* re-submit nodes to write */
		ret = write_fq(fq, 0);

		if (ret < 0)
			return ret;

		return -EAGAIN;
	}

	detach_fq(fq);
	done_fq(fq);

	atom_send_event(atom);

	return 0;
}

/* wait for all i/o for given atom to be completed, actually do one iteration
   on that and return -EAGAIN if there more iterations needed */
int
finish_all_fq(txn_atom * atom, int *nr_io_errors)
{
	flush_queue_t *fq;

	assert("zam-730", spin_atom_is_locked(atom));

	if (fq_list_empty(&atom->flush_queues))
		return 0;

	for (fq = fq_list_front(&atom->flush_queues);
	     !fq_list_end(&atom->flush_queues, fq);
	     fq = fq_list_next(fq))
	{
		if (fq_ready(fq)) {
			int ret;

			mark_fq_in_use(fq);
			ret = finish_fq(fq, nr_io_errors);

			if (ret) {
				fq_put(fq);
				return ret;
			}

			UNLOCK_ATOM(atom);

			return -EAGAIN;
		}
	}

	/* All flush queues are in use; atom remains locked */
	return -EBUSY;
}

/* wait all i/o for current atom */
int
current_atom_finish_all_fq(void)
{
	txn_atom *atom;
	int nr_io_errors = 0;
	int ret = 0;

	do {
		while (1) {
			atom = get_current_atom_locked();
			ret = finish_all_fq(atom, &nr_io_errors);
			if (ret != -EBUSY)
				break;
			atom_wait_event(atom);
		}
	} while (ret == -EAGAIN);

	/* we do not need locked atom after this function finishes, SUCCESS or
	   -EBUSY are two return codes when atom remains locked after
	   finish_all_fq */
	if (!ret)
		UNLOCK_ATOM(atom);

	assert("nikita-2696", spin_atom_is_not_locked(atom));

	if (ret)
		return ret;

	if (nr_io_errors)
		return -EIO;

	return 0;
}

/* change node->atom field for all jnode from given list */
static void
scan_fq_and_update_atom_ref(capture_list_head * list, txn_atom * atom)
{
	jnode *cur;

	for (cur = capture_list_front(list);
	     !capture_list_end(list, cur);
	     cur = capture_list_next(cur))
	{
		LOCK_JNODE(cur);
		cur->atom = atom;
		UNLOCK_JNODE(cur);
	}
}

/* support for atom fusion operation */
void
fuse_fq(txn_atom * to, txn_atom * from)
{
	assert("zam-720", spin_atom_is_locked(to));
	assert("zam-721", spin_atom_is_locked(from));

	{
		flush_queue_t *fq = fq_list_front(&from->flush_queues);

		while (!fq_list_end(&from->flush_queues, fq)) {
			scan_fq_and_update_atom_ref(&fq->prepped, to);

			spin_lock_fq(fq);

			scan_fq_and_update_atom_ref(&fq->sent, to);
			fq->atom = to;

			spin_unlock_fq(fq);

			fq = fq_list_next(fq);
		}
	}

	fq_list_splice(&to->flush_queues, &from->flush_queues);

	to->num_queued += from->num_queued;
}

/* bio i/o completion routine */
static int
end_io_handler(struct bio *bio, unsigned int bytes_done UNUSED_ARG, int err UNUSED_ARG)
{
	int i;
	int nr_errors = 0;
	flush_queue_t *fq = bio->bi_private;

	if (bio->bi_size != 0)
		return 1;

	for (i = 0; i < bio->bi_vcnt; i += 1) {
		struct page *pg = bio->bi_io_vec[i].bv_page;

		if (!test_bit(BIO_UPTODATE, &bio->bi_flags)) {
			SetPageError(pg);
			nr_errors++;
		}

		{
			jnode *node;

			assert("zam-736", pg != NULL);
			assert("zam-736", PagePrivate(pg));
			node = (jnode *) (pg->private);

			JF_CLR(node, JNODE_WRITEBACK);
		}

		end_page_writeback(pg);
		page_cache_release(pg);
	}

	if (fq) {
		atomic_add(nr_errors, &fq->nr_errors);

		if (atomic_sub_and_test(bio->bi_vcnt, &fq->nr_submitted))
			up(&fq->sema);
	}

	bio_put(bio);
	return 0;
}

/* Count I/O requests which will be submitted by @bio in given flush queues
   @fq */
void
add_fq_to_bio(flush_queue_t * fq, struct bio *bio)
{
	bio->bi_private = fq;
	bio->bi_end_io = end_io_handler;

	if (fq)
		atomic_add(bio->bi_vcnt, &fq->nr_submitted);
}

/* submitting to write prepared list of jnodes */
static int
submit_write(flush_queue_t * fq, jnode * first, int nr)
{
	struct bio *bio;
	struct super_block *s = reiser4_get_current_sb();
	int nr_processed;
	int doing_reclaim;

	assert("nikita-3014", schedulable());
	assert("zam-725", nr != 0);

	trace_on (TRACE_IO_W, "write of %d blocks starting from %llu\n", nr, 
		  (unsigned long long)(*jnode_get_block(first)));

	if (!(bio = bio_alloc(GFP_KERNEL, nr)))
		return -ENOMEM;

	bio->bi_sector = *jnode_get_block(first) * (s->s_blocksize >> 9);
	bio->bi_bdev = s->s_bdev;
	bio->bi_vcnt = nr;
	bio->bi_size = s->s_blocksize * nr;

	nr_processed = 0;
	doing_reclaim = current->flags & PF_KSWAPD;
	while (1) {
		int result;
		struct page *pg;

		assert("nikita-2776", JF_ISSET(first, JNODE_FLUSH_QUEUED));
		assert("zam-825", JF_ISSET(first, JNODE_RELOC));

		result = jprotect(first); /* un(-e-)flush it */
		if (result != 0)
			reiser4_panic("nikita-2775", 
				      "Failure to reload jnode: %i", result);
		pg = jnode_page(first);

#if REISER4_DEBUG
		if(++ first->written > 1) {
			__u32 count = 0;
			__u32 id    = 0;
			txn_atom * atom;

			LOCK_JNODE (first);
			atom = atom_locked_by_jnode(first);

			if (atom) {
				count = atom->capture_count;
				id    = atom->atom_id;
				UNLOCK_ATOM(atom);
			}

			UNLOCK_JNODE (first);
			
			trace_on(TRACE_LOG, "jnode [block = %llu] written %d times, atom (id = %u, count=%u)\n", 
				 (unsigned long long)first->blocknr, first->written, id, count);
		}
#endif

		/* This page is protected from washing from the page cache by
		   pages' jnode state bits: JNODE_OVERWRITE if jnode is in
		   overwrite set or JNODE_WRITEBACK if jnode is in relocate
		   set. */
		assert("zam-727", pg != NULL);

		page_cache_get(pg);

		lock_and_wait_page_writeback(pg);
		SetPageWriteback(pg);

		if (doing_reclaim)
			/* pages are submitted from kswapd as part of memory
			 * reclamation. Mark them as such. */
			SetPageReclaim(pg);

		write_lock(&pg->mapping->page_lock);
		/* clear dirty bit and update page cache statistics. */
		test_clear_page_dirty(pg);

		list_del(&pg->list);
		list_add(&pg->list, &pg->mapping->locked_pages);

		write_unlock(&pg->mapping->page_lock);

		reiser4_unlock_page(pg);

		jnode_io_hook(first, pg, WRITE);

		/* we do not need to protect this node from e-flush anymore  */
 		junprotect(first);

		bio->bi_io_vec[nr_processed].bv_page = pg;
		bio->bi_io_vec[nr_processed].bv_len = s->s_blocksize;
		bio->bi_io_vec[nr_processed].bv_offset = 0;

		if (++ nr_processed >= nr)
			break;

		first = capture_list_next(first);
	}

	add_fq_to_bio(fq, bio);
	reiser4_submit_bio(WRITE, bio);

	update_blocknr_hint_default (s, jnode_get_block (first));
 
	return nr;
}

/* 1. check whether this node should be written to disk or not; 
   2. change its state if yes, dequeue jnode if not; 
   3. inform the caller about the decision */
static int
prepare_node_for_write(flush_queue_t * fq, jnode * node)
{
	int ret = 0;
	txn_atom *atom;

	atom = fq->atom;

	assert("zam-726", atom != NULL);
	assert("zam-803", spin_atom_is_locked(atom));

	LOCK_JNODE(node);

	if (!JF_ISSET(node, JNODE_DIRTY)) {
		/* dequeue it */
		dequeue_jnode(fq, node);
		ret = 1;	/* this node should be skipped */
	} else {

		JF_SET(node, JNODE_WRITEBACK);
		JF_CLR(node, JNODE_DIRTY);

		capture_list_remove(node);
		capture_list_push_back(&fq->sent, node);

		UNLOCK_JNODE(node);
	}
	return ret;
}

/* submit @how_many write requests for nodes on the already filled
   flush queue @fq. There is a feature that any chunk of contiguous
   blocks are written even if we must submit more requests than
   @how_many.

   @fq       -- flush queue object which contains jnodes we can (and will) write.
   @how_many -- limit for number of blocks we should write, if 0 -- write all
                blocks.

   @return   -- number of submitted blocks (>=0) if success, otherwise -- error
                code (<0).
*/
int
write_fq(flush_queue_t * fq, int how_many)
{
	/* number of blocks we submit to write in this write_fq() call */
	int nr_submitted;
	/* a limit for maximum number of blocks in one bio implied by the device
	   specific request queue restriction */
	int max_blocks;	
	/* atom the fq is attached to */
	txn_atom * atom;

	assert("nikita-3015", schedulable());

#if REISER4_USER_LEVEL_SIMULATION
	max_blocks = fq->nr_queued;
#else
	{
		struct super_block *s = reiser4_get_current_sb();
		max_blocks = bdev_get_queue(s->s_bdev)->max_sectors >> (s->s_blocksize_bits - 9);
	}
#endif
	nr_submitted = 0;

	/* repeat until either we empty the queue or we submit how_many were requested to be submitted. */
	while (1) {
		/* take those nodes from the front of the prepped queue that are a contiguous
		   sequence of block numbers, not greater than max_blocks (i/o subsystem
		   limitation), and form a set from them defined by the range from the front of
		   the queue to cur.  Pass that set to prepare_node_for_write(). */
		int ret;

		jnode * first;
		jnode * last;

		int nr_contiguous;

		spin_lock_fq(fq);
		atom = atom_get_locked_by_fq(fq);
		spin_unlock_fq(fq);

		assert ("zam-802", atom != NULL);

		if (capture_list_empty(&fq->prepped)) {
			UNLOCK_ATOM(atom);
			break;
		}

		/* We save first node from sequence as an argument for
		   submit_write() */
		first = last = capture_list_front(&fq->prepped);
		nr_contiguous = 0;

		for (;;) {
			jnode *cur = last;

			if (capture_list_end(&fq->prepped, cur))
				break;

			if (*jnode_get_block(cur) != *jnode_get_block(first) + nr_contiguous)
				break;

			last = capture_list_next(last);

			if (prepare_node_for_write(fq, cur))
				break;

			if (++nr_contiguous >= max_blocks)
				break;
		}

		/* All nodes we going to write are moved to sent list, nobody
		   can steal them from there, we can unlock atom */
		UNLOCK_ATOM(atom);

		/* take the set we just prepped, and submit it for writing to disk */
		if (nr_contiguous) {
			ret = submit_write(fq, first, nr_contiguous);

			if (ret < 0)
				return ret;

			nr_submitted += ret;
		}

		if (how_many && nr_submitted >= how_many)
			break;
	} 

	trace_on (TRACE_IO_W, "write_fq submitted %d blocks\n", nr_submitted);

	return nr_submitted;
}

/* Getting flush queue object for exclusive use by one thread. May require
   several iterations which is indicated by -EAGAIN return code. */
int
fq_by_atom(txn_atom * atom, flush_queue_t ** new_fq)
{
	flush_queue_t *fq;

	assert("zam-745", spin_atom_is_locked(atom));

	fq = fq_list_front(&atom->flush_queues);
	while (!fq_list_end(&atom->flush_queues, fq)) {
		spin_lock_fq(fq);

		if (fq_ready(fq)) {
			mark_fq_in_use(fq);
			spin_unlock_fq(fq);

			if (*new_fq)
				done_fq(*new_fq);

			*new_fq = fq;

			return 0;
		}

		spin_unlock_fq(fq);

		fq = fq_list_next(fq);
	}

	/* Use previously allocated fq object */
	if (*new_fq) {
		mark_fq_in_use(*new_fq);
		attach_fq(atom, *new_fq);

		return 0;
	}

	UNLOCK_ATOM(atom);

	*new_fq = create_fq();

	if (*new_fq == NULL)
		return -ENOMEM;

	return -EAGAIN;
}

/* A wrapper around fq_by_atom for getting a flush queue object for current atom */
flush_queue_t *
get_fq_for_current_atom(void)
{
	flush_queue_t *fq = NULL;
	txn_atom *atom;
	int ret;

	do {
		atom = get_current_atom_locked();
		ret = fq_by_atom(atom, &fq);
	} while (ret == -EAGAIN);

	if (ret)
		return ERR_PTR(ret);

	UNLOCK_ATOM(atom);
	return fq;
}

/* Releasing flush queue object after exclusive use */
static void
fq_put_nolock(flush_queue_t * fq)
{
	assert("zam-747", fq->atom != NULL);
	mark_fq_ready(fq);
}

void
fq_put(flush_queue_t * fq)
{
	txn_atom *atom;

	spin_lock_fq(fq);
	atom = atom_get_locked_by_fq(fq);

	assert("zam-746", atom != NULL);

	fq_put_nolock(fq);
	atom_send_event(atom);

	spin_unlock_fq(fq);
	UNLOCK_ATOM(atom);
}

/* A part of atom object initialization related to the embedded flush queue
   list head */
void
init_atom_fq_parts(txn_atom * atom)
{
	fq_list_init(&atom->flush_queues);
}

/* perform a sent list scan before submitting queue to disk */
int
scan_and_write_fq(flush_queue_t * fq, int how_many)
{
	assert ("zam-798", fq_in_use(fq));

	if (atomic_read(&fq->nr_submitted) == 0) {
		txn_atom *atom;

		spin_lock_fq(fq);
		atom = atom_get_locked_by_fq(fq);
		spin_unlock_fq(fq);

		scan_fq_sent_list(fq);

		UNLOCK_ATOM(atom);
	}

	return write_fq(fq, how_many);
}

/* get a flush queue for an atom pointed by given jnode (spin-locked) ; returns
 * both atom and jnode locked and found and took exclusive access for flush
 * queue object.  */
int fq_by_jnode (jnode * node, flush_queue_t ** fq)
{
	txn_atom * atom;
	int ret;

	assert("zam-835", spin_jnode_is_locked(node));

	*fq = NULL;

	while (1) {
		/* begin with taking lock on atom */
		atom = atom_locked_by_jnode(node);
		UNLOCK_JNODE(node);

		if (atom == NULL) {
			/* jnode does not point to the atom anymore, it is
			 * possible because jnode lock could be removed for a
			 * time in atom_get_locked_by_jnode() */
			if (*fq) {
				done_fq(*fq);
				*fq = NULL;
			}
			return 0;
		}

		/* atom lock is required for taking flush queue */
		ret = fq_by_atom(atom, fq);

		if (ret) {
			if (ret == -EAGAIN)
				/* atom lock was released for doing memory
				 * allocation, start with locked jnode one more
				 * time */
				goto lock_again;
			return ret;
 		}

		/* It is correct to lock atom first, then lock a jnode */
		LOCK_JNODE(node);

		if (node->atom == atom)
			break;	/* Yes! it is our jnode. We got all of them:
				 * flush queue, and both locked atom and
				 * jnode */

		/* release all locks and allocated objects and restart from
		 * locked jnode. */
		UNLOCK_JNODE(node);

		fq_put(*fq);
		fq = NULL;

		UNLOCK_ATOM(atom);

	lock_again:
		LOCK_JNODE(node);
	}

	return 0;
}

/* Steal prepped nodes from another flush queue (from same @atom)
   and add them to @dst flush queue */
static void steal_queued_nodes (txn_atom * atom, flush_queue_t * dst)
{
	flush_queue_t * fq;

	assert ("zam-791", spin_atom_is_locked(atom));
	assert ("zam-792", spin_fq_is_not_locked(dst));

	for (fq = fq_list_front(&atom->flush_queues);
	     ! fq_list_end(&atom->flush_queues, fq);
	     fq = fq_list_next (fq))
	{
		if (dst != fq && fq->nr_queued > 0) {
			capture_list_head tmp;
			long nr = 0;

			capture_list_init(&tmp);

			while (!capture_list_empty(&fq->prepped)) {
				jnode * node;

				node = capture_list_front(&fq->prepped);
				capture_list_remove_clean(node);
				capture_list_push_back(&tmp, node);

				nr++;
			}

			fq->nr_queued -= nr;

			capture_list_splice(&dst->prepped, &tmp);
			dst->nr_queued += nr;

			return;
		}
	}
}

/* A response to memory pressure by submitting queued nodes to disk */
int writeback_queued_jnodes(struct super_block *s, jnode * node)
{
	flush_queue_t * fq;
	txn_atom * atom;
	int ret;

	assert ("zam-790", node != NULL);

	LOCK_JNODE(node);
	ret = fq_by_jnode(node, &fq);

	if (ret || !fq)
		return ret;

	atom = node->atom;

	UNLOCK_JNODE(node);

	assert ("zam-789", spin_atom_is_locked (atom));

	if (atomic_read(&fq->nr_submitted) == 0)
		scan_fq_sent_list(fq);
	else {
		int errors = 0;
		/* wait for i/o completion */
		ret = wait_io(fq, &errors);
		if (ret) {
			if(ret == -EAGAIN)
				ret = 0;
		} else {
			UNLOCK_ATOM(atom);
		}

		fq_put(fq);
		return ret;
	}

	if (fq->nr_queued == 0)
		steal_queued_nodes (atom, fq);

	UNLOCK_ATOM(atom);

	ret = write_fq(fq, 0);
	fq_put(fq);

	return ret;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   scroll-step: 1
   End:
*/
