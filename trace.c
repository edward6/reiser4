/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Tracing facility. Copied from reiserfs v3.x patch, never released */

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "trace.h"
#include "super.h"

#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/slab.h>

#if REISER4_TRACE_TREE

static int flush_trace(reiser4_trace_file * trace);
static int free_space(reiser4_trace_file * trace, size_t * len);
static int lock_trace(reiser4_trace_file * trace);
static void unlock_trace(reiser4_trace_file * trace);

#define LOCK_OR_FAIL( trace )			\
({						\
	int __result;				\
						\
	__result = lock_trace( trace );		\
	if( __result != 0 )			\
		return __result;		\
})

int
open_trace_file(struct super_block *super, const char *file_name, size_t size, reiser4_trace_file * trace)
{
	assert("nikita-2498", file_name != NULL);
	assert("nikita-2499", trace != NULL);
	assert("nikita-2500", size > 0);

	xmemset(trace, 0, sizeof *trace);

	init_MUTEX(&trace->held);
	if (!strcmp(file_name, "/dev/null")) {
		trace->type = log_to_bucket;
		return 0;
	}
	trace->buf = kmalloc(size, GFP_KERNEL);
	if (trace->buf == NULL)
		return -ENOMEM;
	trace->size = size;
	if (!strcmp(file_name, "/dev/console")) {
		trace->type = log_to_console;
		return 0;
	}
	trace->fd = filp_open(file_name, O_CREAT | O_WRONLY, S_IFREG | S_IWUSR);
	if (IS_ERR(trace->fd)) {
		warning("nikita-2501", "cannot open trace file '%s': %li", file_name, PTR_ERR(trace->fd));
		trace->fd = NULL;
		return PTR_ERR(trace->fd);
	}
	if (trace->fd->f_dentry->d_inode->i_sb == super) {
		warning("nikita-2506", "Refusing to log onto traced fs");
		return -EINVAL;
	}
	trace->fd->f_dentry->d_inode->i_flags |= S_NOATIME;
	trace->fd->f_flags |= O_APPEND;
	trace->type = log_to_file;
	return 0;
}

int
write_trace(reiser4_trace_file * file, const char *format, ...)
{
	size_t len;
	int result;
	va_list args;

	if ((file == NULL) || (file->type == log_to_bucket) || (file->buf == NULL) || (file->disabled > 0))
		return 0;

	va_start(args, format);
	len = vsnprintf((char *) format, 0, format, args) + 1;
	va_end(args);

	LOCK_OR_FAIL(file);
	result = free_space(file, &len);
	if (result == 0) {
		va_start(args, format);
		file->used += vsnprintf(file->buf + file->used, file->size - file->used, format, args);
		va_end(args);
	}
	unlock_trace(file);
	return result;
}

int
write_trace_raw(reiser4_trace_file * file, const void *data, size_t len)
{
	int result;

	if ((file == NULL) || (file->type == log_to_bucket) || (file->buf == NULL) || (file->disabled > 0))
		return 0;

	LOCK_OR_FAIL(file);
	result = free_space(file, &len);
	if (result == 0) {
		xmemcpy(file->buf + file->used, data, (size_t) len);
		file->used += len;
	}
	unlock_trace(file);
	return result;
}

void
close_trace_file(reiser4_trace_file * trace)
{
	flush_trace(trace);
	if (trace->fd != NULL)
		filp_close(trace->fd, NULL);
	if (trace->buf != NULL)
		kfree(trace->buf);
}

int
hold_trace(reiser4_trace_file * file, int flag)
{
	if (flag)
		return lock_trace(file);
	else {
		unlock_trace(file);
		return 0;
	}
}

int
disable_trace(reiser4_trace_file * file, int flag)
{
	LOCK_OR_FAIL(file);
	file->disabled += flag ? +1 : -1;
	unlock_trace(file);
	return 0;
}

#define START_KERNEL_IO				\
        {					\
		mm_segment_t __ski_old_fs;	\
						\
		__ski_old_fs = get_fs();	\
		set_fs( KERNEL_DS )

#define END_KERNEL_IO				\
		set_fs( __ski_old_fs );		\
	}

static int
lock_trace(reiser4_trace_file * trace)
{
	return down_interruptible(&trace->held);
}

static void
unlock_trace(reiser4_trace_file * trace)
{
	up(&trace->held);
}

static int
flush_trace(reiser4_trace_file * file)
{
	int result;

	result = 0;
	switch (file->type) {
	case log_to_file:{
			struct file *fd;

			fd = file->fd;
			if (fd && fd->f_op != NULL && fd->f_op->write != NULL) {
				int written;

				written = 0;
				START_KERNEL_IO;
				while (file->used > 0) {
					result = fd->f_op->write(fd, file->buf + written, file->used, &fd->f_pos);
					if (result > 0) {
						file->used -= result;
						written += result;
					} else {
						warning("nikita-2502", "Error writing trace: %i", result);
						break;
					}
				}
				END_KERNEL_IO;
			} else {
				warning("nikita-2504", "no ->write() in trace-file");
				result = -EINVAL;
			}
			break;
		}
	default:
		warning("nikita-2505", "unknown trace-file type: %i. Dumping to console", file->type);
	case log_to_console:
		if (file->buf != NULL)
			info(file->buf);
	case log_to_bucket:
		file->used = 0;
		break;
	}
	return result;
}

static int
free_space(reiser4_trace_file * file, size_t * len)
{
	if (*len > file->size) {
		warning("nikita-2503", "trace record too large: %i > %i. Truncating", *len, file->size);
		*len = file->size;
	}
	while (*len > file->size - file->used) {
		int result;

		/* flushing can sleep, so loop */
		result = flush_trace(file);
		if (result < 0)
			return result;
	}
	return 0;
}

int
write_trace_stamp(reiser4_tree * tree, reiser4_traced_op op, ...)
{
	reiser4_trace_file *file;
	va_list args;
	char buf[200];
	char *rest;
	reiser4_key *key;

	file = &get_super_private(tree->super)->trace_file;

	if (no_context) {
		info("cannot write trace from interrupt\n");
		return 0;
	}

	va_start(args, op);

	key = va_arg(args, reiser4_key *);
	rest = buf + sprintf_key(buf, key);
	*rest++ = ':';
	*rest = '\0';

	switch (op) {
	case tree_cut:{
			reiser4_key *to;

			to = va_arg(args, reiser4_key *);
			rest += sprintf_key(rest, to);
			break;
		}
	case tree_lookup:{
	default:
			break;
		}
	case tree_insert:
	case tree_paste:{
			reiser4_item_data *data;
			coord_t *coord;
			__u32 flags;

			data = va_arg(args, reiser4_item_data *);
			coord = va_arg(args, coord_t *);
			flags = va_arg(args, __u32);

			rest += sprintf(rest, "%s:(%u:%u):%x",
					data->iplug->h.label, coord->item_pos, coord->unit_pos, flags);
		}
	}
	va_end(args);
	return write_trace(file, "%i:[%s]:%c:%x:%lu:%s\n",
			   current->pid, kdevname(to_kdev_t(tree->super->s_dev)), op, 0xacc0u, jiffies, buf);
}

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
