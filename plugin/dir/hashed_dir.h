/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
 * file names to to files.
 */

#if !defined( __HASHED_DIR_H__ )
#define __HASHED_DIR_H__

/** create sd for directory file. Create stat-data, dot, and dotdot. */
extern int                hashed_create    ( struct inode *object, 
					     struct inode *parent,
					     reiser4_object_create_data * );
extern int                hashed_delete    ( struct inode *object, 
					     struct inode *parent );
extern int                hashed_owns_item ( const struct inode *inode, 
					     const new_coord *coord );
extern file_lookup_result hashed_lookup    ( struct inode *inode, struct dentry *dentry );
/*					     const struct qstr *name, name_t *,
					     reiser4_key *key, 
					     reiser4_dir_entry_desc *entry );*/
extern int                hashed_add_entry ( struct inode *object,
					     struct dentry *where, 
					     reiser4_object_create_data *,
					     reiser4_dir_entry_desc *entry );
extern int                hashed_rem_entry ( struct inode *object, 
					     struct dentry *where, 
					     reiser4_dir_entry_desc *entry );

/* __HASHED_DIR_H__ */
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
