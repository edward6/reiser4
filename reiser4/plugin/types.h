/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Basic plugin data-types.
 * see fs/reiser4/plugin/plugin.c for details
 */

#if !defined( __FS_REISER4_PLUGIN_TYPES_H__ )
#define __FS_REISER4_PLUGIN_TYPES_H__


/* plugin data-types and constants */

typedef enum {
	REISER4_FILE_PLUGIN_ID,
	REISER4_ITEM_PLUGIN_ID,
	REISER4_NODE_PLUGIN_ID,
	REISER4_HASH_PLUGIN_ID,
	REISER4_TAIL_PLUGIN_ID,
	REISER4_HOOK_PLUGIN_ID,
	REISER4_PERM_PLUGIN_ID,
	REISER4_SD_EXT_PLUGIN_ID,
	REISER4_PLUGIN_TYPES
} reiser4_plugin_type;

typedef int reiser4_plugin_id;

struct reiser4_plugin_operations;
/** generic plugin operations, supported by each 
    plugin type. */
typedef struct reiser4_plugin_ops reiser4_plugin_ops;

struct reiser4_plugin_ref;
typedef struct reiser4_plugin_ref reiser4_plugin_ref;

TS_LIST_DECLARE( plugin );

/** common part of each plugin instance. */
typedef struct reiser4_plugin_header {
	/** record len. Set this to sizeof( reiser4_plugin_header ).
	    Used to check for stale dynamic plugins. */
	int                  rec_len;
	/** plugin type */
	reiser4_plugin_type  type_id;
	/** id of this plugin */
	reiser4_plugin_id    id;
	reiser4_plugin_ops  *pops;
	/** short label of this plugin */
	const char          *label;
	/** descriptive string. Put your copyright message here. */
	const char          *desc;
	plugin_list_link     linkage;
} reiser4_plugin_header;

/** VFS operations */
typedef enum {
	/* inode ops */
	VFS_CREATE_OP, VFS_LINK_OP, VFS_UNLINK_OP, VFS_SYMLINK_OP,
	VFS_MKDIR_OP, VFS_RMDIR_OP, VFS_MKNOD_OP, VFS_RENAME_OP,
	VFS_READLINK_OP, VFS_FOLLOW_LINK_OP, VFS_TRUNCATE_OP,
	VFS_PERMISSION_OP, VFS_REVALIDATE_OP, VFS_SETATTR_OP,
	VFS_GETATTR_OP,

	/* file ops */
	VFS_LLSEEK_OP, VFS_READ_OP, VFS_WRITE_OP, VFS_READDIR_OP,
	VFS_POLL_OP, VFS_IOCTL_OP, VFS_MMAP_OP, VFS_OPEN_OP, VFS_FLUSH_OP,
	VFS_RELEASE_OP, VFS_FSYNC_OP, VFS_FASYNC_OP, VFS_LOCK_OP, VFS_READV_OP,
	VFS_WRITEV_OP, VFS_SENDPAGE_OP, VFS_GET_UNMAPPED_AREA_OP,

	/* as ops */
	VFS_WRITEPAGE_OP, VFS_READPAGE_OP, VFS_SYNC_PAGE_OP,
	VFS_PREPARE_WRITE_OP, VFS_COMMIT_WRITE_OP, VFS_BMAP_OP
} vfs_op;

typedef enum { READ_OP = 0, WRITE_OP = 1 } rw_op;

typedef ssize_t ( *rw_f_type )( struct file *file, flow *a_flow, loff_t *off );

/** entry in file object */
struct reiser4_entry {
	/*
	 * key of directory entry
	 */
	reiser4_key   key;
	/*
	 * inode of object bound by this entry
	 */
	struct inode *obj;
};

