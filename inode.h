/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Inode functions.
 */

#if !defined( __REISER4_INODE_H__ )
#define __REISER4_INODE_H__

/** reiser4-specific part of inode */
typedef struct reiser4_inode_info {
	/** plugin, associated with inode and its state, including
	     dependant plugins: 
	     object,
	     security, 
	     tail policy, 
	     hash for directories */
	reiser4_plugin_ref plugin;
	/**
	 * generic fields
	 */
	struct inode       vfs_inode;
} reiser4_inode_info;

extern reiser4_tree *tree_by_inode( const struct inode *inode );
extern reiser4_plugin_ref *reiser4_get_object_state( const struct inode *inode );
extern reiser4_inode_info *reiser4_inode_data( const struct inode * );
extern __u32 *reiser4_inode_flags( const struct inode *inode );
extern file_plugin *reiser4_get_file_plugin( const struct inode *inode );
extern dir_plugin *reiser4_get_dir_plugin( const struct inode *inode );
extern int reiser4_max_filename_len( const struct inode *inode );
extern item_plugin *reiser4_get_sd_plugin( const struct inode *inode );
extern inter_syscall_ra_hint *reiser4_inter_syscall_ra( const struct inode *inode );
extern void reiser4_lock_inode( struct inode *inode );
extern int reiser4_lock_inode_interruptible( struct inode *inode );
extern void reiser4_unlock_inode( struct inode *inode );
extern int is_reiser4_inode( const struct inode *inode );
extern int setup_inode_ops( struct inode *inode );
extern int init_inode( struct inode *inode, tree_coord *coord );
extern struct inode * reiser4_iget( struct super_block * super, 
				    const reiser4_key *key );

extern int reiser4_add_nlink( struct inode *object );
extern int reiser4_del_nlink( struct inode *object );
extern int truncate_object( struct inode *inode, loff_t size );
extern int lookup_sd( struct inode *inode, znode_lock_mode lock_mode, 
			 tree_coord *coord, reiser4_lock_handle *lh,
			 reiser4_key *key );
int lookup_sd_by_key( reiser4_tree *tree, znode_lock_mode lock_mode, 
		      tree_coord *coord, reiser4_lock_handle *lh, 
		      reiser4_key *key );
extern reiser4_plugin *guess_plugin_by_mode( struct inode *inode );
extern int common_file_install( struct inode *inode, reiser4_plugin *plug,
				struct inode *parent, 
				reiser4_object_create_data *data );
extern int common_file_delete( struct inode *inode, struct inode *parent );
extern int common_file_save( struct inode *inode );
extern int common_write_inode( struct inode *inode );
extern int common_file_owns_item( const struct inode *inode, 
				  const tree_coord *coord );
extern void print_inode( const char *prefix, const struct inode *i );

extern struct file_operations reiser4_file_operations;
extern struct inode_operations reiser4_inode_operations;
extern struct super_operations reiser4_super_operations;
extern struct address_space_operations reiser4_as_operations;

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
} reiser4_dentry_fsdata;

/**
 * &reiser4_dentry_fsdata - reiser4-specific data attached to files.
 *
 * This is allocated dynamically and released in reiser4_release()
 */
typedef struct reiser4_file_fsdata {
	/**
	 * last 64 bits of key, used by ->readdir()
	 */
	__u64 readdir_offset;
} reiser4_file_fsdata;

extern reiser4_dentry_fsdata *reiser4_get_dentry_fsdata( struct dentry *dentry );
extern reiser4_file_fsdata *reiser4_get_file_fsdata( struct file *f );


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
