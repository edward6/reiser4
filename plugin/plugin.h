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
	REISER4_FILE_PLUGIN_TYPE,
	REISER4_DIR_PLUGIN_TYPE,
	REISER4_ITEM_PLUGIN_TYPE,
	REISER4_NODE_PLUGIN_TYPE,
	REISER4_HASH_PLUGIN_TYPE,
	REISER4_TAIL_PLUGIN_TYPE,
	REISER4_HOOK_PLUGIN_TYPE,
	REISER4_PERM_PLUGIN_TYPE,
	REISER4_SD_EXT_PLUGIN_TYPE,
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
typedef struct plugin_header {
	/** plugin type */
	reiser4_plugin_type  type_id;
	/** id of this plugin */
	reiser4_plugin_id    id;
	/** plugin operations */
	reiser4_plugin_ops  *pops;
	/** short label of this plugin */
	const char          *label;
	/** descriptive string. Put your copyright message here. */
	const char          *desc;
	/** list linkage */
	plugin_list_link     linkage;
} plugin_header;

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

typedef ssize_t ( *rw_f_type )( struct file *file, flow_t *a_flow, loff_t *off );


/**

 File plugin.  Defines the set of methods that file plugins implement, some of which are optional.  

 A file plugin offers to the caller an interface for IO ( writing to and/or reading from) to what the caller sees as one
 sequence of bytes.  An IO to it may affect more than one physical sequence of bytes, or no physical sequence of bytes,
 it may affect sequences of bytes offered by other file plugins to the semantic layer, and the file plugin may invoke
 other plugins and delegate work to them, but its interface is structured for offering the caller the ability to read
 and/or write what the caller sees as being a single sequence of bytes.

 The file plugin must present a sequence of bytes to the caller, but it does not necessarily have to store a sequence of
 bytes, it does not necessarily have to support efficient tree traversal to any offset in the sequence of bytes (tail
 and extent items, whose keys contain offsets, do however provide efficient non-sequential lookup of any offset in the
 sequence of bytes).

 Directory plugins provide methods for selecting file plugins by resolving a name for them.  

 The functionality other filesystems call an attribute, and rigidly tie together, we decompose into orthogonal
 selectable features of files.  Using the terminology we will define next, an attribute is a perhaps constrained,
 perhaps static length, file whose parent has a uni-count-intra-link to it, which might be grandparent-major-packed, and
 whose parent has a deletion method that deletes it.

 File plugins implement constraints. 

 Files can be of variable length (e.g. regular unix files), or of static length (e.g. static sized attributes).

 An object may have many sequences of bytes, and many file plugins, but, it has exactly one objectid.  It is usually
 desirable that an object has a deletion method which deletes every item with that objectid.  Items cannot in general be
 found by just their objectids.  This means that an object must have either a method built into its deletion plugin
 method for knowing what items need to be deleted, or links stored with the object that provide the plugin with a method
 for finding those items.  Deleting a file within an object may or may not have the effect of deleting the entire
 object, depending on the file plugin's deletion method.

 LINK TAXONOMY:

 * Many objects have a reference count, and when the reference count reaches 0 the object's deletion method is invoked.
 Some links embody a reference count increase ("countlinks"), and others do not ("nocountlinks").

 * Some links are bi-directional links ("bilinks"), and some are uni-directional("unilinks").

 * Some links are between parts of the same object ("intralinks"), and some are between different objects ("interlinks").

 PACKING TAXONOMY:

 * Some items of an object are stored with a major packing locality based on their object's objectid (e.g. unix directory
 items in plan A), and these are called "self-major-packed".

 * Some items of an object are stored with a major packing locality based on their semantic parent object's objectid
 (e.g. unix file bodies in plan A), and these are called "parent-major-packed".

 * Some items of an object are stored with a major packing locality based on their semantic grandparent, and these are
 called "grandparent-major-packed".  Now carefully notice that we run into trouble with key length if we have to store a
 8 byte major+minor grandparent based packing locality, an 8 byte parent objectid, an 8 byte attribute objectid, and an
 8 byte offset, all in a 24 byte key.  One of these fields must be sacrificed if an item is to be
 grandparent-major-packed, and which to sacrifice is left to the item author choosing to make the item
 grandparent-major-packed.  You cannot make tail items and extent items grandparent-major-packed, though you could make
 them self-major-packed (usually they are parent-major-packed).

 In the case of ACLs (which are composed of fixed length ACEs which consist of {subject-type,
 subject, and permission bitmask} triples), it makes sense to not have an offset field in the ACE item key, and to allow
 duplicate keys for ACEs.  Thus, the set of ACES for a given file is found by looking for a key consisting of the
 objectid of the grandparent (thus grouping all ACLs in a directory together), the minor packing locality of ACE, the
 objectid of the file, and 0.  

 IO involves moving data from one location to another, which means that two locations must be specified, source and
 destination.  

 This source and destination can be in the filesystem, or they can be a pointer in the user process address space plus a byte count.

 If both source and destination are in the filesystem, then at least one of them must be representable as a pure stream
 of bytes (which we call a flow, and define as a struct containing a key, a data pointer, and a length).  This may mean
 converting one of them into a flow.  We provide a generic cast_into_flow() method, which will work for any plugin
 supporting read_flow(), though it is inefficiently implemented in that it temporarily stores the flow in a buffer
 (Question: what to do with huge flows that cannot fit into memory?  Answer: we must not convert them all at once. )

 Performing a write requires resolving the write request into a flow defining the source, and a method that performs the write, and
 a key that defines where in the tree the write is to go.

 Performing a read requires resolving the read request into a flow defining the target, and a method that performs the
 read, and a key that defines where in the tree the read is to come from.

 There will exist file plugins which have no pluginid stored on the disk for them, and which are only invoked by other
 plugins.  

 */