/** file (object) plugin.  Defines the set of methods that file plugins implement, some of which are optional.  This includes all of the file related VFS operations.  
*/
typedef struct reiser4_file_plugin {

	/** install given plugin on fresh object. Initialise inode fields.
	    This must set REISER4_NO_STAT_DATA bit in our inode flags. 
	    This is called on object creation
	    (create_object()).*/
	int ( *install )( struct inode *inode, reiser4_plugin *plug,
			  struct inode *parent, 
			  reiser4_object_create_data *data );

	/** setup given plugin on already initialised inode. This is
	    done during reading inode from disk. */
	int ( *activate )( struct inode *inode, reiser4_plugin *plug );

	/** save stat-data (inode) onto disk. It was called
	    reiserfs_update_sd() in 3.x */
	int ( *save_sd )( struct inode *inode );

	/**
	 * methods in ->estimate sub-structure determine how much space call
	 * to the appropriate plugin method will consume in the journal log.
	 *
	 * Such estimation need not to be exact, but it has to be
	 * conservative.
	 *
	 */
	struct estimate_how_much_to_reserve_in_transaction_for_op {
		/** 
		 * how much space to reserve in transcrash to do io (read or
		 * write) on this file. Generic function will call this and
		 * start transcrash, it should also keep track of nested
		 * transcrashes.  ->rw() should provide conservative
		 * estimation. Actual amount of pages supplied to journal by
		 * operation can be less, because of overwrite etc. If
		 * reasonable estimation is impossible, return 0 here and do
		 * manual transcrash handling in the low-level method.
		 */
		int ( *rw )( struct file *file, char *buf, size_t size, 
			     loff_t *off, rw_op op );

		/** how much space to reserve in transcrash to create new
		    entry in this object */
		int ( *add )( const struct inode *object, struct dentry *dentry,
			      reiser4_object_create_data *data );

		/** how much space to reserve in transcrash to remove entry in
		    this object */
		int ( *rem )( const struct inode *object, struct dentry *dentry );
		/** how much space to reserve in transcrash to create new
		    object of this type */
		int ( *create )( reiser4_object_create_data *data );

		/** 
		 * how much space to reserve in transcrash to delete empty
		 * (fresh) object of this type
		 *
		 */
		int ( *destroy )( const struct inode *inode );

		/** 
		 * how much space to reserve in transcrash to add link to this
		 * object.
		 */
		int ( *add_link )( const struct inode *inode );

		/** how much space to reserve in transcrash to remvoe link
		    from this object */
		int ( *rem_link )( const struct inode *inode );

		/** how much space to reserve in transcrash to update object's
		    stat-data */
		int ( *save )( const struct inode *inode );
	} estimate;

	int ( *create_child )( struct inode *parent, struct dentry *dentry, 
			       reiser4_object_create_data *data );
	int ( *unlink )( struct inode *parent, struct dentry *victim );
	int ( *link )( struct inode *parent, struct dentry *existing, 
		       struct dentry *where );
	/** create stat-data for this object and clear 
	    REISER4_NO_STAT_DATA in our inode flags. */
	int ( *create )( struct inode *object, struct inode *parent, 
			 reiser4_object_create_data *data );

	/** 
	 * delete this object's stat-data if REISER4_NO_STAT_DATA is cleared
	 * and set REISER4_NO_STAT_DATA 
	 *
	 */
	int ( *destroy )( struct inode *object, struct inode *parent );

	int ( *add_entry )( struct inode *object, struct dentry *where, 
			    reiser4_object_create_data *data, 
			    reiser4_entry *entry );

	int ( *rem_entry )( struct inode *object, 
			    struct dentry *where, reiser4_entry *entry );

	/** bump reference counter on "object" */
	int ( *add_link )( struct inode *object );

	/** decrease reference counter on "object" */
	int ( *rem_link )( struct inode *object );

	int ( *can_add_link )( const struct inode *object );

	/** inherit plugin properties from parent object and from top
	    object. Latter is particulary required in a case when parent
	    object is unaccessible like when knfsd asks for inode in the
	    mid-air. This is called on object creation. */
	int ( *inherit )( struct inode *inode, 
			  struct inode *parent, struct inode *root );

	int ( *is_empty )( const struct inode *inode );

	/**
	 * read from/write on object
	 */
	ssize_t ( *io )( struct file *file, char *buf, size_t size, 
			 loff_t *off, rw_op op );

	/** read-write methods */
	rw_f_type rw_f[ WRITE_OP + 1 ];

	/**
	 * Construct flow into @f according to user-supplied data.
	 */
	int ( *build_flow )( struct file *file, char *buf, size_t size, 
			     loff_t *off, rw_op op, flow *f );

	/** check whether "name" is acceptable name to be inserted into
	    this object. Optionally implemented by directory-like objects.
	    Can check for maximal length, reserved symbols etc */
	int ( *is_name_acceptable )( const struct inode *inode, 
				     const char *name, int len );

	/** lookup for "name" within this object and return its key in
	    "key". If this is not implemented (set to NULL),
	    reiser4_lookup will return -ENOTDIR */
	file_lookup_result ( *lookup )( struct inode *inode, 
					const struct qstr *name,
					reiser4_key *key, reiser4_entry *entry );

	/** return true if item addressed by @coord belongs to @inode.
	    This is used by read/write to properly slice flow into items
	    in presence of multiple key assignment policies, because
	    items of a file are not necessarily contiguous in a key space,
	    for example, in a plan-b. */
	int ( *owns_item )( const struct inode *inode,
			    const tree_coord *coord );
	/** return pointer to plugin of new item that should be inserted
	    into body of @inode at position determined by @key. This is
	    called by write() when it has to insert new item into
	    file. */
	int ( *item_plugin_at )( const struct inode *inode, 
				 const reiser4_key *key );

	int ( *truncate )( struct inode *inode, loff_t size );
	int ( *find_item )( reiser4_tree *tree, reiser4_key *key,
			    tree_coord *coord, reiser4_lock_handle *lh );
	int ( *readpage )( struct file *file, struct page * );
} reiser4_file_plugin;

