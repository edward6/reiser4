/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "reiser4.h"
#include "ioctl.h"
#include "tree.h"
#include "inode.h"

/* reiser4_ioctl - handler for ioctl for inode supported commands:
   
   REISER4_IOC_UNPACK - try to unpack tail from into extent and prevent packing 
   file (argument arg has to be non-zero)
*/
int
reiser4_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int result;

	REISER4_ENTRY(filp->f_dentry->d_inode->i_sb);

	switch (cmd) {
	case REISER4_IOC_UNPACK:

		if (arg) {
			result = reiser4_unpack(inode, filp);
			break;
		}

	default:
		result = -ENOTTY;
		break;
	}

	REISER4_EXIT(result);
}

/* reiser4_unpack -- function try to convert tail into extent by means of using
   tail2extent function. */
int
reiser4_unpack(struct inode *inode, struct file *filp)
{
	int result;

	get_nonexclusive_access(inode);
	result = tail2extent(inode);
	drop_nonexclusive_access(inode);

	if (result == 0) {
		reiser4_inode *state;

		state = reiser4_inode_data(inode);
		state->tail = tail_plugin_by_id(NEVER_TAIL_ID);
		inode_set_plugin(inode, tail_plugin_to_plugin(state->tail));
		result = reiser4_write_sd(inode);
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