typedef struct file_plugin {


/* reiser4 required file operations */

	int (* write_flow)(flow_t * , /* buffer of data to write */
		      reiser4_key *);
	int (* read_flow)(flow_t * , /* buffer to hold data to be read */
		     reiser4_key *);
/* VFS required/defined operations */
	int ( *truncate )( struct inode *inode, loff_t size );

	/** save inode cached stat-data onto disk. It was called
	    reiserfs_update_sd() in 3.x */
	int ( *write_sd_by_inode)( struct inode *inode );
	int ( *readpage )( struct file *file, struct page * );
	/* these should be implemented using body_read_flow and body_write_flow builtins */
	ssize_t ( *read )( struct file *file, char *buf, size_t size, 
			 loff_t *off );
	ssize_t ( *write )( struct file *file, char *buf, size_t size, 
			 loff_t *off );

	
/*
 * private methods: These are optional.  If used they will allow you to minimize the amount of code needed to implement
 * a deviation from some other method that also uses them.
 */

	/**
	 * Construct flow into @flow according to user-supplied data.
	 * needs better comment
	 */
	int ( *flow_by_inode )( struct file *file, char *buf, size_t size, 
				loff_t *off, rw_op op, flow_t * );
	/*int ( *flow_by_key )( reiser4_key *key, flow_t * );*/
	/*
	 * set the plugin for a file.  Called during file creation in reiser4()
	 * and creat().
	 */
	int ( *set_plug_in_sd )( reiser4_plugin_type plug_type, reiser4_key key_of_sd );
	/*
	 * set the plugin for a file.  Called during file creation in creat()
	 * but not reiser4() unless an inode already exists for the file.
	 */
	int ( *set_plug_in_inode )( reiser4_plugin_type plug_type, struct inode *inode );
	int ( *create_blank_sd )( reiser4_key *key );
	/*
	 * this does whatever is necessary to do when object is created. For
	 * instance, for ordinary files stat data is inserted, for directory
	 * entries "." and ".." get inserted becides stat data
	 */
	int ( *create )( struct inode *object, struct inode *parent,
			 reiser4_object_create_data *data );
	/** 
	 * delete this object's stat-data if REISER4_NO_STAT_DATA is not set
	 * and set REISER4_NO_STAT_DATA 

	 This means which, vs should fix it, or vs thinks it should be fixed?

	 * FIXME-VS: this does not delete stat data only. For example, for
	 * directories which have "." and ".." explicitly it also removes those
	 * entries

	 That should be part of rem_link, not destroy_stat_data, yes?

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

	/** 
	 * checks whether yet another hard links to this object can be
	 * added 
	 */
	int ( *can_add_link )( const struct inode *inode );
} file_plugin;


