/* Copyright 2002 by Hans Reiser */

#include "reiser4.h"

/* The routines to submit and wait for completion node write requests. */

void init_io_handle (struct reiser4_io_handle * io)
{
	sema_init(&io->io_sema, 0);

	/* This setting makes the wakeup code in wander_and_io working only
	 * after io->nr_submitted gets decremented in done_io_handle. */
	atomic_set(&io->nr_submitted, 1);

	atomic_set(&io->nr_errors, 0);
}

int done_io_handle (struct reiser4_io_handle * io)
{
	/* this 1 was from init_io_handle() */
	if (! atomic_dec_and_test(&io->nr_submitted)) {
		/* sort and pass requests to driver */
		blk_run_queues();
		/* wait all IO to complete */
		down (&io->io_sema);

		assert ("zam-577", atomic_read(&io->nr_submitted) == 0);

	}

	if (atomic_read(&io->nr_errors)) return -EIO;
	return 0;
}

void io_handle_add_bio (struct reiser4_io_handle * io, struct bio * bio)
{
	bio->bi_private = io;

	if (io) atomic_add(bio->bi_vcnt, &io->nr_submitted);
}

void io_handle_end_io (const struct bio * bio)
{
	struct reiser4_io_handle * io = bio->bi_private;

	if (!io) return;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags)) 
		atomic_inc(&io->nr_errors);

	if (atomic_sub_and_test(bio->bi_vcnt, &io->nr_submitted))
		up (&io->io_sema);
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

