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
		       znode_lock_mode mode, reiser4_dir_entry_desc *entry );

/** create sd for directory file. Create stat-data, dot, and dotdot. */
int hashed_create( struct inode *object /* new directory */, 
		   struct inode *parent /* parent directory */,
		   reiser4_object_create_data *data UNUSED_ARG /* info passed
								* to us, this
								* is filled by
								* reiser4()
								* syscall in
								* particular */)
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
	reiser4_dir_entry_desc entry;
	struct dentry goodby_dots;
	int           result;

	assert( "nikita-1449", object != NULL );

	xmemset( &entry, 0, sizeof entry );
	xmemset( &goodby_dots, 0, sizeof goodby_dots );

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
	reiser4_dir_entry_desc entry;

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

	xmemset( &entry, 0, sizeof entry );
	xmemset( &dots_entry, 0, sizeof dots_entry );
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
 * implementation of ->resolve_into_inode() method for hashed durectories.
 */
file_lookup_result hashed_lookup( struct inode *parent /* inode of directory to
							* lookup into */,
				  struct dentry *dentry )
{
	int                    result;
	tree_coord             coord;
	reiser4_lock_handle    lh;
	const char            *name;
	int                    len;
	reiser4_dir_entry_desc entry;
	
	assert( "nikita-1247", parent != NULL );
	assert( "nikita-1248", dentry != NULL );
	assert( "nikita-1123", dentry->d_name.name != NULL );
	
	/*
	 * we are trying to do finer grained locking than BKL. Lock inode in
	 * question and release BKL. Hopefully BKL was only taken once by VFS.
	 */
	if( reiser4_lock_inode_interruptible( parent ) != 0 )
		return -EINTR;

	if( perm_chk( parent, lookup, parent, dentry ) )
		return -EPERM;
	
	name = dentry -> d_name.name;
	len  = dentry -> d_name.len;
	

	if( !is_name_acceptable( parent, name, len ) ) {
		/* some arbitrary error code to return */
		return -ENAMETOOLONG;
	}

	/*
	 * set up operations on dentry. 
	 *
	 * FIXME-NIKITA this also has to be done for root dentry somewhere?
	 */
	dentry -> d_op = &reiser4_dentry_operation;

	init_coord( &coord );
	init_lh( &lh );

	/*
	 * find entry in a directory. This is plugin method.
	 */
	result = find_entry( parent, &dentry -> d_name, &coord, &lh,
			     ZNODE_READ_LOCK, &entry );
	if( result == 0 ) {
		/* entry was found, extract object key from it. */
		result = item_plugin_by_coord( &coord ) -> 
			s.dir.extract_key( &coord, &entry.key );
	}
	done_lh( &lh );
	done_coord( &coord );
	
	if( result == 0 ) {
		struct inode *inode;

		inode = reiser4_iget( parent -> i_sb, &entry.key );
		if( inode ) {
			__u32 *flags;
 
			flags = reiser4_inode_flags( inode );
			if( *flags & REISER4_LIGHT_WEIGHT_INODE ) {
				inode -> i_uid = parent -> i_uid;
				inode -> i_gid = parent -> i_gid;
				/* clear light-weight flag. If inode would be
				   read by any other name, [ug]id wouldn't
				   change. */
				*flags &= ~REISER4_LIGHT_WEIGHT_INODE;
			}
			/* success */
			d_add( dentry, inode );
			reiser4_unlock_inode( inode );
			result = 0;
		} else
			result = -EACCES;
	}

	reiser4_unlock_inode( parent );
	return result;
}

/**
 * ->add_entry() method for hashed directory object plugin.
 */
int hashed_add_entry( struct inode *object, struct dentry *where, 
		      reiser4_object_create_data *data UNUSED_ARG, 
		      reiser4_dir_entry_desc *entry )
{
	int                 result;
	tree_coord          coord;
	reiser4_lock_handle lh;

	assert( "nikita-1114", object != NULL );
	assert( "nikita-1250", where != NULL );

	init_coord( &coord );
	init_lh( &lh );

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
		assert( "nikita-1709", 
			item_plugin_by_id( REISER4_DIR_ITEM_PLUGIN ) -> item_type
			== DIR_ENTRY_ITEM_TYPE );
		result = item_plugin_by_id( REISER4_DIR_ITEM_PLUGIN ) -> 
			s.dir.add_entry( object, 
					 &coord, &lh, where, entry );
	} else if( result == 0 )
		result = -EEXIST;
	done_lh( &lh );
	done_coord( &coord );
	return result;
}

/**
 * ->rem_entry() method for hashed directory object plugin.
 */
