/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
 * file names to to files.
 */

#include "../../reiser4.h"

static int create_dot_dotdot( struct inode *object, struct inode *parent );
static int find_entry( const struct inode *dir, const struct qstr *name, 
		       tree_coord *coord, reiser4_lock_handle *lh,
		       znode_lock_mode mode, reiser4_entry *entry );

/** create sd for directory file. Create stat-data, dot, and dotdot. */
int hashed_create( struct inode *object /* new directory */, 
		   struct inode *parent /* parent directory */,
		   reiser4_object_create_data *data UNUSED_ARG /* info
								* passed
								* to us,
								* this
								* is
								* filled
								* by
								* reiser4()
								* syscall
								* in
								* particular */ )
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
 * ->delete() method of directory plugin
 *
 * Delete dot and dotdot, decrease nlink on parent and call
 * common_file_delete() to delete stat data.
 *
 */
int hashed_delete( struct inode *object, struct inode *parent )
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
	hashed_rem_entry( object, &goodby_dots, &entry );

	entry.obj = goodby_dots.d_inode = parent;
	goodby_dots.d_name.name = "..";
	goodby_dots.d_name.len = 2;
	result = hashed_rem_entry( object, &goodby_dots, &entry );

	reiser4_del_nlink( parent );
	return common_file_delete( object, parent );
}

/**
 * ->owns_item() for hashed directory object plugin.
 */
int hashed_owns_item( const struct inode *inode, const tree_coord *coord )
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
	result = hashed_add_entry( object, &dots_entry, NULL, &entry );
	
	if( result == 0 )
		result = reiser4_add_nlink( object );

	if( result == 0 ) {
		entry.obj = dots_entry.d_inode = parent;
		dots_entry.d_name.name = "..";
		dots_entry.d_name.len = 2;
		result = hashed_add_entry( object, &dots_entry, NULL, &entry );
		/*
		 * if creation of ".." failed, iput() will delete object
		 * with ".".
		 */
	}

	if( result == 0 )
		result = reiser4_add_nlink( parent );

	return result;
}

/**
 * implementation of ->lookup() method for hashed durectories.
 */
