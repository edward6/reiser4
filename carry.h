/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Functions and data types to "carry" tree modification(s) upward.
   See fs/reiser4/carry.c for details. */

#if !defined( __FS_REISER4_CARRY_H__ )
#define __FS_REISER4_CARRY_H__

#include "forward.h"
#include "debug.h"
#include "pool.h"
#include "znode.h"

#include <linux/types.h>

/* &carry_node - "location" of carry node.

   "location" of node that is involved or going to be involved into
   carry process. Node where operation will be carried to on the
   parent level cannot be recorded explicitly. Operation will be carried
   usually to the parent of some node (where changes are performed at
   the current level) or, to the left neighbor of its parent. But while
   modifications are performed at the current level, parent may
   change. So, we have to allow some indirection (or, positevly,
   flexibility) in locating carry nodes.

*/
typedef struct carry_node {
	/* pool linkage */
	reiser4_pool_header header;

	/* base node from which real_node is calculated. See
	    fs/reiser4/carry.c:lock_carry_node(). */
	znode *node;

	/* how to get ->real_node */
	/* to get ->real_node obtain parent of ->node*/
	__u32 parent:1;
	/* to get ->real_node obtain left neighbor of parent of
	    ->node*/
	__u32 left:1;
	__u32 left_before:1;

	/* locking */

	/* this node was locked by carry process and should be
	    unlocked when carry leaves a level */
	__u32 unlock:1;

	/* disk block for this node was allocated by carry process and
	    should be deallocated when carry leaves a level */
	__u32 deallocate:1;
	/* this carry node was allocated by carry process and should be
	    freed when carry leaves a level */
	__u32 free:1;

	/* type of lock we want to take on this node */
	lock_handle lock_handle;
} carry_node;

/* &carry_opcode - elementary operations that can be carried upward

   Operations that carry() can handle. This list is supposed to be
   expanded.

   Each carry operation (cop) is handled by appropriate function defined
   in fs/reiser4/carry.c. For example COP_INSERT is handled by
   fs/reiser4/carry.c:carry_insert() etc. These functions in turn
   call plugins of nodes affected by operation to modify nodes' content
   and to gather operations to be performed on the next level.

*/
typedef enum {
	/* insert new item into node. */
	COP_INSERT,
	/* delete pointer from parent node */
	COP_DELETE,
	/* remove part of or whole node. */
	COP_CUT,
	/* increase size of item. */
	COP_PASTE,
	/* insert extent (that is sequence of unformatted nodes). */
	COP_EXTENT,
	/* update delimiting key in least common ancestor of two
	   nodes. This is performed when items are moved between two
	   nodes.
	*/
	COP_UPDATE,
	/* update parent to reflect changes in the child. 3.x format
	   emulation uses this to update "child size" in parent. */
	COP_MODIFY,
	COP_INSERT_FLOW,
	COP_LAST_OP,
} carry_opcode;

#define CARRY_FLOW_NEW_NODES_LIMIT 4

typedef enum {
	COP_MODIFY_FREE_SPACE = (1 << 0),	/* FIXME_JMACD currently unused
						 * -josh */
} cop_modify_flag;

/* mode (or subtype) of COP_{INSERT|PASTE} operation. Specifies how target
   item is determined. */
typedef enum {
	/* target item is one containing pointer to the ->child node */
	COPT_CHILD,
	/* target item is given explicitly by @coord */
	COPT_ITEM_DATA,
	/* target item is given by key */
	COPT_KEY,
	/* see insert_paste_common() for more comments on this. */
	COPT_PASTE_RESTARTED,
} cop_insert_pos_type;

/* flags to cut and delete */
typedef enum {
	DELETE_RETAIN_EMPTY = (1 << 0),
	DELETE_DONT_COMPACT = (1 << 1),
	DELETE_KILL = (1 << 2)
} cop_delete_flag;

typedef enum {
	CARRY_TRACK_CHANGE = 1,
	CARRY_TRACK_NODE   = 2
} carry_track_type;

/* data supplied to COP_{INSERT|PASTE} by callers */
typedef struct carry_insert_data {
	/* position where new item is to be inserted */
	coord_t *coord;
	/* new item description */
	reiser4_item_data *data;
	/* key of new item */
	const reiser4_key *key;
} carry_insert_data;

/* data supplied to COP_CUT by callers */
typedef struct carry_cut_data {
	coord_t *from;
	coord_t *to;
	const reiser4_key *from_key;
	const reiser4_key *to_key;
	reiser4_key *smallest_removed;
	unsigned flags;
	void *iplug_params;
	struct inode *inode;
	lock_handle *left;
	lock_handle *right;
} carry_cut_data;

