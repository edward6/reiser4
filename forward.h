/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Forward declarations. Thank you Kernighan.
 */

#if !defined( __REISER4_FORWARD_H__ )
#define __REISER4_FORWARD_H__

typedef struct zlock zlock;
typedef struct lock_stack lock_stack;
typedef struct lock_handle lock_handle;
typedef struct znode znode;
typedef struct flow flow_t;
typedef struct new_coord new_coord;
typedef struct item_coord item_coord;
typedef struct shift_params shift_params;
typedef struct reiser4_object_create_data reiser4_object_create_data;
typedef union  reiser4_plugin reiser4_plugin;
typedef struct item_plugin item_plugin;
typedef struct reiser4_item_data reiser4_item_data;
typedef union  reiser4_key reiser4_key;
typedef union  reiser4_dblock_nr reiser4_dblock_nr;
typedef struct reiser4_tree reiser4_tree;
typedef struct carry_tree_op carry_tree_op;
typedef struct carry_tree_node carry_tree_node;
typedef struct reiser4_journal reiser4_journal;
typedef struct txn_atom txn_atom;
typedef struct txn_handle txn_handle;
typedef struct txn_mgr txn_mgr;
typedef struct reiser4_dir_entry_desc reiser4_dir_entry_desc;
typedef struct reiser4_context reiser4_context;
typedef struct carry_level carry_level;
typedef struct blocknr_set blocknr_set;
typedef struct blocknr_set_entry blocknr_set_entry;
/* super_block->u.generic_sbp points to this */
typedef struct reiser4_super_info_data reiser4_super_info_data;
/*next two objects are fields of reiser4_super_info_data */
typedef struct reiser4_oid_allocator reiser4_oid_allocator;
typedef struct reiser4_space_allocator reiser4_space_allocator;

typedef unsigned pos_in_node;
typedef unsigned pos_in_item;

typedef struct jnode jnode;
typedef struct reiser4_blocknr_hint reiser4_blocknr_hint;

struct inode;
struct page;
struct file;
struct dentry;
struct super_block;

/** return values of coord_by_key(). cbk == coord_by_key */
typedef enum { 
	CBK_COORD_FOUND    = 0,
	CBK_COORD_NOTFOUND = -ENOENT, 
	CBK_IO_ERROR       = -EIO, /* FIXME: it seems silly to have special OOM, IO_ERROR return codes for each search. */
	CBK_OOM            = -ENOMEM /* FIXME: it seems silly to have special OOM, IO_ERROR return codes for each search. */
} lookup_result;

/** results of lookup with directory file */
typedef enum { 
	FILE_NAME_FOUND     = 0, 
	FILE_NAME_NOTFOUND  = -ENOENT, 
	FILE_IO_ERROR       = -EIO, /* FIXME: it seems silly to have special OOM, IO_ERROR return codes for each search. */
	FILE_OOM            = -ENOMEM /* FIXME: it seems silly to have special OOM, IO_ERROR return codes for each search. */
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

	/* By setting the ZNODE_LOCK_NONBLOCK flag in a lock request the call to longterm_lock_znode will not sleep
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
 * values returned by squalloc_right_neighbor and its auxiliary functions
 */
typedef enum {
	/*
	 * unit of internal item is moved
	 */
	SUBTREE_MOVED = 0,
	/*
	 * nothing else can be squeezed into left neighbor
	 */
	SQUEEZE_TARGET_FULL = 1,
	/*
	 * all content of node is squeezed into its left neighbor
	 */
	SQUEEZE_SOURCE_EMPTY = 2,
	/*
	 * one more item is copied (this is only returned by
	 * allocate_and_copy_extent to squalloc_twig))
	 */
	SQUEEZE_CONTINUE = 3
} squeeze_result;

struct name {
};
typedef struct name name_t;

typedef union lnode lnode;

typedef enum { 
	STATIC_STAT_DATA_ID,
	SIMPLE_DIR_ENTRY_ID,
	COMPOUND_DIR_ID,
	NODE_POINTER_ID,
	ACL_ID,
	EXTENT_POINTER_ID, 
	TAIL_ID, 
	LAST_ITEM_ID 
} item_id;

/* Flags passed to jnode_flush() to allow it to distinguish default settings based on
 * whether commit() was called or VM memory pressure was applied. */
typedef enum {
	JNODE_FLUSH_COMMIT = 1,
	JNODE_FLUSH_MEMORY = 2,
} jnode_flush_flags;

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