typedef struct reiser4_tail_plugin {
	/** returns non-zero iff file's tail has to be stored
	    in a direct item. */
	int ( *tail )( const struct inode *inode, loff_t size );
} reiser4_tail_plugin;

typedef struct reiser4_hash_plugin {
	/** computes hash of the given name */
	__u64 ( *hash ) ( const unsigned char *name, int len );
} reiser4_hash_plugin;

/* hook plugins exist for debugging only? */
typedef struct reiser4_hook_plugin {
	/** abstract hook function */
	int ( *hook ) ( struct super_block *super, ... );
} reiser4_hook_plugin;

typedef struct reiser4_perm_plugin {
	/** check permissions for read/write */
	int ( *rw_ok )( struct file *file, const char *buf, 
			size_t size, loff_t *off, rw_op op );

	/** check permissions for lookup */
	int ( *lookup_ok )( struct inode *parent, struct dentry *dentry );

	/** check permissions for create */
	int ( *create_ok )( struct inode *parent, struct dentry *dentry, 
			    reiser4_object_create_data *data );

	/** check permissions for linking @where to @existing */
	int ( *link_ok )( struct dentry *existing, struct inode *parent, 
			  struct dentry *where );
	/** check permissions for unlinking @victim from @parent */
	int ( *unlink_ok )( struct inode *parent, struct dentry *victim );
	/** check permissions from deletion of @object whose last
	    reference is in in @parent */
	int ( *delete_ok )( struct inode *parent, struct dentry *victim );
} reiser4_perm_plugin;

