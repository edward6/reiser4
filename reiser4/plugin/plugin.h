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

separate file and directory plugins
 
*/

typedef struct fplug {

/* reiser4 required file operations */

	/* note that keys should have 0 for their offsets */
	int (*write_flow)(flow * flow);
	int (*read_flow)(flow * flow);
	
/* VFS required/defined operations */
	int ( *reiser4_truncate )( struct inode *inode, loff_t size );
	/* create new object described by @data and add it to the @parent directory under the name described by
	   @dentry */
	int ( *reiser4_create )( struct inode *parent, struct dentry *dentry, 
			       reiser4_object_create_data *data );
	/** save inode cached stat-data onto disk. It was called
	    reiserfs_update_sd() in 3.x */
	int ( *reiser4_write_inode)( struct inode *inode );
	int ( *reiser4_readpage )( struct file *file, struct page * );
	
/* sub-methods: These are optional.  If used they will allow you to minimize the amount of code needed to implement a
	   deviation from some other method that uses them.  You could logically argue that they should be a separate
	   type of plugin. */
	/**
	 * Construct flow into @f according to user-supplied data.
	 */
	int ( *flow_by_inode )( struct file *file, char *buf, size_t size, 
			     loff_t *off, rw_op op, flow *f );
	int (*flow_by_key)(key key, flow * flow);
	/* set the plugin for a file.  Called during file creation in reiser4() and creat(). */
	int (*set_plug_in_sd)(plug_type plug_type, key key_of_sd);
	/* set the plugin for a file.  Called during file creation in creat() but not reiser4() unless an inode already exists
	   for the file. */
	int (*set_plug_in_inode)(plug_type plug_type, struct inode *inode);
	int (*create_blank_sd)(key key);
	/** 
	 * delete this object's stat-data if REISER4_NO_STAT_DATA is cleared
	 * and set REISER4_NO_STAT_DATA 
	 *
	 */
	int ( *destroy_stat_data )( struct inode *object, struct inode *parent );
	/** bump reference counter on "object" */
	int ( *add_link )( struct inode *object );

	/** decrease reference counter on "object" */
	int ( *rem_link )( struct inode *object );

	/** return true if item addressed by @coord belongs to @inode.
	    This is used by read/write to properly slice flow into items
	    in presence of multiple key assignment policies, because
	    items of a file are not necessarily contiguous in a key space,
	    for example, in a plan-b. */
	int ( *owns_item )( const struct inode *inode,
			    const tree_coord *coord );

	int ( *can_add_link )( key key );





} fplug;

typedef struct dplug {

	/* returns whether it is a builtin */
	int (*is_built_in)(char * name, int length);

/* VFS required/defined operations below this line */
	int ( *unlink )( struct inode *parent, struct dentry *victim );
	int ( *link )( struct inode *parent, struct dentry *existing, 
		       struct dentry *where );
	/** lookup for "name" within this object and return its key in
	    "key". If this is not implemented (set to NULL),
	    reiser4_lookup will return -ENOTDIR 

	should be made to be more precisely VFS lookup -Hans 

	*/
	file_lookup_result ( *lookup )( struct inode *inode, 
					const struct qstr *name,
					reiser4_key *key, reiser4_entry *entry );
/* sub-methods: These are optional.  If used they will allow you to minimize the amount of code needed to implement a
	   deviation from some other method that uses them.  You could logically argue that they should be a separate
	   type of plugin. */

	/** check whether "name" is acceptable name to be inserted into
	    this object. Optionally implemented by directory-like objects.
	    Can check for maximal length, reserved symbols etc */
	int ( *is_name_acceptable )( const struct inode *inode, 
				     const char *name, int len );

	int ( *add_entry )( struct inode *object, struct dentry *where, 
			    reiser4_object_create_data *data, 
			    reiser4_entry *entry );

	int ( *rem_entry )( struct inode *object, 
			    struct dentry *where, reiser4_entry *entry );



} dplug;

typedef struct reiser4_file_plugin {

/* these that remain below need to be discussed with Nikita on monday. */

	/** inherit plugin properties from parent object and from top
	    object. Latter is particulary required in a case when parent
	    object is unaccessible like when knfsd asks for inode in the
	    mid-air. This is called on object creation. */
	int ( *inherit )( struct inode *inode, 
			  struct inode *parent, struct inode *root );

	/**
	 * read from/write on object
	 */
	ssize_t ( *io )( struct file *file, char *buf, size_t size, 
			 loff_t *off, rw_op op );

	/** read-write methods */
	rw_f_type rw_f[ WRITE_OP + 1 ];

	/** return pointer to plugin of new item that should be inserted
	    into body of @inode at position determined by @key. This is
	    called by write() when it has to insert new item into
	    file. */
	int ( *item_plugin_at )( const struct inode *inode, 
				 const reiser4_key *key );

	int ( *find_item )( reiser4_tree *tree, reiser4_key *key,
			    tree_coord *coord, reiser4_lock_handle *lh );
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
	/* add here something for non-standard objects you invent, like
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
