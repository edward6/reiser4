/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Forward declarations. Thank you Kernighan.
 */

#if !defined( __REISER4_FORWARD_H__ )
#define __REISER4_FORWARD_H__

struct __reiser4_zlock;
typedef struct __reiser4_zlock reiser4_zlock;

struct __reiser4_lock_stack;
typedef struct __reiser4_lock_stack reiser4_lock_stack;

struct __reiser4_lock_handle;
typedef struct __reiser4_lock_handle reiser4_lock_handle;

struct znode;
typedef struct znode znode;

struct flow;
typedef struct flow flow_t;

struct tree_coord;
typedef struct tree_coord tree_coord;

struct item_coord;
typedef struct item_coord item_coord;

struct shift_params;
typedef struct shift_params shift_params;

struct reiser4_object_create_data;
typedef struct reiser4_object_create_data reiser4_object_create_data;

struct reiser4_plugin;
typedef struct reiser4_plugin reiser4_plugin;

struct reiser4_item_data;
typedef struct reiser4_item_data reiser4_item_data;

union reiser4_key;
typedef union reiser4_key reiser4_key;

union reiser4_disk_addr;
typedef union reiser4_disk_addr reiser4_disk_addr;

struct reiser4_tree;
typedef struct reiser4_tree reiser4_tree;

struct carry_tree_op;
typedef struct carry_tree_op carry_tree_op;

struct carry_tree_node;
typedef struct carry_tree_node carry_tree_node;

struct reiser4_journal;
typedef struct reiser4_journal reiser4_journal;

struct txn_atom;
typedef struct txn_atom txn_atom;

struct txn_handle;
typedef struct txn_handle txn_handle;

struct txn_mgr;
typedef struct txn_mgr txn_mgr;

struct reiser4_dir_entry_desc;
typedef struct reiser4_dir_entry_desc reiser4_dir_entry_desc;

struct reiser4_context;
typedef struct reiser4_context reiser4_context;

struct carry_level;
typedef struct carry_level carry_level;

typedef unsigned pos_in_node;
typedef unsigned pos_in_item;

struct jnode;
typedef struct jnode jnode;

struct slum_scan;
typedef struct slum_scan slum_scan;

struct inode;
struct page;
struct file;
struct dentry;
struct super_block;

/** return values of coord_by_key(). cbk == coord_by_key */
typedef enum { 
	CBK_COORD_FOUND    = 0,
	CBK_COORD_NOTFOUND = -ENOENT, 
	CBK_IO_ERROR       = -EIO,
	CBK_OOM            = -ENOMEM
} lookup_result;

/** results of lookup with directory file */
typedef enum { 
	FILE_NAME_FOUND     = 0, 
	FILE_NAME_NOTFOUND  = -ENOENT, 
	FILE_IO_ERROR       = -EIO,
	FILE_OOM            = -ENOMEM
} file_lookup_result;

/** behaviors of lookup. If coord we are looking for is actually in a tree,
    both coincide. */
typedef enum { 
	/** search exactly for the coord with key given */
	FIND_EXACT,
	/** search for coord with the maximal key not greater than one
	    given */
	FIND_MAX_NOT_MORE_THAN/*LEFT_SLANT_BIAS*/
} lookup_bias;

typedef enum {
	/**
	 * number of leaf level of the tree
	 * The fake root has (tree_level=0).
	 */
	LEAF_LEVEL = 1,

	/**
	 * number of level one above leaf level of the tree: a #define because
	 * LEAF_LEVEL is, thought not used in per-level arrays.
	 *
	 * It is supposed that internal tree used by reiser4 to store file
	 * system data and meta data will have height 2 initially (when
	 * created by mkfs).
	 */
	TWIG_LEVEL = 2,
} tree_level;

/* The "real" maximum ztree height is the 0-origin size of any per-level
 * array, since the zero'th level is not used. */
#define REAL_MAX_ZTREE_HEIGHT     (REISER4_MAX_ZTREE_HEIGHT-LEAF_LEVEL)

/** enumeration of possible mutual position of item and coord.  This enum is
    return type of ->is_in_item() item plugin method which see. */
typedef enum {
	/** coord is on the left of an item*/
	IP_ON_THE_LEFT,
	/** coord is inside item */
	IP_INSIDE,
	/** coord is inside item, but to the right of the rightmost unit of
	    this item */
	IP_RIGHT_EDGE,
	/** coord is on the right of an item */
	IP_ON_THE_RIGHT
} interposition;

/** type of lock to acquire on znode before returning it to caller */
typedef enum {
	ZNODE_READ_LOCK      = 1,
	ZNODE_WRITE_LOCK     = 2,
	ZNODE_WRITE_IF_DIRTY = 3,
} znode_lock_mode;

/** type of lock request */
typedef enum {
	ZNODE_LOCK_LOPRI    = 0,
	ZNODE_LOCK_HIPRI    = (1 << 0),

	/* By setting the ZNODE_LOCK_NONBLOCK flag in a lock request the call to reiser4_lock_znode will not sleep
	 * waiting for the lock to become available.  If the lock is unavailable, reiser4_znode_lock will immediately
	 * return the value -EAGAIN.
	 */
	ZNODE_LOCK_NONBLOCK = (1 << 1),
} znode_lock_request;

typedef enum { READ_OP = 0, WRITE_OP = 1 } rw_op;

/* used to specify direction of shift. These must be -1 and 1 */
typedef enum {
	SHIFT_LEFT = 1,
	SHIFT_RIGHT = -1
} shift_direction;

typedef enum {
	LEFT_SIDE,
	RIGHT_SIDE
} sideof;

#define round_up( value, order )						\
	( ( typeof( value ) )( ( ( long ) ( value ) + ( order ) - 1U ) &	\
			     ~( ( order ) - 1 ) ) )

/*
 * values returned by allocate_and_squeeze_right_neighbor
 */
typedef enum {
	/*
	 * unit of internal item is moved
	 */
	SUBTREE_MOVED = -1,
	/*
	 * nothing else can be squeezed into left neighbor
	 */
	SQUEEZE_DONE = 2,
	/*
	 * all content of node is squeezed into its left neighbor
	 */
	SQUEEZE_CONTINUE = 3
} squeeze_result;

struct name {
};
typedef struct name name_t;


/* __REISER4_FORWARD_H__ */
#endif


/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */

