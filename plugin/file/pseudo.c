/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "pseudo.h"
#include "../plugin.h"

#include "../../inode.h"

#include <linux/seq_file.h>
#include <linux/fs.h>

struct seq_operations pseudo_seq_op;

static pseudo_plugin *
get_pplug(struct file * file)
{
	struct inode  *inode;

	inode = file->f_dentry->d_inode;
	return reiser4_inode_data(inode)->file_plugin_data.pseudo_info.plugin;
}

int open_pseudo(struct inode * inode, struct file * file)
{
	int result;
	pseudo_plugin *pplug;

	pplug = get_pplug(file);

	if (pplug->read_type == PSEUDO_READ_SEQ) {
		result = seq_open(file, &pplug->read.ops);
		if (result == 0) {
			struct seq_file *m;

			m = file->private_data;
			m->private = file;
		}
	} else if (pplug->read_type == PSEUDO_READ_SINGLE)
		result = single_open(file, pplug->read.single_show, file);
	else
		result = 0;

	return result;
}

ssize_t read_pseudo(struct file *file,
		    char __user *buf, size_t size, loff_t *ppos)
{
	switch (get_pplug(file)->read_type) {
	case PSEUDO_READ_SEQ:
	case PSEUDO_READ_SINGLE:
		/* seq_file behaves like pipe, requiring @ppos to always be
		 * address of file->f_pos */
		return seq_read(file, buf, size, &file->f_pos);
	case PSEUDO_READ_FORWARD:
		return get_pplug(file)->read.read(file, buf, size, ppos);
	default:
		return 0;
	}
}

loff_t seek_pseudo(struct file *file, loff_t offset, int origin)
{
	switch (get_pplug(file)->read_type) {
	case PSEUDO_READ_SEQ:
	case PSEUDO_READ_SINGLE:
		return seq_lseek(file, offset, origin);
	default:
		return 0;
	}
}

int release_pseudo(struct inode *inode, struct file *file)
{
	int result;

	switch (get_pplug(file)->read_type) {
	case PSEUDO_READ_SEQ:
	case PSEUDO_READ_SINGLE:
		result = seq_release(inode, file);
		file->private_data = NULL;
		break;
	default:
		result = 0;
	}
	return result;
}

void drop_pseudo(struct inode * object)
{
	/* pseudo files are not protected from deletion by their ->i_nlink */
	generic_delete_inode(object);
}

ssize_t write_pseudo(struct file *file,
		     const char __user *buf, size_t size, loff_t *ppos)
{
	ssize_t result;

	switch (get_pplug(file)->write_type) {
	case PSEUDO_WRITE_STRING: {
		char * inkernel;

		inkernel = getname(buf);
		if (!IS_ERR(inkernel)) {
			result = get_pplug(file)->write.gets(file, inkernel);
			putname(inkernel);
			if (result == 0)
				result = size;
		} else
			result = PTR_ERR(inkernel);
		break;
	}
	case PSEUDO_WRITE_FORWARD:
		result = get_pplug(file)->write.write(file, buf, size, ppos);
		break;
	default:
		result = size;
	}
	return result;
}

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
