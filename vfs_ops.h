/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * vfs_ops.c's exported symbols
 */

#if !defined( __FS_REISER4_VFS_OPS_H__ )
#define __FS_REISER4_VFS_OPS_H__

extern int reiser4_write_sd ( struct inode *object );
extern int reiser4_add_nlink( struct inode *object, int write_sd_p );
extern int reiser4_del_nlink( struct inode *object, int write_sd_p );

extern int truncate_object  ( struct inode *inode, loff_t size );

extern void reiser4_free_dentry_fsdata( struct dentry *dentry );

extern struct file_operations          reiser4_file_operations;
extern struct inode_operations         reiser4_inode_operations;
extern struct inode_operations         reiser4_symlink_inode_operations;
extern struct super_operations         reiser4_super_operations;
extern struct address_space_operations reiser4_as_operations;
extern struct dentry_operations        reiser4_dentry_operation;

extern int reiser4_invalidatepage( struct page *page, unsigned long offset );
extern int reiser4_releasepage   ( struct page *page, int gfp );
extern int reiser4_writepages    ( struct address_space *, int *nr_to_write );

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

TS_LIST_DECLARE( readdir );

/**
 * &reiser4_dentry_fsdata - reiser4-specific data attached to files.
 *
 * This is allocated dynamically and released in reiser4_release()
 */
typedef struct reiser4_file_fsdata {
	struct file *back;
	/*
	 * We need both directory and regular file parts here, because there
	 * are file system objects that are files and directories.
	 */
	struct {
		readdir_pos readdir;
		readdir_list_link linkage;
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

TS_LIST_DEFINE( readdir, reiser4_file_fsdata, dir.linkage );

extern reiser4_dentry_fsdata *reiser4_get_dentry_fsdata( struct dentry *dentry );
extern reiser4_file_fsdata   *reiser4_get_file_fsdata  ( struct file *f );

/* __FS_REISER4_VFS_OPS_H__ */
#endif
