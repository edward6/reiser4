/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
 * file names to to files.
 */

#include "../../reiser4.h"

static int entry_actor( reiser4_tree *tree, 
			tree_coord *coord, reiser4_lock_handle *lh,
			void *cookie );

typedef struct entry_actor_args {
	const char  *name;
	reiser4_key *key;
	int          not_found;
	unsigned     non_uniq;
} entry_actor_args;

int hashed_dir_find( const struct inode *dir, const struct qstr *name, 
		     tree_coord *coord, reiser4_lock_handle *lh,
		     znode_lock_mode mode, reiser4_entry *entry )
{
	int         result;

	assert( "nikita-1130", lh != NULL );
	assert( "nikita-1128", dir != NULL );
	assert( "nikita-1129", name != NULL );

	result = build_entry_key( dir, name, &entry -> key );
	if( result != 0 )
		return result;

	result = coord_by_key( tree_by_inode( dir ), &entry -> key, coord, lh,
			       mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL );
	if( result == CBK_COORD_FOUND ) {
		entry_actor_args cookie;

		/*
		 * Iterate through all units with the same keys.
		 */
		cookie.name      = name -> name;
		cookie.key       = &entry -> key;
		cookie.not_found = 0;
		if( REISER4_STATS )
			cookie.non_uniq = 0;
		result = reiser4_iterate_tree( tree_by_inode( dir ), 
					       coord, lh, entry_actor, &cookie, 
					       mode, 1 );
		/*
		 * if end of the tree or extent was reached during
		 * scanning.
		 */
		if( cookie.not_found || ( result == -ENAVAIL ) )
			result = -ENOENT;
	}
	return result;
}

int hashed_dir_add( struct inode *dir, const struct dentry *where UNUSED_ARG,
		    tree_coord *coord, reiser4_lock_handle *lh,
		    reiser4_object_create_data *data UNUSED_ARG,
		    reiser4_entry *entry )
{
	/*
	 * FIXME-NIKITA hardcode reference to particular directory item
	 * plugin. This should be done in a way similar to sd.
	 */
	return plugin_by_id( REISER4_ITEM_PLUGIN_ID, REISER4_DIR_ITEM_PLUGIN ) -> 
		u.item.s.dir.add_entry( dir, coord, lh, where, entry );
}

int hashed_dir_rem( struct inode *dir, tree_coord *coord, 
		    reiser4_lock_handle *lh, reiser4_entry *entry )
{
	if( item_type_by_coord( coord ) != DIR_ENTRY_ITEM_TYPE ) {
		warning( "nikita-1161", "Non directory item found" );
		print_plugin( "plugin", item_plugin_by_coord( coord ) );
		print_coord_content( "at", coord );
		print_inode( "dir", dir );
		return -EIO;
	}

	return item_plugin_by_coord( coord ) -> 
		u.item.s.dir.rem_entry( dir, coord, lh, entry );
}

static int entry_actor( reiser4_tree *tree UNUSED_ARG, tree_coord *coord, 
			reiser4_lock_handle *lh UNUSED_ARG,
			void *cookie )
{
	reiser4_key       unit_key;
	reiser4_plugin   *plugin;
	entry_actor_args *args;

	assert( "nikita-1131", tree != NULL );
	assert( "nikita-1132", coord != NULL );
	assert( "nikita-1133", cookie != NULL );

	args = cookie;
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

