/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Forward declarations. Thank you Kernighan.
 */

#if !defined( __REISER4_FORWARD_H__ )
#define __REISER4_FORWARD_H__

struct __reiser4_lock_handle;
typedef struct __reiser4_lock_handle reiser4_lock_handle;
struct znode;
typedef struct znode znode;

struct flow;
typedef struct flow flow;

struct tree_coord;
typedef struct tree_coord tree_coord;

struct item_coord;
typedef struct item_coord item_coord;

struct unit_coord;
typedef struct unit_coord unit_coord;

struct shift_params;
typedef struct shift_params shift_params;

struct reiser4_object_create_data;
typedef struct reiser4_object_create_data reiser4_object_create_data;

struct reiser4_plugin;
typedef struct reiser4_plugin reiser4_plugin;

struct node;
typedef struct node reiser4_node_plugin;

struct item_ops;
typedef struct item_ops reiser4_item_plugin;

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

struct reiser4_entry;
typedef struct reiser4_entry reiser4_entry;

struct reiser4_context;
typedef struct reiser4_context reiser4_context;

struct carry_level;
typedef struct carry_level carry_level;

typedef unsigned pos_in_node;
typedef unsigned pos_in_item;

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

typedef unsigned tree_level;

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
	ZNODE_READ_LOCK     = 1,
	ZNODE_WRITE_LOCK    = 2,
} znode_lock_mode;

/** type of lock request */
typedef enum {
	ZNODE_LOCK_LOPRI    = 0,
	ZNODE_LOCK_HIPRI    = (1 << 0),
	ZNODE_LOCK_NONBLOCK = (1 << 1),	/* my time is getting consumed by commenting poorly commented code.  I don't
					   like this.  I expect you to comment all your code thoroughly.  Don't write
					   more code, comment what you have written so that I can stop spending my time
					   commenting it for you.  What does nonblock mean? -Hans */
} znode_lock_request;

/* used to specify direction of shift. These must be -1 and 1 */
typedef enum {
	SHIFT_APPEND = 1,
	SHIFT_PREPEND = -1
} shift_direction;


#define round_up( value, order )						\
	( ( typeof( value ) )( ( ( long ) ( value ) + ( order ) - 1U ) &	\
			     ~( ( order ) - 1 ) ) )

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

