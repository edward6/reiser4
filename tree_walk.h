/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* definitions of reiser4 tree walk functions */

#ifndef __FS_REISER4_TREE_WALK_H__
#define __FS_REISER4_TREE_WALK_H__

#include "debug.h"
#include "forward.h"

/* establishes horizontal links between cached znodes */
int connect_znode(coord_t * coord, znode * node);

/* tree traversal functions (reiser4_get_parent(), reiser4_get_neighbor())
  have the following common arguments:

  return codes:

  @return : 0        - OK, 

	    -ENOENT  - neighbor is not in cache, what is detected by sibling
	               link absence.

            -E_NO_NEIGHBOR - we are sure that neighbor (or parent) node cannot be
                       found (because we are left-/right- most node of the
		       tree, for example). Also, this return code is for
		       reiser4_get_parent() when we see no parent link -- it
		       means that our node is root node.

            -EDEADLK - deadlock detected (request from high-priority process
	               received), other error codes are conformed to
		       /usr/include/asm/errno.h .

    * pointer to lock stack is not passed because we have magic
    * get_current_stack() function.

*/

int
reiser4_get_parent_flags(lock_handle * result, znode * node,
			 znode_lock_mode mode, int flags);

int reiser4_get_parent(lock_handle * result, znode * node, znode_lock_mode mode, int only_connected_p);

/* bits definition for reiser4_get_neighbor function `flags' arg. */
typedef enum {
	/* allows to read block from disk */
	GN_DO_READ = 0x1,
	/* locking left neighbor in stead of right one */
	GN_GO_LEFT = 0x2,
	/* automatically load neighbor node content */
	GN_LOAD_NEIGHBOR = 0x4,
	/* return -EAGAIN if can't lock  */
	GN_TRY_LOCK = 0x8,
	/* used internally in tree_walk.c, causes renew_sibling do not
	   allocate neighbor znode, but only search for him in znode cache */
	GN_NO_ALLOC = 0x10,
	/* do not go across atom boundaries */
	GN_SAME_ATOM = 0x20,
	/* allow to lock not connected nodes */
	GN_ALLOW_NOT_CONNECTED = 0x40
} znode_get_neigbor_flags;

int reiser4_get_neighbor(lock_handle * neighbor, znode * node, znode_lock_mode lock_mode, int flags);

/* there are wrappers for most common usages of reiser4_get_neighbor() */
static inline int
reiser4_get_left_neighbor(lock_handle * result, znode * node, int lock_mode, int flags)
{
	return reiser4_get_neighbor(result, node, lock_mode, flags | GN_GO_LEFT);
}

static inline int
reiser4_get_right_neighbor(lock_handle * result, znode * node, int lock_mode, int flags)
{
	ON_DEBUG(check_lock_node_data(node));
	ON_DEBUG(check_lock_data());
	return reiser4_get_neighbor(result, node, lock_mode, flags & (~GN_GO_LEFT));
}

extern void invalidate_lock(lock_handle * _link);

extern void sibling_list_remove(znode * node);
extern void sibling_list_drop(znode * node);
extern void sibling_list_insert(znode * new, znode * before);
extern void sibling_list_insert_nolock(znode * new, znode * before);
extern void link_left_and_right(znode * left, znode * right);


struct tree_walk_actor {
	int (*process_znode)(znode* , void*);
	int (*process_extent)(const coord_t *, void *);
	int (*before)(void *);
};
extern int tree_walk(const reiser4_key *, struct tree_walk_actor *, void *opaque);

#endif				/* __FS_REISER4_TREE_WALK_H__ */

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
