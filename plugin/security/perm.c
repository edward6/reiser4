/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/*
 * this file contains implementation of permission plugins. Currently, only
 * RWX_PERM_ID is implemented
 */

#include "../plugin.h"
#include "../plugin_header.h"
#include "../../debug.h"

#include <linux/fs.h>
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/quotaops.h>
#include <asm/uaccess.h>

static int mask_ok_common(struct inode *inode, int mask)
{
	return generic_permission(inode, mask, NULL);
}

static int setattr_ok_common(struct dentry *dentry, struct iattr *attr)
{
	int result;
	struct inode *inode;

	assert("nikita-2272", dentry != NULL);
	assert("nikita-2273", attr != NULL);

	inode = dentry->d_inode;
	assert("nikita-2274", inode != NULL);

	result = inode_change_ok(inode, attr);
	if (result == 0) {
		unsigned int valid;

		valid = attr->ia_valid;
		if ((valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
		    (valid & ATTR_GID && attr->ia_gid != inode->i_gid))
			result = DQUOT_TRANSFER(inode, attr) ? -EDQUOT : 0;
	}
	return result;
}

static int
read_ok_common(struct file *file, const char __user *buf, size_t size, loff_t * off)
{
	return access_ok(VERIFY_WRITE, buf, size) ? 0 : -EFAULT;
}

static int
write_ok_common(struct file *file, const char __user *buf, size_t size, loff_t * off)
{
	return access_ok(VERIFY_READ, buf, size) ? 0 : -EFAULT;
}

perm_plugin perm_plugins[LAST_PERM_ID] = {
	[RWX_PERM_ID] = {
		.h = {
			.type_id = REISER4_PERM_PLUGIN_TYPE,
			.id = RWX_PERM_ID,
			.pops = NULL,
			.label = "rwx",
			.desc = "standard UNIX permissions",
			.linkage = {NULL, NULL}
		},
		.read_ok = read_ok_common,
		.write_ok = write_ok_common,
		.lookup_ok = NULL,
		.create_ok = NULL,
		.link_ok = NULL,
		.unlink_ok = NULL,
		.delete_ok = NULL,
		.mask_ok = mask_ok_common,
		.setattr_ok = setattr_ok_common,
		.getattr_ok = NULL,
		.rename_ok = NULL,
	}
};

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
