/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "tslist.h"
#include "super.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "page_cache.h"
#include "wander.h"

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
	atom->nr_flush_queues++;
}

static void
detach_fq(flush_queue_t * fq)
{
	assert("zam-731", spin_atom_is_locked(fq->atom));

	spin_lock_fq(fq);
	fq_list_remove_clean(fq);
	fq->atom->nr_flush_queues--;
	fq->atom = NULL;
	spin_unlock_fq(fq);
}

/* destroy flush queue object */
void
done_fq(flush_queue_t * fq)
{
	assert("zam-763", capture_list_empty(&fq->prepped));
	assert("zam-765", fq->nr_queued == 0);
	assert("zam-766", atomic_read(&fq->nr_submitted) == 0);

	reiser4_kfree(fq, sizeof *fq);
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
	assert("zam-907", fq_in_use(fq));

	if (JF_ISSET(node, JNODE_FLUSH_QUEUED))
		return;		/* queued already */

	JF_SET(node, JNODE_FLUSH_QUEUED);
	capture_list_remove_clean(node);
	capture_list_push_back(&fq->prepped, node);
	count_enqueued_node(fq);
}

/* repeatable process for waiting io completion on a flush queue object */
static int
wait_io(flush_queue_t * fq, int *nr_io_errors)
{
	assert("zam-738", fq->atom != NULL);
	assert("zam-739", spin_atom_is_locked(fq->atom));
	assert("zam-736", fq_in_use(fq));
	assert("zam-911", capture_list_empty(&fq->prepped));

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

	detach_fq(fq);
	done_fq(fq);

	atom_send_event(atom);

	return 0;
}

/* wait for all i/o for given atom to be completed, actually do one iteration
   on that and return -EAGAIN if there more iterations needed */
static int
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
			assert("vs-1247", fq->owner == 0);
			fq->owner = current;
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
	flush_queue_t *fq;

	assert("zam-720", spin_atom_is_locked(to));
	assert("zam-721", spin_atom_is_locked(from));


	for (fq = fq_list_front(&from->flush_queues);
	     !fq_list_end(&from->flush_queues, fq);
	     fq = fq_list_next(fq))
	{
		scan_fq_and_update_atom_ref(&fq->prepped, to);
		spin_lock_fq(fq);
		fq->atom = to;
		spin_unlock_fq(fq);
	}

	fq_list_splice(&to->flush_queues, &from->flush_queues);

	to->num_queued += from->num_queued;
}

#if REISER4_DEBUG
int atom_fq_parts_are_clean (txn_atom * atom)
{
	assert("zam-915", atom != NULL);
	return fq_list_empty(&atom->flush_queues);
}
#endif

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

/* Move all queued nodes out from @fq->prepped list. */
static void release_prepped_list(flush_queue_t * fq)
{
	txn_atom * atom;

	assert("zam-904", fq_in_use(fq));
 again:
	atom = UNDER_SPIN(fq, fq, atom_get_locked_by_fq(fq));

	while(!capture_list_empty(&fq->prepped)) {
		jnode * cur;

		cur = capture_list_front(&fq->prepped);
		capture_list_remove_clean(cur);

		count_dequeued_node(fq);
		LOCK_JNODE(cur);
		JF_CLR(cur, JNODE_FLUSH_QUEUED);

		if (JF_ISSET(cur, JNODE_HEARD_BANSHEE)) {
			JF_CLR(cur, JNODE_FLUSH_QUEUED);
			JF_CLR(cur, JNODE_OVRWR);
			JF_CLR(cur, JNODE_RELOC);
			JF_CLR(cur, JNODE_CREATED);

			cur->atom->capture_count--;
			cur->atom = NULL;

			UNLOCK_JNODE(cur);
			UNLOCK_ATOM(atom);
			jput (cur);

			goto again;
		}

		if (JF_ISSET(cur, JNODE_DIRTY))
			capture_list_push_back(&atom->dirty_nodes[jnode_get_level(cur)], cur);
		else
			capture_list_push_back(&atom->clean_nodes, cur);

		UNLOCK_JNODE(cur);
	}

	assert ("zam-908", capture_list_empty(&fq->prepped));
	assert ("zam-909", fq->nr_queued == 0);
	UNLOCK_ATOM(atom);
}

/* Submit write requests for nodes on the already filled flush queue @fq.

   @fq: flush queue object which contains jnodes we can (and will) write.
   @return: number of submitted blocks (>=0) if success, otherwise -- an error
            code (<0). */
int
write_fq(flush_queue_t * fq)
{
	int ret;

	ret = write_jnode_list(&fq->prepped, fq);
	if (ret)
		return ret;

	release_prepped_list(fq);
	return 0;
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
			assert("vs-1246", fq->owner == 0);
			fq->owner = current;
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
		assert("vs-1248", (*new_fq)->owner == 0);
		(*new_fq)->owner = current;
		attach_fq(atom, *new_fq);

		return 0;
	}

	UNLOCK_ATOM(atom);

	*new_fq = create_fq();

	if (*new_fq == NULL)
		return -ENOMEM;

	return -EAGAIN;
}

/* A wrapper around fq_by_atom for getting a flush queue object for current
 * atom, if success fq->atom remains locked. */
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
	return fq;
}

/* Releasing flush queue object after exclusive use */
void
fq_put_nolock(flush_queue_t * fq)
{
	assert("zam-747", fq->atom != NULL);
	assert("zam-902", capture_list_empty(&fq->prepped));
	assert("zam-910", fq->nr_queued == 0);
	mark_fq_ready(fq);
	assert("vs-1245", fq->owner == current);
	fq->owner = 0;
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
