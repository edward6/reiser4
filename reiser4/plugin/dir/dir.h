/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory plugin's methods.
 */

#if !defined( __REISER4_DIR_H__ )
#define __REISER4_DIR_H__

/** create sd for directory file. Create stat-data, dot, and dotdot. */
extern int                directory_file_create( struct inode *object, 
						 struct inode *parent,
						 reiser4_object_create_data * );
extern int                directory_file_delete( struct inode *object, 
						 struct inode *parent );
extern int                dir_file_owns_item   ( const struct inode *inode, 
						 const tree_coord *coord );
extern file_lookup_result directory_lookup     ( struct inode *inode, 
						 const struct qstr *name,
						 reiser4_key *key, 
						 reiser4_entry *entry );
extern int                directory_add_entry  ( struct inode *object,
						 struct dentry *where, 
						 reiser4_object_create_data *,
						 reiser4_entry *entry );
extern int                directory_rem_entry  ( struct inode *object, 
						 struct dentry *where, 
						 reiser4_entry *entry );

/* __REISER4_DIR_H__ */
#endif

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
