/* Copyright 2000, 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Tree-tracing facility. Copied from reiserfs v3.x patch, never released. See
 * log.c for comments. */

#if !defined( __REISER4_LOG_H__ )
#define __REISER4_LOG_H__

#include "forward.h"
#include "debug.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block, etc  */
#include <asm/semaphore.h>

typedef enum { log_to_file, log_to_console, log_to_bucket } log_file_type;

#if REISER4_LOG

typedef struct {
	log_file_type type;
	struct file *fd;
	char *buf;
	size_t size;
	size_t used;
	spinlock_t lock;
	struct list_head wait;
	int disabled;
	int long_term;
} reiser4_log_file;

typedef enum {
	tree_cut = 'c',
	tree_lookup = 'l',
	tree_insert = 'i',
	tree_paste = 'p',
	tree_cached = 'C',
	tree_exit = 'e'
} reiser4_log_op;

extern int open_log_file(struct super_block *super, const char *file_name, size_t size, reiser4_log_file * log);
extern int write_log(reiser4_log_file * file, const char *format, ...)
    __attribute__ ((format(printf, 2, 3)));

extern int write_log_raw(reiser4_log_file * file, const void *data, size_t len);
extern int hold_log(reiser4_log_file * file, int flag);
extern int disable_log(reiser4_log_file * file, int flag);
extern void close_log_file(reiser4_log_file * file);

#define write_syscall_log(format, ...)	\
	write_current_logf(WRITE_SYSCALL_LOG, "%s "format, __FUNCTION__ , ## __VA_ARGS__)
extern void write_node_log(const jnode *node);
struct address_space;
extern void write_page_log(const struct address_space *mapping,
			     unsigned long index);
extern void write_io_log(const char *moniker, int rw, struct bio *bio);
extern void write_tree_log(reiser4_tree * tree, reiser4_log_op op, ...);

extern char *jnode_short_info(const jnode *j, char *buf);


#else /* NO LOG */

typedef struct {
} reiser4_log_file;

#define open_log_file(super, file_name, size, log) (0)
#define write_log(file, format, ...) (0)
#define write_log_raw(file, data, len) (0)
#define hold_log(file, flag) (0)
#define disable_log(file, flag) (0)
#define close_log_file(file) noop

#define write_syscall_log(format, ...) noop
#define write_tree_log(tree, op, ...) noop
#define write_node_log(node) noop
#define write_page_log(mapping, index) noop
#define jnode_short_info(j, buf) buf

#endif

#define write_current_logf(log_flag, format, ...)				\
({										\
	struct super_block *super;						\
										\
	super = reiser4_get_current_sb();					\
	IF_LOG(log_flag, write_log(&get_super_private(super)->log_file,		\
                                   "%s %s %s " format "\n",			\
				   current->comm,				\
				   super->s_id, __FUNCTION__, ## __VA_ARGS__));	\
})

/* __REISER4_LOG_H__ */
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
