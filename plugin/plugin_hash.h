/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Generic hash table for use by external plugins. see
 * fs/reiser4/plugin/plugin_hash.c for details */

#if !defined(__PLUGIN_HASH_H__)
#define __PLUGIN_HASH_H__

#include "plugin_header.h"
#include "../type_safe_list.h"
#include "../type_safe_hash.h"

typedef enum phash_scope {
	PHASH_INODE,
	PHASH_JNODE,
	PHASH_SUPER,

	PHASH_LAST
} phash_scope;

struct phash_header;
typedef struct phash_header phash_header;

struct phash_user;
typedef struct phash_user phash_user;

typedef struct phash_ops {
	int (*destroy)(phash_user *user, void *object, phash_header *value);
} phash_ops;

TYPE_SAFE_LIST_DECLARE(phash);

struct phash_user {
	reiser4_plugin_type type_id;
	reiser4_plugin_id   id;
	phash_ops           ops;
	phash_scope         scope;
	phash_list_link     link;
};

TYPE_SAFE_HASH_DECLARE(phash, phash_header);

struct phash_header {
	phash_hash_link link;
	phash_user *user;
	void       *object;
};

extern int  phash_user_register  (phash_user *user);
extern void phash_user_unregister(phash_user *user);

extern phash_header *phash_get(phash_user *user, void *object);
extern void  phash_set(phash_user *user, void *object, phash_header *value);

extern int phash_destroy_hook(phash_scope scope, void *object);

extern int phash_init(void);
extern void phash_done(void);

static inline int
phash_inode_destroy(struct inode *inode)
{
	return phash_destroy_hook(PHASH_INODE, inode);
}

static inline int
phash_jnode_destroy(jnode *node)
{
	return phash_destroy_hook(PHASH_JNODE, node);
}

static inline int
phash_super_destroy(struct super_block *super)
{
	return phash_destroy_hook(PHASH_SUPER, super);
}

/* __PLUGIN_HASH_H__ */
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

