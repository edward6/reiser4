/* Copyright 2000, 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Tracing facility. Copied from reiserfs v3.x patch, never released */

#if !defined( __REISER4_TRACE_H__ )
#define __REISER4_TRACE_H__

#include "forward.h"
#include "debug.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block, etc  */
#include <asm/semaphore.h>

typedef enum { log_to_file, log_to_console, log_to_bucket } trace_file_type;

#if REISER4_TRACE_TREE

typedef struct {
	trace_file_type type;
	struct file *fd;
	char *buf;
	size_t size;
	size_t used;
	struct semaphore held;
	int disabled;
} reiser4_trace_file;

typedef enum {
	tree_cut = 'c',
	tree_lookup = 'l',
	tree_insert = 'i',
	tree_paste = 'p'
} reiser4_traced_op;

extern int open_trace_file(struct super_block *super, const char *file_name, size_t size, reiser4_trace_file * trace);
extern int write_trace(reiser4_trace_file * file, const char *format, ...)
    __attribute__ ((format(printf, 2, 3)));

extern int write_trace_raw(reiser4_trace_file * file, const void *data, size_t len);
extern int hold_trace(reiser4_trace_file * file, int flag);
extern int disable_trace(reiser4_trace_file * file, int flag);
extern void close_trace_file(reiser4_trace_file * file);
extern int write_trace_stamp(reiser4_tree * tree, reiser4_traced_op op, ...);
extern int write_in_trace(const char *func, const char *mes);

#else

typedef struct {
} reiser4_trace_file;

#define open_trace_file( super, file_name, size, trace ) (0)
#define write_trace( file, format, ... ) (0)
#define write_trace_raw( file, data, len ) (0)
#define hold_trace( file, flag ) (0)
#define disable_trace( file, flag ) (0)
#define close_trace_file( file ) noop
#define write_trace_stamp( tree, op, ... ) (0)
#define write_in_trace(func, mes) (0)

#endif

#define WRITE_TRACE(...) ((void) write_trace_stamp(__VA_ARGS__))

/* __REISER4_TRACE_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
