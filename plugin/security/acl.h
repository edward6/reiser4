/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Directory entry. */

#if !defined(__FS_REISER4_PLUGIN_SECURITY_ACL_H__)
#define __FS_REISER4_PLUGIN_SECURITY_ACL_H__

#include "../../forward.h"

#include "../plugin.h"
#include "../xattr.h"

typedef struct acl_perm_info {
	struct posix_acl *access;
	struct posix_acl *dfault;
} acl_perm_info_t;

int mask_ok_acl(struct inode *inode, int mask);
void clear_acl(struct inode *inode);

extern reiser4_plugin_ops acl_plugin_ops;

int reiser4_set_acl(struct inode *inode, int type, struct posix_acl *acl);

/* __FS_REISER4_PLUGIN_SECURITY_ACL_H__ */
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
