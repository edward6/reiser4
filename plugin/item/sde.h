/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/* 
 * Directory entry.
 */

#if !defined( __FS_REISER4_PLUGIN_DIRECTORY_ENTRY_H__ )
#define __FS_REISER4_PLUGIN_DIRECTORY_ENTRY_H__

/**
 * methods specific to the directory item
 */
typedef struct simple_dir_item_plugin {
	/**
	 * extract stat-data key from directory entry at @coord and place it
	 * into @key.
	 */
	int ( *simple_file_key_by_entry )( const tree_coord *coord, reiser4_key *key );
	/**
	 * extract name from directory entry at @coord and return it
	 */
	char *( *simple_extract_name )( const tree_coord *coord );
	/**
	 * extract file type (DT_* stuff) from directory entry at @coord and
	 * return it
	 */
	unsigned ( *simple_extract_file_type )( const tree_coord *coord );
	int ( *simple_add_entry )( const struct inode *dir,
				   tree_coord *coord, lock_handle *lh,
				   const struct dentry *name, reiser4_dir_entry_desc *entry );
	int ( *simple_rem_entry )( const struct inode *dir,
				   tree_coord *coord, lock_handle *lh,
				   reiser4_dir_entry_desc *entry );
	int ( *simple_max_name_len )( int block_size );
} simple_dir_item_plugin;

typedef struct directory_entry_format {
	/**
	 * key of object stat-data. It's not necessary to store whole
	 * key here, because it's always key of stat-data, so minor
	 * packing locality and offset can be omitted here. But this
	 * relies on particular key allocation scheme for stat-data, so,
	 * for extensibility sake, whole key can be stored here.
	 * 
	 * We store key as array of bytes, because we don't want 8-byte
	 * alignment of dir entries.
	 */
	obj_key_id id;
	/**
	 * file name. Null terminated string.
	 */
	d8 name[ 0 ];
} directory_entry_format;

void  de_print        ( const char *prefix, tree_coord *coord );
int   de_extract_key  ( const tree_coord *coord, reiser4_key *key );
char *de_extract_name ( const tree_coord *coord );
unsigned de_extract_file_type( const tree_coord *coord );
int   de_add_entry    ( const struct inode *dir, tree_coord *coord, 
			lock_handle *lh, const struct dentry *name, 
			reiser4_dir_entry_desc *entry );
int   de_rem_entry    ( const struct inode *dir, tree_coord *coord, 
			lock_handle *lh, reiser4_dir_entry_desc *entry );
int   de_max_name_len ( int block_size );

/* __FS_REISER4_PLUGIN_DIRECTORY_ENTRY_H__ */
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