typedef struct dir_plugin {

				/* resolves one component of name_in, and returns the key that it resolves to plus the
				   remaining name */
	int ( *resolve)(name_t * name_in, /* name within this directory that is to be found */
			name_t * name_out, /* name remaining after the part of the name that was resolved is stripped from it */
			key_t key_found	/* key of what was named */
			);

	/* for use by open call, based on name supplied will install
	   appropriate plugin and state information, into the inode such that
	   subsequent VFS operations that supply a pointer to that inode
	   operate in a manner appropriate.  Note that this may require storing
	   some state for the plugin, and that this state might even include
	   the name used by open.  */
	int (*resolve_into_inode)(struct inode *parent_inode, 
				  struct dentry *dentry );
#if 0
				  const struct qstr *name, 
				  /* probably needs more parameters? */
				  name_t *,
				  reiser4_key *,/* if name is found in
						 * directory key of object is
						 * stored here */
				  reiser4_dir_entry_desc * /* entry key and
							    * inode bound by
							    * it */);
#endif
	/* VFS required/defined operations below this line */
	int ( *unlink )( struct inode *parent, struct dentry *victim );
	int ( *link )( struct inode *parent, struct dentry *existing, 
		       struct dentry *where );
	/* sub-methods: These are optional.  If used they will allow you to minimize the amount of code needed to
	   implement a deviation from some other method that uses them.  You could logically argue that they should be a
	   separate type of plugin. */

	/** check whether "name" is acceptable name to be inserted into
	    this object. Optionally implemented by directory-like objects.
	    Can check for maximal length, reserved symbols etc */
	int ( *is_name_acceptable )( const struct inode *inode, 
				     const char *name, int len );

	int ( *add_entry )( struct inode *object, struct dentry *where, 
			    reiser4_object_create_data *data, 
			    reiser4_dir_entry_desc *entry );

	int ( *rem_entry )( struct inode *object, 
			    struct dentry *where, reiser4_dir_entry_desc *entry );
	/*
	 * create new object described by @data and add it to the @parent
	 * directory under the name described by @dentry
	 */	   
	int ( *create_child )( struct inode *parent, struct dentry *dentry, 
			       reiser4_object_create_data *data );
} dir_plugin;

typedef struct tail_plugin {
	/** returns non-zero iff file's tail has to be stored
	    in a direct item. */
	int ( *tail )( const struct inode *inode, loff_t size );

	/** returns non-zero iff file's tail has to be stored
	    unformatted node */
	int ( *notail )( const struct inode *inode, loff_t new_size );

} tail_plugin;

typedef struct hash_plugin {
	/** computes hash of the given name */
	__u64 ( *hash ) ( const unsigned char *name, int len );
} hash_plugin;

/* hook plugins exist for debugging only? */
typedef struct hook_plugin {
	/** abstract hook function */
	int ( *hook ) ( struct super_block *super, ... );
} hook_plugin;

typedef struct sd_ext_plugin {
	int ( *present ) ( struct inode *inode, char **area, int *len );
	int ( *absent ) ( struct inode *inode );
	int ( *save_len ) ( struct inode *inode );
	int ( *save ) ( struct inode *inode, char **area );
	/** alignment requirement for this stat-data part */
	int alignment;
} sd_ext_plugin;

/** plugin instance. 
    We keep everything inside single union for simplicity.
    Alternative solution is to to keep size of actual plugin
    in plugin type description. */
