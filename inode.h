/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Inode functions.
 */

#if !defined( __REISER4_INODE_H__ )
#define __REISER4_INODE_H__

/** reiser4-specific inode flags */
typedef enum { 
	/** 
	 * this is light-weight inode, inheriting some state from its
	 * parent 
	 */
	REISER4_LIGHT_WEIGHT_INODE = 0,
	/** stat data wasn't yet created */
	REISER4_NO_STAT_DATA       = 1,
	/** internal immutable flag. Currently is only used
	    to avoid race condition during file creation.
	    See comment in create_object(). */
	REISER4_IMMUTABLE          = 2,
	/** inode was read from storage */
	REISER4_LOADED             = 3,
	/** this is set when we know for sure state of file tail: for default
	 * reiser4 ordinary files it means that we know whether file is built
	 * of extents or of tail items only */
	REISER4_TAIL_STATE_KNOWN   = 4,
	/** this is set to 1 when not all file data are stored as unformatted
	 * node, 0 - otherwise. Note, that this bit can be only checked if
	 * REISER4_TAIL_STATE_KNOWN is set
	 */
	REISER4_HAS_TAIL           = 5,
	/* this bit is set for symlinks. inode->u.generic_ip points to target
	 * name of symlink */
	REISER4_GENERIC_VP_USED    = 6
} reiser4_file_plugin_flags;

/* state associated with each inode.
 * reiser4 inode.
 *
 * FIXME-NIKITA In 2.5 kernels it is not necessary that all file-system inodes
 * be of the same size. File-system allocates inodes by itself through
 * s_op->allocate_inode() method. So, it is possible to adjust size of inode
 * at the time of its creation.
 *
 */
typedef struct reiser4_inode_info {
	/** plugin of file */
	file_plugin            *file;
	/** plugin of dir */
	dir_plugin             *dir;
	/** perm plugin for this file */
	perm_plugin            *perm;
	/** tail policy plugin. Only meaningful for regular files */
	tail_plugin            *tail;
	/** hash plugin. Only meaningful for directories. */
	hash_plugin            *hash;
	/** plugin of stat-data */
	item_plugin            *sd;
	/** plugin of items a directory is built of */
	item_plugin            *dir_item;
	spinlock_t              guard;
	/** seal for stat-data */
	seal_t                    sd_seal;
	/** coord of stat-data in sealed node */
	coord_t                sd_coord;
	/** reiser4-specific inode flags. They are "transient" and are not
	    supposed to be stored on a disk. Used to trace "state" of
	    inode. Bitmasks for this field are defined in
	    reiser4_file_plugin_flags enum */
	unsigned long              flags;
	/** bytes actually used by the file */
	__u64                      bytes;
	__u64                      extmask;
	/** length of stat-data for this inode */
	short                      sd_len;
	/** bitmask of non-default plugins for this inode */
	__u16                      plugin_mask;
	inter_syscall_rap          ra;
	/** locality id for this file */
	oid_t                      locality_id;
	/* tail2extent and extent2tail use down_write, read, write, readpage -
	 * down_read */
	struct rw_semaphore        sem;
	/** generic fields not specific to reiser4, but used by VFS */
	struct inode       vfs_inode;
} reiser4_inode_info;

#define spin_ordering_pred_inode( inode )   (1)
SPIN_LOCK_FUNCTIONS( inode, reiser4_inode_info, guard );

extern reiser4_tree *tree_by_inode( const struct inode *inode );
extern reiser4_inode_info *reiser4_inode_data( const struct inode *inode );
extern int reiser4_max_filename_len( const struct inode *inode );
extern int max_hash_collisions( const struct inode *dir );
extern inter_syscall_rap *inter_syscall_ra( const struct inode *inode );
extern void reiser4_lock_inode( struct inode *inode );
extern int reiser4_lock_inode_interruptible( struct inode *inode );
extern void reiser4_unlock_inode( struct inode *inode );
extern int is_reiser4_inode( const struct inode *inode );
extern int setup_inode_ops( struct inode *inode, 
			    reiser4_object_create_data *data );
extern int init_inode( struct inode *inode, coord_t *coord );
extern struct inode * reiser4_iget( struct super_block * super, 
				    const reiser4_key *key );
extern int reiser4_inode_find_actor( struct inode *inode, void *opaque);

