/* Copyright 2002 by Hans Reiser */

#if ! defined (_FS_REISER4_IO_HANDLE_H_)
#define _FS_REISER4_IO_HANDLE_H_

struct reiser4_io_handle {
	struct semaphore io_sema;
	atomic_t         nr_submitted;
	atomic_t         nr_errors;
};

extern void init_io_handle (struct reiser4_io_handle *);
extern int  done_io_handle (struct reiser4_io_handle *);
extern void io_handle_add_bio (struct reiser4_io_handle*, struct bio*);
extern void io_handle_end_io (struct bio*);

#endif /* _FS_REISER4_IO_HANDLE_H_ */