struct reiser4_plugin {
	/** generic fields */
	plugin_header h;
	/** data specific to particular plugin type */
	union __plugins {
		file_plugin   file;
		dir_plugin    dir;
		hash_plugin   hash;
		tail_plugin   tail;
		hook_plugin   hook;
		perm_plugin   perm;
		node_plugin   node;
		item_plugin   item;
		sd_ext_plugin sd_ext;
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
	/** plugin of file */
	file_plugin            *file;
	/** plugin of dir */
	dir_plugin             *dir;
	/** perm plugin for this file */
	perm_plugin            *perm;
	/** tail policy plugin. Only meaningful for regular files */
	tail_plugin            *tail;
	/** hash plugin. Only meaningful for directory files */
	hash_plugin            *hash;
	/** plugin of stat-data */
	item_plugin            *sd;
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
#if REISER4_USE_COLLISION_LIMIT
	int                        max_collisions;
#endif
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
extern int set_object_plugin( struct inode *inode, reiser4_plugin_id id );
extern int handle_default_plugin_option( char *option, 
						 reiser4_plugin **area );
extern int setup_plugins( struct super_block *super, 
				  reiser4_plugin **area );
extern reiser4_plugin *lookup_plugin( char *type_label, char *plug_label );
extern int inherit_if_nil( reiser4_plugin **to, reiser4_plugin **from );
extern int init_plugins( void );

/* builtin plugins */

/* builtin file-plugins */
typedef enum { REGULAR_FILE_PLUGIN_ID, DIRECTORY_FILE_PLUGIN_ID,
	       SYMLINK_FILE_PLUGIN_ID,
	       /* 
		* SPECIAL_FILE_PLUGIN_ID is for objects completely handled by
		* VFS: fifos, devices, sockets 
		*/
	       SPECIAL_FILE_PLUGIN_ID,
	       LAST_FILE_PLUGIN_ID
} reiser4_file_id;

/* builtin dir-plugins */
typedef enum { 
	HASHED_DIR_PLUGIN_ID,
	LAST_DIR_ID
} reiser4_dir_id;

/* defined in fs/reiser4/plugin/object.c */
extern reiser4_plugin file_plugins[ LAST_FILE_PLUGIN_ID ];

/** data type used to pack parameters that we pass to vfs
    object creation function create_object() */
struct reiser4_object_create_data {
	/** plugin to control created object */
	reiser4_file_id id;
	/** mode of regular file, directory or special file */
/* what happens if some other sort of perm plugin is in use? */
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

extern reiser4_plugin *plugin_by_disk_id( reiser4_tree *tree, 
					  reiser4_plugin_type type_id, d16 *did );

extern reiser4_plugin *plugin_by_unsafe_id( reiser4_plugin_type type_id, 
					    reiser4_plugin_id id );

#define PLUGIN_BY_ID(TYPE,ID,FIELD)                                                \
static inline TYPE *TYPE ## _by_id( reiser4_plugin_id id )                         \
{                                                                                  \
	reiser4_plugin *plugin = plugin_by_id ( ID, id );                     \
	return plugin ? & plugin -> u.FIELD : NULL;                                \
}                                                                                  \
static inline TYPE *TYPE ## _by_disk_id( reiser4_tree *tree, d16 *id )             \
{                                                                                  \
	reiser4_plugin *plugin = plugin_by_disk_id ( tree, ID, id );          \
	return plugin ? & plugin -> u.FIELD : NULL;                                \
}                                                                                  \
static inline TYPE *TYPE ## _by_unsafe_id( reiser4_plugin_id id )                  \
{                                                                                  \
	reiser4_plugin *plugin = plugin_by_unsafe_id ( ID, id );              \
	return plugin ? & plugin -> u.FIELD : NULL;                                \
}                                                                                  \
static inline reiser4_plugin* TYPE ## _to_plugin( TYPE* plugin )                   \
{                                                                                  \
	if (plugin != NULL)                                                        \
		return (reiser4_plugin*) (((long) plugin) - sizeof (plugin_header));       \
	else                                                                       \
		return NULL;                                                       \
}                                                                                  \
static inline reiser4_plugin_id TYPE ## _id( TYPE* plugin )                        \
{                                                                                  \
	return TYPE ## _to_plugin (plugin) -> h.id;                                \
}                                                                                  \
typedef struct { int foo; } TYPE ## _plugin_dummy

PLUGIN_BY_ID(item_plugin,REISER4_ITEM_PLUGIN_TYPE,item);
PLUGIN_BY_ID(file_plugin,REISER4_FILE_PLUGIN_TYPE,file);
PLUGIN_BY_ID(dir_plugin,REISER4_DIR_PLUGIN_TYPE,dir);
PLUGIN_BY_ID(node_plugin,REISER4_NODE_PLUGIN_TYPE,node);
PLUGIN_BY_ID(sd_ext_plugin,REISER4_SD_EXT_PLUGIN_TYPE,sd_ext);
PLUGIN_BY_ID(perm_plugin,REISER4_PERM_PLUGIN_TYPE,perm);
PLUGIN_BY_ID(hash_plugin,REISER4_HASH_PLUGIN_TYPE,hash);
PLUGIN_BY_ID(tail_plugin,REISER4_TAIL_PLUGIN_TYPE,tail);

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
