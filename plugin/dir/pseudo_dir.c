/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Directory plugin for pseudo files */

#include "../../debug.h"
#include "../../inode.h"
#include "../pseudo/pseudo.h"
#include "dir.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry */

/* implementation of ->lookup() method for pseudo files. */
reiser4_internal int lookup_pseudo(struct inode * parent, struct dentry **dentry)
{
	pseudo_plugin *pplug;
	int result;

	pplug = reiser4_inode_data(parent)->file_plugin_data.pseudo_info.plugin;
	assert("nikita-3222", pplug->lookup != NULL);
	result = pplug->lookup(parent, dentry);
	if (result == -ENOENT)
		result = lookup_pseudo_file(parent, dentry);
	return result;
}


reiser4_internal int
readdir_pseudo(struct file *f, void *dirent, filldir_t filld)
{
	pseudo_plugin *pplug;
	struct inode  *inode;
	struct dentry *dentry;
	int result = 0;

	dentry = f->f_dentry;
	inode = dentry->d_inode;
	pplug = reiser4_inode_data(inode)->file_plugin_data.pseudo_info.plugin;
	if (pplug->readdir != NULL)
		result = pplug->readdir(f, dirent, filld);
	else {
		ino_t ino;
		int i;

		i = f->f_pos;
		switch (i) {
		case 0:
			ino = get_inode_oid(dentry->d_inode);
			if (filld(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			f->f_pos++;
			i++;
			/* fallthrough */
		case 1:
			ino = parent_ino(dentry);
			if (filld(dirent, "..", 2, i, ino, DT_DIR) < 0)
				break;
			f->f_pos++;
			i++;
			/* fallthrough */
		}
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
   End:
*/
