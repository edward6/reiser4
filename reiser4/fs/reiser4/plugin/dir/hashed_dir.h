/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
 * file names to to files.
 */

#if !defined( __HASHED_DIR_H__ )
#define __HASHED_DIR_H__

extern int hashed_dir_find ( const struct inode *dir, const struct qstr *name, 
			     tree_coord *coord, reiser4_lock_handle *lh,
			     znode_lock_mode mode, reiser4_entry *entry );

extern int hashed_dir_add  ( struct inode *dir, const struct dentry *where,
			     tree_coord *coord, reiser4_lock_handle *lh,
			     reiser4_object_create_data *data,
			     reiser4_entry *entry );

extern int hashed_dir_rem  ( struct inode *dir, tree_coord *coord, 
			     reiser4_lock_handle *lh, reiser4_entry *entry );

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
