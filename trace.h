/* Copyright 2000, 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Tree-tracing facility. Copied from reiserfs v3.x patch, never released. See
 * trace.c for comments. */

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
	spinlock_t lock;
	struct list_head wait;
	int disabled;
	int long_term;
} reiser4_trace_file;

typedef enum {
	tree_cut = 'c',
	tree_lookup = 'l',
	tree_insert = 'i',
	tree_paste = 'p',
	tree_cached = 'C',
	tree_exit = 'e'
} reiser4_traced_op;

extern int open_trace_file(struct super_block *super, const char *file_name, size_t size, reiser4_trace_file * trace);
extern int write_trace(reiser4_trace_file * file, const char *format, ...)
    __attribute__ ((format(printf, 2, 3)));

extern int write_trace_raw(reiser4_trace_file * file, const void *data, size_t len);
extern int hold_trace(reiser4_trace_file * file, int flag);
extern int disable_trace(reiser4_trace_file * file, int flag);
extern void close_trace_file(reiser4_trace_file * file);

#define write_syscall_trace(format, ...)	\
	write_current_tracef("%s "format, __FUNCTION__ , ## __VA_ARGS__)
extern void write_node_trace(const jnode *node);
struct address_space;
extern void write_page_trace(const struct address_space *mapping,
			     unsigned long index);
extern void write_io_trace(const char *moniker, int rw, struct bio *bio);
extern void write_tree_trace(reiser4_tree * tree, reiser4_traced_op op, ...);

extern char *jnode_short_info(const jnode *j, char *buf);

#define trace_mark(mark)  write_current_tracef("mark=" #mark "\n")

#else

typedef struct {
} reiser4_trace_file;

#define open_trace_file(super, file_name, size, trace) (0)
#define write_trace(file, format, ...) (0)
#define write_trace_raw(file, data, len) (0)
#define hold_trace(file, flag) (0)
#define disable_trace(file, flag) (0)
#define close_trace_file(file) noop

#define write_syscall_trace(format, ...) noop
#define write_tree_trace(tree, op, ...) noop
#define write_node_trace(node) noop
#define write_page_trace(mapping, index) noop
#define jnode_short_info(j, buf) buf

#define trace_mark(mark)  noop

#endif

#define write_tracef(file, super, format, ...)		\
({							\
	write_trace(file, "%i %s %s %lu " format "\n",	\
		    current->pid, current->comm,	\
		    super->s_id,			\
		    jiffies , ## __VA_ARGS__);		\
})

#define write_current_tracef(format, ...)			\
({								\
	struct super_block *__super;				\
								\
	__super = reiser4_get_current_sb();			\
	write_tracef(&get_super_private(__super)->trace_file,	\
		     __super, format, ## __VA_ARGS__);		\
})

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