extern int reiser4_add_nlink( struct inode *object, int write_sd_p );
extern int reiser4_del_nlink( struct inode *object, int write_sd_p );
extern int truncate_object( struct inode *inode, loff_t size );
extern int lookup_sd( struct inode *inode, znode_lock_mode lock_mode, 
			 coord_t *coord, lock_handle *lh,
			 reiser4_key *key );
extern int lookup_sd_by_key( reiser4_tree *tree, znode_lock_mode lock_mode, 
		      coord_t *coord, lock_handle *lh, 
		      const reiser4_key *key );
extern int is_empty_actor( reiser4_tree *tree, coord_t *coord, lock_handle *lh,
                           void *arg );

extern int guess_plugin_by_mode( struct inode *inode );
extern int common_file_install( struct inode *inode, reiser4_plugin *plug,
				struct inode *parent, 
				reiser4_object_create_data *data );
extern int common_file_delete( struct inode *inode, struct inode *parent );
extern int common_file_save( struct inode *inode );
extern int common_build_flow( struct inode *, char *buf,
			      int user, size_t size,
			      loff_t off, rw_op op, flow_t *);

extern int common_write_inode( struct inode *inode );
extern int common_file_owns_item( const struct inode *inode, 
				  const coord_t *coord );
#if REISER4_DEBUG
extern void print_inode( const char *prefix, const struct inode *i );
#else
#define print_inode( p, i ) noop
#endif

extern void inode_set_flag( struct inode *inode, reiser4_file_plugin_flags f );
extern void inode_clr_flag( struct inode *inode, reiser4_file_plugin_flags f );
extern int  inode_get_flag( struct inode *inode, reiser4_file_plugin_flags f );

extern file_plugin *inode_file_plugin( const struct inode *inode );
extern dir_plugin  *inode_dir_plugin ( const struct inode *inode );
extern perm_plugin *inode_perm_plugin( const struct inode *inode );
extern tail_plugin *inode_tail_plugin( const struct inode *inode );
extern hash_plugin *inode_hash_plugin( const struct inode *inode );
extern item_plugin *inode_sd_plugin( const struct inode *inode );
extern item_plugin *inode_dir_item_plugin( const struct inode *inode );

extern struct file_operations reiser4_file_operations;
extern struct inode_operations reiser4_inode_operations;
extern struct inode_operations reiser4_symlink_inode_operations;
extern struct super_operations reiser4_super_operations;
extern struct address_space_operations reiser4_as_operations;
extern struct dentry_operations reiser4_dentry_operation;

extern int reiser4_invalidatepage( struct page *page, unsigned long offset );
extern int reiser4_releasepage( struct page *page, int gfp );
extern int reiser4_writepages( struct address_space *, int *nr_to_write );

/**
 * &reiser4_dentry_fsdata - reiser4-specific data attached to dentries.
 *
 * This is allocated dynamically and released in d_op->d_release()
 */
typedef struct reiser4_dentry_fsdata {
	/*
	 * here will go fields filled by ->lookup() to speedup next
	 * create/unlink, like blocknr of znode with stat-data, or key
	 * of stat-data.
	 */

	/* seal covering directory entry */
	seal_t     entry_seal;
	/* coord of directory entry */
	coord_t entry_coord;
} reiser4_dentry_fsdata;

/**
 * &reiser4_dentry_fsdata - reiser4-specific data attached to files.
 *
 * This is allocated dynamically and released in reiser4_release()
 */
typedef union reiser4_file_fsdata {
	/*
	 * not clear what to do with objects that have both body and index. I
	 * am hesitating to add so much to each struct file as duplicating of
	 * all that stuff would require.
	 */
	struct {
		/**
		 * last 64 bits of key, used by ->readdir()
		 */
		__u64 readdir_offset;
		/**
		 * how many entries with identical keys to skip on the next
		 * readdir()
		 */
		__u64 skip;
	} dir;
	struct {
		/*
		 * store a seal for last accessed piece of meta-data: either
		 * tail or extent item. This can be used to avoid tree
		 * traversals.
		 */
		seal_t last_access;
		/*
		 * FIXME-VS: without coord seal is more or less
		 * useless. Shouldn't therefore coord be included into seal?
		 */
		coord_t coord;
		tree_level level;
	} reg;
} reiser4_file_fsdata;

extern reiser4_dentry_fsdata *reiser4_get_dentry_fsdata( struct dentry *dentry );
extern reiser4_file_fsdata *reiser4_get_file_fsdata( struct file *f );
extern void reiser4_make_bad_inode( struct inode *inode );


/* __REISER4_INODE_H__ */
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