file_lookup_result hashed_lookup( struct inode *inode /* inode of
						       * directory to
						       * lookup into */, 
				  const struct qstr *name /* name to
							   * look for */, 
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
	result = find_entry( inode, name, &coord, &lh, ZNODE_READ_LOCK, entry );
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

/**
 * ->add_entry() method for hashed directory object plugin.
 */
int hashed_add_entry( struct inode *object, struct dentry *where, 
		      reiser4_object_create_data *data UNUSED_ARG, 
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
	result = find_entry( object, &where -> d_name, &coord, &lh, 
			     ZNODE_WRITE_LOCK, entry );
	if( result == -ENOENT ) {
		/*
		 * add new entry. Jusr pass control to the directory
		 * item plugin.
		 */
		/*
		 * FIXME-NIKITA hardcode reference to particular
		 * directory item plugin. This should be done in a way
		 * similar to sd.
		 */
		result = plugin_by_id( REISER4_ITEM_PLUGIN_ID, 
				       REISER4_DIR_ITEM_PLUGIN ) -> 
			u.item.s.dir.add_entry( object, 
						&coord, &lh, where, entry );
	} else if( result == 0 )
		result = -EEXIST;
	reiser4_done_lh( &lh );
	reiser4_done_coord( &coord );
	return result;
}

/**
 * ->rem_entry() method for hashed directory object plugin.
 */
int hashed_rem_entry( struct inode *object /* directory from which entry
					    * is begin removed */, 
		      struct dentry *where /* name that is being
					    * removed */, 
		      reiser4_entry *entry /* description of entry being
					    * removed */ )
{
	int                 result;
	tree_coord          coord;
	reiser4_lock_handle lh;

	assert( "nikita-1124", object != NULL );
	assert( "nikita-1125", where != NULL );

	reiser4_init_coord( &coord );
	reiser4_init_lh( &lh );

	/*
	 * check for this entry in a directory. This is plugin method.
	 */
	result = find_entry( object, &where -> d_name, &coord, &lh,
			     ZNODE_WRITE_LOCK, entry );
	if( result == 0 ) {
		/*
		 * remove entry. Just pass control to the directory item
		 * plugin.
		 */
		if( item_type_by_coord( &coord ) != DIR_ENTRY_ITEM_TYPE ) {
			warning( "nikita-1161", "Non directory item found" );
			print_plugin( "plugin", item_plugin_by_coord( &coord ) );
			print_coord_content( "at", &coord );
			print_inode( "dir", object );
			result = -EIO;
		} else
			result = item_plugin_by_coord( &coord ) -> 
				u.item.s.dir.rem_entry( object, 
							&coord, &lh, entry );
	}
	reiser4_done_lh( &lh );
	reiser4_done_coord( &coord );
	return result;
}


static int entry_actor( reiser4_tree *tree, 
			tree_coord *coord, reiser4_lock_handle *lh,
			void *args );

typedef struct entry_actor_args {
	const char  *name;
	reiser4_key *key;
	int          not_found;
	unsigned     non_uniq;
} entry_actor_args;

/**
 * Look for given @name within directory @dir.
 *
 * This is called during lookup, creation and removeal of directory
 * entries.
 *
 * First calculate key that directory entry for @name would have. Search
 * for this key in the tree. If such key is found, scan all items with
 * the same key, checking name in each directory entry along the way.
 *
 */
static int find_entry( const struct inode *dir, const struct qstr *name, 
		       tree_coord *coord, reiser4_lock_handle *lh,
		       znode_lock_mode mode, reiser4_entry *entry )
{
	int         result;

	assert( "nikita-1130", lh != NULL );
	assert( "nikita-1128", dir != NULL );
	assert( "nikita-1129", name != NULL );

	/*
	 * compose key of directory entry for @name
	 */
	result = build_entry_key( dir, name, &entry -> key );
	if( result != 0 )
		return result;

	result = coord_by_key( tree_by_inode( dir ), &entry -> key, coord, lh,
			       mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL );
	if( result == CBK_COORD_FOUND ) {
		entry_actor_args arg;

		/*
		 * Iterate through all units with the same keys.
		 */
		arg.name      = name -> name;
		arg.key       = &entry -> key;
		arg.not_found = 0;
		if( REISER4_STATS )
			arg.non_uniq = 0;
		result = reiser4_iterate_tree( tree_by_inode( dir ), 
					       coord, lh, entry_actor, &arg, 
					       mode, 1 );
		/*
		 * if end of the tree or extent was reached during
		 * scanning.
		 */
		if( arg.not_found || ( result == -ENAVAIL ) )
			result = -ENOENT;
	}
	return result;
}

/**
 * Function called by find_entry() to look for given name in the directory.
 */
static int entry_actor( reiser4_tree *tree UNUSED_ARG, tree_coord *coord, 
			reiser4_lock_handle *lh UNUSED_ARG,
			void *entry_actor_arg )
{
	reiser4_key       unit_key;
	reiser4_plugin   *plugin;
	entry_actor_args *args;

	assert( "nikita-1131", tree != NULL );
	assert( "nikita-1132", coord != NULL );
	assert( "nikita-1133", entry_actor_arg != NULL );

	args = entry_actor_arg;
	reiser4_stat_nuniq_max( ++ args -> non_uniq );
	if( keycmp( args -> key, 
		    unit_key_by_coord( coord, &unit_key ) ) != EQUAL_TO ) {
		args -> not_found = 1;
		coord -> between = BEFORE_UNIT;
		return 0;
	}
	plugin = item_plugin_by_coord( coord );
	if( plugin == NULL ) {
		warning( "nikita-1135", "Cannot get item plugin" );
		print_coord( "coord", coord, 1 );
		return -EIO;
	} else if( ( item_type_by_coord( coord ) != DIR_ENTRY_ITEM_TYPE ) ||
		   ( plugin -> h.type_id != REISER4_ITEM_PLUGIN_ID ) ) {
		warning( "nikita-1136", "Wrong item plugin" );
		print_coord( "coord", coord, 1 );
		print_plugin( "plugin", plugin );
		return -EIO;
	}
	assert( "nikita-1137", plugin -> u.item.s.dir.extract_name );

	trace_on( TRACE_DIR, "[%i]: \"%s\", \"%s\" in %lli\n",
		  current_pid, args -> name, 
		  plugin -> u.item.s.dir.extract_name( coord ),
		  znode_get_block( coord -> node ) -> blk );

	/*
	 * Compare name stored in this entry with name we are looking for.
	 * 
	 * FIXME-NIKITA Here should go code for support of something like
	 * unicode, code tables, etc.
	 */
	return !!strcmp( args -> name, 
			 plugin -> u.item.s.dir.extract_name( coord ) );
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

