/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Tracing facility. Copied from reiserfs v3.x patch, never released */

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "trace.h"
#include "super.h"
#include "inode.h"
#include "page_cache.h" /* for jprivate() */

#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>

#if REISER4_TRACE_TREE

static int trace_flush(reiser4_trace_file * trace);
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
	int mapping_mask;
	assert("nikita-2498", file_name != NULL);
	assert("nikita-2499", trace != NULL);
	assert("nikita-2500", size > 0);

	xmemset(trace, 0, sizeof *trace);

	spin_lock_init(&trace->lock);
	INIT_LIST_HEAD(&trace->wait);

	if (!strcmp(file_name, "/dev/null")) {
		trace->type = log_to_bucket;
		return 0;
	}
	trace->buf = vmalloc(size);
	if (trace->buf == NULL)
		return RETERR(-ENOMEM);
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
		return RETERR(-EINVAL);
	}
	trace->fd->f_dentry->d_inode->i_flags |= S_NOATIME;
	trace->fd->f_flags |= O_APPEND;
	mapping_mask = mapping_gfp_mask(trace->fd->f_dentry->d_inode->i_mapping);
	mapping_mask &= ~__GFP_FS;
	mapping_mask |= GFP_NOFS;
	mapping_set_gfp_mask( trace->fd->f_dentry->d_inode->i_mapping, mapping_mask);
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
	if (trace->type == log_to_file && lock_trace(trace) == 0) {
		trace_flush(trace);
		unlock_trace(trace);
	}
	if (trace->fd != NULL)
		filp_close(trace->fd, NULL);
	if (trace->buf != NULL) {
		vfree(trace->buf);
		trace->buf = NULL;
	}
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

struct __wlink {
	struct list_head link;
	struct semaphore sema;
};

static int
lock_trace(reiser4_trace_file * trace)
{
	int ret = 0;

	spin_lock(&trace->lock);

	while (trace->long_term) {
		/* sleep on a semaphore */
		struct __wlink link;
		sema_init(&link.sema, 0);
		list_add(&link.link, &trace->wait);
		spin_unlock(&trace->lock);

		ret = down_interruptible(&link.sema);

		spin_lock(&trace->lock);
		list_del(&link.link);
	}

	return ret;
}

static void
unlock_trace(reiser4_trace_file * trace)
{
	spin_unlock(&trace->lock);
}

static void convert_to_longterm (reiser4_trace_file * trace)
{
	assert ("zam-833", trace->long_term == 0);
	trace->long_term = 1;
	spin_unlock(&trace->lock);
}

static void convert_to_shortterm (reiser4_trace_file * trace)
{
	struct list_head * pos;

	spin_lock(&trace->lock);
	assert ("zam-834", trace->long_term);
	trace->long_term = 0;
	list_for_each(pos, &trace->wait) {
		struct __wlink * link;
		link = list_entry(pos, struct __wlink, link);
		up(&link->sema);
	}
}

static int
trace_flush(reiser4_trace_file * file)
{
	int result;

	result = 0;
	switch (file->type) {
	case log_to_file:{
		struct file *fd;

		convert_to_longterm(file);

		fd = file->fd;
		if (fd && fd->f_op != NULL && fd->f_op->write != NULL) {
			int written;

			written = 0;
			START_KERNEL_IO;
			while (file->used > 0) {
				result = vfs_write(fd, file->buf + written, file->used, &fd->f_pos);
				if (result > 0) {
					file->used -= result;
					written += result;
				} else {
					static int log_io_failed = 0;
					
					if (IS_POW(log_io_failed))
						warning("nikita-2502", "Error writing trace: %i", result);
					++ log_io_failed;
					break;
				}
			}
			END_KERNEL_IO;
		} else {
			warning("nikita-2504", "no ->write() in trace-file");
			result = RETERR(-EINVAL);
		}

		convert_to_shortterm(file);

		break;
	}
	default:
		warning("nikita-2505", "unknown trace-file type: %i. Dumping to console", file->type);
	case log_to_console:
		if (file->buf != NULL)
			printk(file->buf);
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
		result = trace_flush(file);
		if (result < 0)
			return result;
	}
	return 0;
}

void
write_tree_trace(reiser4_tree * tree, reiser4_traced_op op, ...)
{
	va_list args;
	char buf[200];
	char *rest;
	reiser4_key *key;

	if (unlikely(in_interrupt() || in_irq())) {
		printk("cannot write trace from interrupt\n");
		return;
	}

	va_start(args, op);

	rest = buf;
	rest += sprintf(rest, "....tree %c ", op);

	if (op != tree_cached && op != tree_exit) {
		key = va_arg(args, reiser4_key *);
		rest += sprintf_key(rest, key);
		*rest++ = ' ';
		*rest = '\0';

		switch (op) {
		case tree_cut: {
			reiser4_key *to;

			to = va_arg(args, reiser4_key *);
			rest += sprintf_key(rest, to);
			break;
		}
		case tree_lookup:
		default:
			break;
		case tree_insert:
		case tree_paste: {
			reiser4_item_data *data;
			coord_t *coord;
			__u32 flags;

			data = va_arg(args, reiser4_item_data *);
			coord = va_arg(args, coord_t *);
			flags = va_arg(args, __u32);

			rest += sprintf(rest, "%s (%u,%u) %x",
					data->iplug->h.label, 
					coord->item_pos, coord->unit_pos, flags);
		}
		}
	}
	va_end(args);
	write_current_tracef("%s", buf);
}

char *
jnode_short_info(const jnode *j, char *buf)
{
	if (j == NULL) {
		sprintf(buf, "null");
	} else {
		sprintf(buf, "%i %c %c %i",
			jnode_get_level(j),
			jnode_is_znode(j) ? 'Z' :
			jnode_is_unformatted(j) ? 'J' : '?',
			JF_ISSET(j, JNODE_OVRWR) ? 'O' :
			JF_ISSET(j, JNODE_RELOC) ? 'R' : ' ',
			j->atom ? j->atom->atom_id : -1);
	}
	return buf;
}


void
write_node_trace(const jnode *node)
{
	char jbuf[100];

	jnode_short_info(node, jbuf);
	write_current_tracef(".....node %s %s", 
			     sprint_address(jnode_get_block(node)), jbuf);
}

void
write_page_trace(const struct address_space *mapping, unsigned long index)
{
	write_current_tracef(".....page %llu %lu", get_inode_oid(mapping->host),
			     index);
}

void
write_io_trace(const char *moniker, int rw, struct bio *bio)
{
	struct super_block *super;
	reiser4_super_info_data *sbinfo;
	reiser4_block_nr start;
	char jbuf[100];

	super = reiser4_get_current_sb();
	sbinfo = get_super_private(super);

	start = bio->bi_sector >> (super->s_blocksize_bits - 9);
	jnode_short_info(jprivate(bio->bi_io_vec[0].bv_page), jbuf);
	write_current_tracef("......bio %s %c %+lli  (%llu,%u) %s",
			     moniker, (rw == READ) ? 'r' : 'w',
			     start - sbinfo->last_touched - 1,
			     start, bio->bi_vcnt, jbuf);
	sbinfo->last_touched = start + bio->bi_vcnt - 1;
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
