/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Methods of directory plugin.
 */

/* version 3 has no directory read-ahead.  This is silly/wrong.  It
   would be nice if there was some commonality between file and
   directory read-ahead code, but I am not sure how well this can be
   done.  */

#include "../../reiser4.h"

static int create_dot_dotdot( struct inode *object, struct inode *parent );
static int dir_find_entry( const struct inode *dir, const struct qstr *name,
			   tree_coord *coord, reiser4_lock_handle *lh,
			   znode_lock_mode mode, reiser4_entry *entry );
static int dir_add_entry( struct inode *dir, struct dentry *where,
			  tree_coord *coord, reiser4_lock_handle *lh,
			  reiser4_object_create_data *data, 
			  reiser4_entry *entry );
static int dir_rem_entry( struct inode *dir, tree_coord *coord, 
			  reiser4_lock_handle *lh, reiser4_entry *entry );

/**
 * generic implementation of ->lookup() method.
 */
file_lookup_result directory_lookup( struct inode *inode /* inode of directory
							  * to lookup into */, 
				     const struct qstr *name /* name to look
							      * for */, 
				     reiser4_key *key /* length of name to
						       * look for */,
				     reiser4_entry *entry /* key of object
							   * found */ )
{
	int                 result;
	tree_coord         coord;
	reiser4_lock_handle lh;

	assert( "nikita-1247", inode != NULL );
	assert( "nikita-1248", name != NULL );
	assert( "nikita-1122", name != NULL );
	assert( "nikita-1123", name -> name != NULL );
	assert( "nikita-1249", key != NULL );

	reiser4_init_coord( &coord );
	reiser4_init_lh( &lh );

	/*
	 * find entry in a directory. This is plugin method.
	 */
	result = dir_find_entry( inode, name, &coord, &lh, ZNODE_READ_LOCK, 
				 entry );
	if( result == 0 )
		/*
		 * if entry was found, extract object key from it.
		 */
		result = item_plugin_by_coord( &coord ) -> 
			u.item.s.dir.extract_key( &coord, key );
	reiser4_done_lh( &lh );
	reiser4_done_coord( &coord );
	return result;
}

int directory_add_entry( struct inode *object, struct dentry *where, 
			 reiser4_object_create_data *data,
			 reiser4_entry *entry )
{
	int                 result;
	tree_coord          coord;
	reiser4_lock_handle lh;

	assert( "nikita-1114", object != NULL );
	assert( "nikita-1250", where != NULL );

	reiser4_init_coord( &coord );
	reiser4_init_lh( &lh );

	trace_on( TRACE_DIR, "[%i]: creating \"%s\" in %lx\n", current_pid,
		  where -> d_name.name, object -> i_ino );
	/*
	 * check for this entry in a directory. This is plugin method.
	 */
	result = dir_find_entry( object, &where -> d_name, &coord, &lh,
				 ZNODE_WRITE_LOCK, entry );
	if( result == -ENOENT )
		/*
		 * add new entry
		 */
		result = dir_add_entry( object, where, &coord, &lh, data, entry );
	else if( result == 0 )
		result = -EEXIST;
	reiser4_done_lh( &lh );
	reiser4_done_coord( &coord );
	return result;
}

int directory_rem_entry( struct inode *object, struct dentry *where, 
			 reiser4_entry *entry )
{
	int                 result;
	tree_coord         coord;
	reiser4_lock_handle lh;

	assert( "nikita-1124", object != NULL );
	assert( "nikita-1125", where != NULL );

	reiser4_init_coord( &coord );
	reiser4_init_lh( &lh );

	/*
	 * check for this entry in a directory. This is plugin method.
	 */
	result = dir_find_entry( object, &where -> d_name, &coord, &lh,
				 ZNODE_WRITE_LOCK, entry );
	if( result == 0 )
		/*
		 * remove entry
		 */
		result = dir_rem_entry( object, &coord, &lh, entry );
	reiser4_done_lh( &lh );
	reiser4_done_coord( &coord );
	return result;
}

/** create sd for directory file. Create stat-data, dot, and dotdot. */
int directory_file_create( struct inode *object, struct inode *parent,
			   reiser4_object_create_data *data UNUSED_ARG )
{
	int result;

	assert( "nikita-680", object != NULL );
	assert( "nikita-681", S_ISDIR( object -> i_mode ) );
	assert( "nikita-682", parent != NULL );
	assert( "nikita-684", data != NULL );
	assert( "nikita-685", 
		*reiser4_inode_flags( object ) & REISER4_NO_STAT_DATA );
	assert( "nikita-686", data -> id == DIRECTORY_FILE_PLUGIN_ID );
	assert( "nikita-687", object -> i_mode & S_IFDIR );
	trace_stamp( TRACE_DIR );
	
	result = common_file_save( object );
	if( result == 0 )
		result = create_dot_dotdot( object, parent );
	return result;
}