/* &carry_tree_op - operation to "carry" upward.

   Description of an operation we want to "carry" to the upper level of
   a tree: e.g, when we insert something and there is not enough space
   we allocate a new node and "carry" the operation of inserting a
   pointer to the new node to the upper level, on removal of empty node,
   we carry up operation of removing appropriate entry from parent.

   There are two types of carry ops: when adding or deleting node we
   node at the parent level where appropriate modification has to be
   performed is known in advance. When shifting items between nodes
   (split, merge), delimiting key should be changed in the least common
   parent of the nodes involved that is not known in advance.

   For the operations of the first type we store in &carry_op pointer to
   the &carry_node at the parent level. For the operation of the second
   type we store &carry_node or parents of the left and right nodes
   modified and keep track of them upward until they coincide.

*/
typedef struct carry_op {
	/* pool linkage */
	reiser4_pool_header header;
	carry_opcode op;
	/* node on which operation is to be performed:
	
	   for insert, paste: node where new item is to be inserted
	
	   for delete: node where pointer is to be deleted
	
	   for cut: node to cut from
	
	   for update: node where delimiting key is to be modified
	
	   for modify: parent of modified node
	
	*/
	carry_node *node;
	union {
		struct {
			/* (sub-)type of insertion/paste. Taken from
			   cop_insert_pos_type. */
			__u8 type;
			/* various operation flags. Taken from
			   cop_insert_flag. */
			__u8 flags;
			carry_insert_data *d;
			carry_node *child;
			znode *brother;
		} insert, paste, extent;
		carry_cut_data *cut;
		struct {
			carry_node *left;
		} update;
		struct {
			/* changed child */
			carry_node *child;
			/* bitmask of changes. See &cop_modify_flag */
			__u32 flag;
		} modify;
		struct {
			/* flags to deletion operation. Are taken from
			   cop_delete_flag */
			__u32 flags;
			/* child to delete from parent. If this is
			   NULL, delete op->node.  */
			carry_node *child;
		} delete;
		struct {
			flow_t *flow;
			coord_t *insert_point;
			reiser4_item_data *data;
			/* flow insertion is limited by number of new blocks
			   added in that operation which do not get any data
			   but part of flow. This limit is set by macro
			   CARRY_FLOW_NEW_NODES_LIMIT. This field stores number
			   of nodes added already during one carry_flow */
			int new_nodes;
		} insert_flow;
	} u;
} carry_op;

/* &carry_op_pool - preallocated pool of carry operations, and nodes */
typedef struct carry_pool {
	carry_op op[CARRIES_POOL_SIZE];
	reiser4_pool op_pool;
	carry_node node[NODES_LOCKED_POOL_SIZE];
	reiser4_pool node_pool;
} carry_pool;

/* &carry_tree_level - carry process on given level

   Description of balancing process on the given level.

   No need for locking here, as carry_tree_level is essentially per
   thread thing (for now).

*/
struct carry_level {
	/* this level may be restarted */
	__u32 restartable:1;
	/* list of carry nodes on this level, ordered by key order */
	pool_level_list_head nodes;
	pool_level_list_head ops;
	/* pool where new objects are allocated from */
	carry_pool *pool;
	int ops_num;
	int nodes_num;
	/* new root created on this level, if any */
	znode *new_root;
	/* This is set by caller (insert_by_key(), resize_item(), etc.) when
	   they want ->tracked to automagically wander to the node where
	   insertion point moved after insert or paste.
	*/
	carry_track_type track_type;
	/* lock handle supplied by user that we are tracking. See
	   above. */
	lock_handle *tracked;
#if REISER4_STATS
	tree_level level_no;
#endif
};

/* information carry passes to plugin methods that may add new operations to
   the @todo queue  */
struct carry_plugin_info {
	carry_level *doing;
	carry_level *todo;
};

int carry(carry_level * doing, carry_level * done);

carry_node *add_carry(carry_level * level, pool_ordering order, carry_node * reference);
carry_node *add_carry_skip(carry_level * level, pool_ordering order, carry_node * reference);
carry_op *add_op(carry_level * level, pool_ordering order, carry_op * reference);

extern carry_node *insert_carry_node(carry_level * doing,
				     carry_level * todo, const znode * node);

extern carry_node *add_carry_atplace(carry_level *doing,
				     carry_level *todo, znode *node);

extern carry_node *find_begetting_brother(carry_node * node, carry_level * kin);

extern void init_carry_pool(carry_pool * pool);
extern void done_carry_pool(carry_pool * pool);

extern void init_carry_level(carry_level * level, carry_pool * pool);

extern carry_op *post_carry(carry_level * level, carry_opcode op, znode * node, int apply_to_parent);
extern carry_op *node_post_carry(carry_plugin_info * info, carry_opcode op, znode * node, int apply_to_parent_p);

extern int carry_op_num(const carry_level * level);
extern int carry_node_num(const carry_level * level);

carry_node *add_new_znode(znode * brother, carry_node * reference, carry_level * doing, carry_level * todo);

carry_node *find_carry_node(carry_level * level, const znode * node);

extern znode *carry_real(const carry_node * node);

/* debugging function */

#if REISER4_DEBUG_OUTPUT
extern void print_carry(const char *prefix, carry_node * node);
extern void print_op(const char *prefix, carry_op * op);
extern void print_level(const char *prefix, carry_level * level);
#else
#define print_carry( p, n ) noop
#define print_op( p, o ) noop
#define print_level( p, l ) noop
#endif

/* __FS_REISER4_CARRY_H__ */
#endif

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
