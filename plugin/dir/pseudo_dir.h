/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Directory plugin for pseudo files */

#if !defined( __PSEUDO_DIR_H__ )
#define __PSEUDO_DIR_H__

#include "../../forward.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry */

extern int lookup_pseudo(struct inode * parent, struct dentry **dentry);
extern int readdir_pseudo(struct file *f, void *dirent, filldir_t filld);

/* __PSEUDO_DIR_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
