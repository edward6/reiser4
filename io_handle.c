/* Copyright 2002 by Hans Reiser */

/*
 * Reiser4 i/o handles are i/o synchronization objects.
 *
 * An i/o handle has a counter for submitted bio objects and a semaphore. The
 * counter gets incremented when each new i/o request is submitted and gets
 * decremented on each i/o completion. The last i/o request completion ups the
 * i/o handle semaphore.
 *
 * The thread which waits for completion of all i/o requests submitted for
 * given atom just sleeps on a semaphore instead of traversing a list of
 * jnodes and waiting on each jnode's page for i/o completion.  
 *
 * It is less efficient to wait on each page on a list rather than to wait on
 * one i/o handle semaphore because of unnecessary context switches which
 * could happen at each wait_on_page*() call.
 */

#include "debug.h"
#include "tslist.h"
#include "txnmgr.h"

#include <asm/atomic.h>
#include <linux/bio.h>

TS_LIST_DEFINE (io_handles, struct reiser4_io_handle, linkage);

static void init_io_handle (struct reiser4_io_handle * io)
{
	sema_init(&io->io_sema, 0);

	/* This setting makes the wakeup code in wander_and_io working only
	 * after io->nr_submitted gets decremented in done_io_handle. */
	atomic_set(&io->nr_submitted, 1);

	atomic_set(&io->nr_errors, 0);
}

/* wait for completion of all i/o requests counted in the i/o handle */
static int io_handle_wait_io (struct reiser4_io_handle * io)
{
	assert ("zam-700", no_counters_are_held ());

	/* this 1 was from init_io_handle() */
	if (! atomic_dec_and_test(&io->nr_submitted)) {
		/* sort and pass requests to driver */
		blk_run_queues();
		/* wait all IO to complete */
		down (&io->io_sema);

		assert ("zam-577", atomic_read(&io->nr_submitted) == 0);
	}

	return  atomic_read(&io->nr_errors);
}

/* count one bio in i/o handle */
static void io_handle_add_bio (struct reiser4_io_handle * io, struct bio * bio)
{
	bio->bi_private = io;

	if (io) atomic_add(bio->bi_vcnt, &io->nr_submitted);
}

/* This routine should be called from i/o completion handler to proper
 * counting of completed i/o requests. */
void io_handle_end_io (struct bio * bio)
{
	struct reiser4_io_handle * io = bio->bi_private;

	if (!io) return;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags)) 
		atomic_inc(&io->nr_errors);

	if (atomic_sub_and_test(bio->bi_vcnt, &io->nr_submitted))
		up (&io->io_sema);
}

/* Take one available i/o handle from @atom's list and count one @bio there.
 * If @atom has no i/o handles, drop atom lock and allocate one */
int atom_add_bio (txn_atom * atom, struct bio * bio, struct reiser4_io_handle ** iop)
{
	struct reiser4_io_handle * hio;

	assert ("zam-693", spin_atom_is_locked (atom));

	if (! io_handles_list_empty (&atom->io_handles)) {
		hio = io_handles_list_front (&atom->io_handles);
	} else {
		if (! *iop) { 
			spin_unlock_atom (atom);
			*iop = reiser4_kmalloc (sizeof (struct reiser4_io_handle), GFP_KERNEL);
			if (*iop == NULL)
				return -ENOMEM;
			return -EAGAIN;
		}
		hio = *iop;
		*iop = NULL;
		init_io_handle (hio);
		io_handles_list_push_front (&atom->io_handles, hio);
	}

	io_handle_add_bio (hio, bio);

	if (*iop) {
		reiser4_kfree (*iop, sizeof (struct reiser4_io_handle));
		*iop = NULL;
	}

	return 0;
}


/* Adding bio to current atom (should be unlocked) */
int current_atom_add_bio (struct bio * bio)
{
	struct reiser4_io_handle * hio = NULL;
	txn_atom * atom;
	int ret;

 repeat: 
	atom = get_current_atom_locked ();

	ret = atom_add_bio (atom, bio, &hio);

	if (ret == -EAGAIN)
		goto repeat;

	if (ret)
		return ret;

	spin_unlock_atom (atom);

	return ret;
}

/* fuse i/o handles lists at atom fusion time */
void atom_fuse_io (txn_atom * to, txn_atom * from)
{
	assert ("zam-695", spin_atom_is_locked (to));
	assert ("zam-696", spin_atom_is_locked (from));

	io_handles_list_splice (&to->io_handles, &from->io_handles);
}

/* wait for all i/o completion, invalidate all i/o handles, @io_error counts
 * i/o errors */
int atom_wait_on_io (txn_atom * atom, int * io_error)
{
	struct reiser4_io_handle * io;

	assert ("zam-694", spin_atom_is_locked (atom));

	if (io_handles_list_empty (&atom->io_handles))
		return 0;

	io = io_handles_list_pop_front (&atom->io_handles);
	spin_unlock_atom (atom);
	*io_error += io_handle_wait_io (io);
	reiser4_kfree (io, sizeof (struct reiser4_io_handle));

	return -EAGAIN;
}

/* wait for completion of all i/o requests submitted for current atom */
int current_atom_wait_on_io (void)
{
	int error = 0;
	int ret;
	txn_atom * atom;

 repeat:
	atom = get_current_atom_locked ();
	ret = atom_wait_on_io (atom, &error);

	if (ret == -EAGAIN)
		goto repeat;

	if (ret) 
		return ret;

	spin_unlock_atom (atom);

	if (error)
		return -EIO;

	return 0;
}

/* A part of atom initialization */
void atom_init_io (txn_atom * atom)
{
	io_handles_list_init (&atom->io_handles);
}

/* A part of atom destroying */
void atom_done_io (txn_atom * atom) 
{
	assert ("zam-698", spin_atom_is_locked (atom));

	/* it is possible to have not active i/o handles at this time because
	 * jwait_io() was used to wait on i/o which does not invalidate i/o
	 * handles list */

	while (! io_handles_list_empty (&atom->io_handles)) {
		struct reiser4_io_handle * io;

		io = io_handles_list_pop_front (&atom->io_handles);
		assert ("zam-699", atomic_read (&io->nr_submitted) == 1);
		reiser4_kfree (io, sizeof (struct reiser4_io_handle));
	}
}

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * End:
 */