/**
 * directory_file_delete() - ->delete() method of directory plugin
 *
 * Delete dot and dotdot, decrease nlink on parent and call
 * common_file_delete() to delete stat data.
 *
 */
int directory_file_delete( struct inode *object, struct inode *parent )
{
	reiser4_entry entry;
	struct dentry goodby_dots;
	int           result;

	assert( "nikita-1449", object != NULL );

	memset( &entry, 0, sizeof entry );
	memset( &goodby_dots, 0, sizeof goodby_dots );

	entry.obj = goodby_dots.d_inode = object;
	goodby_dots.d_name.name = ".";
	goodby_dots.d_name.len = 1;
	directory_rem_entry( object, &goodby_dots, &entry );

	entry.obj = goodby_dots.d_inode = parent;
	goodby_dots.d_name.name = "..";
	goodby_dots.d_name.len = 2;
	result = directory_rem_entry( object, &goodby_dots, &entry );

	reiser4_del_nlink( parent );
	return common_file_delete( object, parent );
}

int dir_file_owns_item( const struct inode *inode, const tree_coord *coord )
{
	reiser4_key item_key;

	assert( "nikita-1335", inode != NULL );
	assert( "nikita-1334", coord != NULL );

	if( item_type_by_coord( coord ) == DIR_ENTRY_ITEM_TYPE )
		/*
		 * FIXME-NIKITA move this into kassign.c
		 */
		return get_key_locality( item_key_by_coord( coord, &item_key ) ) == ( oid_t ) inode -> i_ino;
	else
		return common_file_owns_item( inode, coord );
}

/** helper function for directory_file_create(). Create "." and ".." */
static int create_dot_dotdot( struct inode *object, struct inode *parent )
{
	int           result;
	struct dentry dots_entry;
	reiser4_entry entry;

	assert( "nikita-688", object != NULL );
	assert( "nikita-689", S_ISDIR( object -> i_mode ) );
	assert( "nikita-691", parent != NULL );
	trace_stamp( TRACE_DIR );
	
	/*
	 * We store dot and dotdot as normal directory entries. This is
	 * not necessary, because almost all information stored in them
	 * is already in the stat-data of directory, the only thing
	 * being missed is objectid of grand-parent directory that can
	 * easily be added there as extension.
	 *
	 * But it is done the way it is done, because not storing dot
	 * and dotdot will lead to the following complications:
	 *
	 * . special case handling in ->lookup().
	 * . addition of another extension to the sd.
	 * . dependency on key allocation policy for stat data.
	 *
	 */

	memset( &entry, 0, sizeof entry );
	memset( &dots_entry, 0, sizeof dots_entry );
	entry.obj = dots_entry.d_inode = object;
	dots_entry.d_name.name = ".";
	dots_entry.d_name.len = 1;
	result = directory_add_entry( object, &dots_entry, NULL, &entry );
	
	if( result == 0 )
		result = reiser4_add_nlink( object );

	if( result == 0 ) {
		entry.obj = dots_entry.d_inode = parent;
		dots_entry.d_name.name = "..";
		dots_entry.d_name.len = 2;
		result = directory_add_entry( object, &dots_entry, NULL, &entry );
		/*
		 * if creation of ".." failed, iput() will delete object
		 * with ".".
		 */
	}

	if( result == 0 )
		result = reiser4_add_nlink( parent );

	return result;
}

static int dir_find_entry( const struct inode *dir, const struct qstr *name, 
			   tree_coord *coord, reiser4_lock_handle *lh,
			   znode_lock_mode mode, reiser4_entry *entry )
{
	assert( "nikita-1138", lh != NULL );
	assert( "nikita-1106", dir != NULL );
	assert( "nikita-1107", name != NULL );
	assert( "nikita-1108", coord != NULL );

	return reiser4_get_file_plugin( dir ) -> u.dir.find_entry
		( dir, name, coord, lh, mode, entry );
}

static int dir_add_entry( struct inode *dir, struct dentry *where,
			  tree_coord *coord, reiser4_lock_handle *lh,
			  reiser4_object_create_data *data, 
			  reiser4_entry *entry )
{
	assert( "nikita-1118", dir != NULL );
	assert( "nikita-1119", coord != NULL );

	return reiser4_get_file_plugin( dir ) -> u.dir.add_entry
		( dir, where, coord, lh, data, entry );
}

static int dir_rem_entry( struct inode *dir, tree_coord *coord, 
			  reiser4_lock_handle *lh, reiser4_entry *entry )
{
	assert( "nikita-1126", dir != NULL );
	assert( "nikita-1127", coord != NULL );

	return reiser4_get_file_plugin( dir ) -> u.dir.rem_entry
		( dir, coord, lh, entry );
}


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

