/* Copyright 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

#if !defined( __PLUGIN_XATTR_H__ )
#define __PLUGIN_XATTR_H__

#include "../type_safe_list.h"

#include "plugin_header.h"

struct reiser4_xattr_plugin {
	/* generic fields */
	plugin_header h;

	char *prefix;
	size_t (*list)(char *list, struct inode *inode, const char *name,
		       int name_len);
	int (*get)(struct inode *inode, const char *name, void *buffer,
		   size_t size);
	int (*set)(struct inode *inode, const char *name, const void *buffer,
		   size_t size, int flags);
};

TYPE_SAFE_LIST_DECLARE(xattr);

typedef struct xattr_namespace {
	/* all name spaces of inode are linked through ->linkage */
	xattr_list_link linkage;
	/* zero-terminated array of plugins */
	reiser4_xattr_plugin *plug;
} xattr_namespace;

TYPE_SAFE_LIST_DEFINE(xattr, xattr_namespace, linkage);

extern void xattr_add_common_namespace(xattr_namespace *ns);
extern void xattr_del_common_namespace(xattr_namespace *ns);
extern void xattr_add_global_namespace(xattr_namespace *ns);
extern void xattr_del_global_namespace(xattr_namespace *ns);

ssize_t xattr_get_common(struct dentry *dentry, const char *name,
			 void *buffer, size_t size);

ssize_t xattr_list_common(struct dentry *dentry, char *buffer, size_t size);

int xattr_set_common(struct dentry *dentry, const char *name,
		     const void *value, size_t size, int flags);

int xattr_remove_common(struct dentry *dentry, const char *name);

int xattr_add_namespace(struct inode *inode, reiser4_xattr_plugin *plug);
void xattr_del_namespace(struct inode *inode, reiser4_xattr_plugin *plug);
void xattr_clean(struct inode *inode);

/* __PLUGIN_XATTR_H__ */
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
