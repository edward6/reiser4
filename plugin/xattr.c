/* Copyright 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "../inode.h"
#include "../debug.h"

#include "xattr.h"
#include "plugin.h"

#include <linux/xattr.h>

static xattr_list_head
global_namespaces = (xattr_list_head)TYPE_SAFE_LIST_HEAD_INIT(global_namespaces);

xattr_list_head
xattr_common_namespaces = (xattr_list_head)TYPE_SAFE_LIST_HEAD_INIT(xattr_common_namespaces);


/* copied from ext2 */
static inline const char *
strcmp_prefix(const char *a, const char *a_prefix)
{
	while (*a_prefix && *a == *a_prefix) {
		a++;
		a_prefix++;
	}
	return *a_prefix ? NULL : a;
}

static reiser4_xattr_plugin *
xattr_search_table(const char **name, reiser4_xattr_plugin *table)
{
	int i;
	reiser4_xattr_plugin *result;

	for (i = 0, result = NULL; table[i].prefix != NULL ; ++i) {
		const char *match;

		match = strcmp_prefix(*name, table[i].prefix);
		if (match != NULL) {
			result = &table[i];
			*name = match;
			break;
		}
	}
	return result;
}

static reiser4_xattr_plugin *
xattr_search_namespaces(const char **name, xattr_list_head *head)
{
	xattr_namespace *ns;
	reiser4_xattr_plugin *result;

	for_all_type_safe_list(xattr, head, ns) {
		result = xattr_search_table(name, ns->plug);
		if (result != NULL)
			return result;
	}
	return NULL;
}

static reiser4_xattr_plugin *
xattr_resolve_name(const char **name, struct inode *inode)
{
	reiser4_inode *info;
	reiser4_xattr_plugin *result;
	file_plugin *fplug;

	if (*name == NULL)
		return NULL;

	info = reiser4_inode_data(inode);
	result = xattr_search_namespaces(name, &info->xattr_namespaces);
	if (result != NULL)
		return result;
	fplug = inode_file_plugin(inode);
	result = xattr_search_namespaces(name, fplug->xattr.ns);
	if (result != NULL)
		return result;
	return xattr_search_namespaces(name, &global_namespaces);
}

ssize_t
xattr_get_common(struct dentry *dentry, const char *name,
	      void *buffer, size_t size)
{
	reiser4_xattr_plugin *handler;
	struct inode *inode;

	inode = dentry->d_inode;
	handler = xattr_resolve_name(&name, inode);
	if (handler != NULL)
		return handler->get(inode, name, buffer, size);
	else
		return RETERR(-EOPNOTSUPP);
}

ssize_t
xattr_list_common(struct dentry *dentry, char *buffer, size_t size)
{
	return RETERR(-EOPNOTSUPP);
}

int
xattr_set_common(struct dentry *dentry, const char *name,
	      const void *value, size_t size, int flags)
{
	reiser4_xattr_plugin *handler;
	struct inode *inode;

	inode = dentry->d_inode;

	if (size == 0)
		value = "";  /* empty EA, do not remove */
	handler = xattr_resolve_name(&name, inode);
	if (handler != NULL)
		return handler->set(inode, name, value, size, flags);
	else
		return RETERR(-EOPNOTSUPP);
}

int
xattr_remove_common(struct dentry *dentry, const char *name)
{
	reiser4_xattr_plugin *handler;
	struct inode *inode;

	inode = dentry->d_inode;

	handler = xattr_resolve_name(&name, inode);
	if (handler != NULL)
		return handler->set(inode, name, NULL, 0, XATTR_REPLACE);
	else
		return RETERR(-EOPNOTSUPP);
}

int
xattr_add_namespace(struct inode *inode, reiser4_xattr_plugin *plug)
{
	reiser4_inode *info;
	xattr_namespace *ns;

	info = reiser4_inode_data(inode);
	ns = reiser4_kmalloc(sizeof *ns, GFP_KERNEL);
	if (ns != NULL) {
		ns->plug = plug;
		xattr_list_push_front(&info->xattr_namespaces, ns);
		return 0;
	} else
		return RETERR(-ENOMEM);
}

void
xattr_del_namespace(struct inode *inode, reiser4_xattr_plugin *plug)
{
	xattr_namespace *ns;
	reiser4_inode *info;

	info = reiser4_inode_data(inode);
	for_all_type_safe_list(xattr, &info->xattr_namespaces, ns) {
		if (ns->plug == plug) {
			xattr_list_remove(ns);
			reiser4_kfree(ns, sizeof *ns);
			break;
		}
	}
}

void
xattr_add_common_namespace(xattr_namespace *ns)
{
	xattr_list_push_front(&xattr_common_namespaces, ns);
}

void
xattr_del_common_namespace(xattr_namespace *ns)
{
	xattr_list_remove(ns);
}

void
xattr_add_global_namespace(xattr_namespace *ns)
{
	xattr_list_push_front(&global_namespaces, ns);
}

void
xattr_del_global_namespace(xattr_namespace *ns)
{
	xattr_list_remove(ns);
}

void
xattr_clean(struct inode *inode)
{
	reiser4_inode *info;
	reiser4_context ctx;

	info = reiser4_inode_data(inode);
	init_context(&ctx, inode->i_sb);
	while (!xattr_list_empty(&info->xattr_namespaces)) {
		xattr_namespace *ns;

		ns = xattr_list_front(&info->xattr_namespaces);
		xattr_list_remove(ns);
		reiser4_kfree(ns, sizeof *ns);
	}
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
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