int hashed_rem_entry( struct inode *object /* directory from which entry
					    * is begin removed */, 
		      struct dentry *where /* name that is being
					    * removed */, 
		      reiser4_dir_entry_desc *entry /* description of entry being
					    * removed */ )
{
	int                 result;
	tree_coord          coord;
	reiser4_lock_handle lh;

	assert( "nikita-1124", object != NULL );
	assert( "nikita-1125", where != NULL );

	init_coord( &coord );
	init_lh( &lh );

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
			print_plugin( "plugin", item_plugin_to_plugin (item_plugin_by_coord( &coord ) ) );
			print_coord_content( "at", &coord );
			print_inode( "dir", object );
			result = -EIO;
		} else
			result = item_plugin_by_coord( &coord ) -> 
				s.dir.rem_entry( object, 
							&coord, &lh, entry );
	}
	done_lh( &lh );
	done_coord( &coord );
	return result;
}


static int entry_actor( reiser4_tree *tree, 
			tree_coord *coord, reiser4_lock_handle *lh,
			void *args );

typedef struct entry_actor_args {
	const char  *name;
	reiser4_key *key;
#if REISER4_USE_COLLISION_LIMIT || REISER4_STATS
	int          max_non_uniq;
	int          non_uniq;
#endif
	int          not_found;
	znode_lock_mode mode;

	tree_coord          last_coord;
	reiser4_lock_handle last_lh;

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
		       znode_lock_mode mode, reiser4_dir_entry_desc *entry )
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
			       mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, 
			       CBK_FOR_INSERT );
	if( result == CBK_COORD_FOUND ) {
		entry_actor_args arg;

		/*
		 * Iterate through all units with the same keys.
		 */
		arg.name      = name -> name;
		arg.key       = &entry -> key;
		arg.not_found = 0;
#if REISER4_USE_COLLISION_LIMIT
		arg.non_uniq = 0;
		arg.max_non_uniq = max_hash_collisions( dir );
#endif
		arg.mode = mode;
		init_coord( &arg.last_coord );
		init_lh( &arg.last_lh );

		result = iterate_tree( tree_by_inode( dir ), 
					       coord, lh, entry_actor, &arg, 
					       mode, 1 );
		/*
		 * if end of the tree or extent was reached during
		 * scanning.
		 */
		if( arg.not_found || ( result == -ENAVAIL ) ) {
			/*
			 * step back
			 */
			done_lh( lh );
			done_coord( coord );

			dup_coord( coord, &arg.last_coord );
			move_lh( lh, &arg.last_lh );

			result = -ENOENT;
		}

		done_lh( &arg.last_lh );
		done_coord( &arg.last_coord );
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
	item_plugin      *iplug;
	entry_actor_args *args;

	assert( "nikita-1131", tree != NULL );
	assert( "nikita-1132", coord != NULL );
	assert( "nikita-1133", entry_actor_arg != NULL );

	args = entry_actor_arg;
#if REISER4_USE_COLLISION_LIMIT
	++ args -> non_uniq;
	reiser4_stat_nuniq_max( ( unsigned ) args -> non_uniq );
	if( args -> non_uniq > args -> max_non_uniq ) {
		args -> not_found = 1;
		/*
		 * hash collision overflow.
		 */
		return -EBUSY;
	}
#endif
	if( keycmp( args -> key, 
		    unit_key_by_coord( coord, &unit_key ) ) != EQUAL_TO ) {
		assert( "nikita-1791", 
			keycmp( args -> key, 
				unit_key_by_coord( coord, 
						   &unit_key ) ) == LESS_THAN );
		args -> not_found = 1;
		args -> last_coord.between = AFTER_UNIT;
		return 0;
	}
	iplug = item_plugin_by_coord( coord );
	if( iplug == NULL ) {
		warning( "nikita-1135", "Cannot get item plugin" );
		print_coord( "coord", coord, 1 );
		return -EIO;
	} else if( ( item_type_by_coord( coord ) != DIR_ENTRY_ITEM_TYPE ) ) {
		warning( "nikita-1136", "Wrong item plugin" );
		print_coord( "coord", coord, 1 );
		print_plugin( "plugin", item_plugin_to_plugin (iplug) );
		return -EIO;
	}
	assert( "nikita-1137", iplug -> s.dir.extract_name );

	done_coord( &args -> last_coord );
	dup_coord( &args -> last_coord, coord );
	if( args -> last_lh.node != lh -> node ) {
		int lock_result;

		done_lh( &args -> last_lh );
		assert( "", znode_is_any_locked( lh -> node ) );
		lock_result = longterm_lock_znode( &args -> last_lh, lh -> node,
						  args -> mode, 
						  ZNODE_LOCK_HIPRI );
		if( lock_result != 0 )
			return lock_result;
	}
	
	trace_on( TRACE_DIR, "[%i]: \"%s\", \"%s\" in %lli\n",
		  current_pid, args -> name, 
		  iplug -> s.dir.extract_name( coord ),
		  znode_get_block( coord -> node ) -> blk );

	/*
	 * Compare name stored in this entry with name we are looking for.
	 * 
	 * FIXME-NIKITA Here should go code for support of something like
	 * unicode, code tables, etc.
	 */
	return !!strcmp( args -> name, 
			 iplug -> s.dir.extract_name( coord ) );
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