/** call ->check_ok method of perm plugin for inode */
#define perm_chk( inode, check, args... )			\
({								\
	reiser4_plugin *perm;					\
								\
	perm = reiser4_get_object_state( inode ) -> perm;	\
	( ( perm != NULL ) &&					\
	  ( perm -> u.perm. ## check ## _ok != NULL ) &&	\
	    perm -> u.perm. ## check ## _ok( ##args ) );	\
})

typedef struct reiser4_sd_ext_plugin {
	int ( *present ) ( struct inode *inode, char **area, int *len );
	int ( *absent ) ( struct inode *inode );
	int ( *save_len ) ( struct inode *inode );
	int ( *save ) ( struct inode *inode, char **area );
	/** alignment requirement for this stat-data part */
	int alignment;
} reiser4_sd_ext_plugin;

/** plugin instance. 
    We keep everything inside single union for simplicity.
    Alternative solution is to to keep size of actual plugin
    in plugin type description. */
struct reiser4_plugin {
	/** generic fields */
	reiser4_plugin_header h;
	/** data specific to particular plugin type */
	union __plugins {
		reiser4_file_plugin   file;
		reiser4_hash_plugin   hash;
		reiser4_tail_plugin   tail;
		reiser4_hook_plugin   hook;
		reiser4_perm_plugin   perm;
		reiser4_node_plugin   node;
		reiser4_item_plugin   item;
		reiser4_sd_ext_plugin sd_ext;
		void                *generic;
	} u;
};

typedef enum { 
	REISER4_LIGHT_WEIGHT_INODE = 0x1,
	REISER4_NO_STAT_DATA       = 0x2,
	/** internal immutable flag. Currently is only used
	    to avoid race condition during file creation.
	    See comment in create_object(). */
	REISER4_IMMUTABLE          = 0x4,
	/** inode was read from storage */
	REISER4_LOADED             = 0x8,
} reiser4_file_plugin_flags;

/** intra-syscall ra hint. Use it when you are going to do
    several operations in a row */
typedef enum { AHEAD_RA, BEHIND_RA, NO_RA } intra_syscall_ra_hint;

/** inter-syscall ra hint structure. We can store such thing into inode.
    Properly it should be associated with struct file rather than with
    struct inode, but there is no file-system specific part in struct
    file, or we can store ra_hints hashed by file-struct address and
    track them in ->release() method. insert/delete tree operations
    consult this structure and update it. We can add user interface to
    this later.  This will help as to handle cross-syscall repeatable
    insertions efficiently */
typedef struct inter_syscall_ra_hint {
	/* some fields should go here.
	   Start by looking at include/linux/fs.h:struct file */
} inter_syscall_ra_hint;

/** state associated with each inode. Stored in each inode. 
    This is basically reiser4-specific part of inode 

*/
struct reiser4_plugin_ref {
	/** plugin of file itself */
	reiser4_plugin            *file;
	/** perm plugin for this file */
	reiser4_plugin            *perm;
	/** tail policy plugin. Only meaningful for regular files */
	reiser4_plugin            *tail;
	/** hash plugin. Only meaningful for directory files */
	reiser4_plugin            *hash;
	/** plugin of stat-data */
	reiser4_plugin            *sd;
	/** reiser4-specific inode flags. They are "transient" and 
	    are not supposed to be stored on a disk. Used to trace
	    "state" of inode. Bitmasks for this field are defined in 
	    fs/reiser4/plugin/types.h:reiser4_file_plugin_flags */
	__u32                      flags;
	/** bytes actually used by the file */
	__u64                      bytes;
	__u64                      extmask;
	/** length of stat-data for this inode */
	short                      sd_len;
	/** bitmask of non-standard plugins for this inode */
	__u16                      plugin_mask;
	inter_syscall_ra_hint      ra;
	/** locality id for this file */
	oid_t                      locality_id;
};

struct reiser4_plugin_ops {
	/** load given plugin from disk */
	int ( *load )( struct inode *inode, reiser4_plugin *plugin,
		       char **area, int *len );
	/** how many space is required to store this plugin's state
	    in stat-data */
	int ( *save_len )( struct inode *inode, reiser4_plugin *plugin );
	/** save persistent plugin-data to disk */
	int ( *save )( struct inode *inode, reiser4_plugin *plugin, 
		       char **area );
	/** alignment requirement for on-disk state of this plugin
	    in number of bytes */
	int alignment;
	/** install itself into given inode. This can return error
	    (e.g., you cannot change hash of non-empty directory). */
	int ( *change )( struct inode *inode, reiser4_plugin *plugin );
};


/* functions implemented in fs/reiser4/plugin/base.c */

/** stores plugin reference in reiser4-specific part of inode */
extern int reiser4_set_object_plugin( struct inode *inode, reiser4_plugin_id id );
extern int reiser4_handle_default_plugin_option( char *option, 
						 reiser4_plugin **area );
extern int reiser4_setup_plugins( struct super_block *super, 
				  reiser4_plugin **area );
extern reiser4_plugin *lookup_plugin( char *type_label, char *plug_label );
extern int inherit_if_nil( reiser4_plugin **to, reiser4_plugin **from );
extern int init_plugins( void );

/* builtin plugins */

/* builtin file-plugins */
typedef enum { REGULAR_FILE_PLUGIN_ID, DIRECTORY_FILE_PLUGIN_ID,
	       SYMLINK_FILE_PLUGIN_ID,
	       /* special_fobj_id is for objects completely handled
		  by VFS: fifos, devices, sockets */
	       SPECIAL_FILE_PLUGIN_ID,
	       LAST_FILE_PLUGIN_ID
} reiser4_file_id;
/* defined in fs/reiser4/plugin/object.c */
extern reiser4_plugin file_plugins[ LAST_FILE_PLUGIN_ID ];

/** data type used to pack parameters that we pass to generic
    object creation function create_object() */
struct reiser4_object_create_data {
	/** plugin to control created object */
	reiser4_file_id id;
	/** mode of regular file, directory or special file */
	int         mode;
	/** rdev of special file */
	int         rdev;
	/** symlink target */
	const char *name;
	/* add here something for non-stanard objects you invent, like
	   query for interpolation file etc. */
};

/* builtin hash-plugins */

typedef enum { 
	RUPASOV_HASH_ID, 
	R5_HASH_ID, 
	TEA_HASH_ID,
	FNV1_HASH_ID,
	DEGENERATE_HASH_ID,
	LAST_HASH_ID
} reiser4_hash_id;

/* builtin tail-plugins */

typedef enum { 
	NEVER_TAIL_ID, 
	SUPPRESS_OLD_ID,
	FOURK_TAIL_ID, 
	ALWAYS_TAIL_ID,
	LAST_TAIL_ID
} reiser4_tail_id;

typedef enum { DUMP_HOOK_ID } reiser4_hook_id;

typedef enum { RWX_PERM_ID } reiser4_perm_id;

#define MAX_PLUGIN_TYPE_LABEL_LEN  32
#define MAX_PLUGIN_PLUG_LABEL_LEN  32

/** used for interface with user-land: table-driven parsing in
    reiser4(). */
typedef struct plugin_locator {
	reiser4_plugin_type type_id;
	reiser4_plugin_id   id;
	char         type_label[ MAX_PLUGIN_TYPE_LABEL_LEN ];
	char         plug_label[ MAX_PLUGIN_PLUG_LABEL_LEN ];
} plugin_locator;

extern int locate_plugin( struct inode *inode, plugin_locator *loc );
extern reiser4_plugin *plugin_by_id( reiser4_plugin_type type_id, 
				     reiser4_plugin_id id );
extern reiser4_plugin *plugin_by_unsafe_id( reiser4_plugin_type type_id, 
					    reiser4_plugin_id id );
extern reiser4_plugin *plugin_by_disk_id( reiser4_tree *tree, 
					reiser4_plugin_type type_id, d16 *did );
extern int save_plugin_id( reiser4_plugin *plugin, d16 *area );

extern void print_plugin( const char *prefix, reiser4_plugin *plugin );

TS_LIST_DEFINE( plugin, reiser4_plugin, h.linkage );

extern plugin_list_head *get_plugin_list( reiser4_plugin_type type_id );

#define for_all_plugins( ptype, plugin )			\
for( plugin = plugin_list_front( get_plugin_list( ptype ) ) ;	\
     ! plugin_list_end( get_plugin_list( ptype ), plugin ) ;	\
     plugin = plugin_list_next( plugin ) )

/* __FS_REISER4_PLUGIN_TYPES_H__ */
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
