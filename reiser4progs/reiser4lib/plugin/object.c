/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Examples of object plugins: file, directory, symlink, special file
 */
/*
 * Plugins associated with inode:
 *
 * Plugin of inode is plugin referenced by plugin-id field of on-disk
 * stat-data. How we store this plugin in in-core inode is not
 * important. Currently pointers are used, another variant is to store
 * offsets and do array lookup on each access.
 *
 * Now, each inode has one selected plugin: object plugin that
 * determines what type of file this object is: directory, regular etc.
 *
 * This main plugin can use other plugins that are thus subordinated to
 * it. Directory instance of object plugin uses hash; regular file
 * instance uses tail policy plugin.
 *
 * Object plugin is either taken from id in stat-data or guessed from
 * i_mode bits. Once it is established we ask it to install its
 * subordinate plugins, by looking again in stat-data or inheriting them
 * from parent.
 *
 */
/*
 * How new inode is initialized during ->read_inode():
 *  1 read stat-data and initialize inode fields: i_size, i_mode, 
 *    i_generation, capabilities etc.
 *  2 read plugin id from stat data or try to guess plugin id 
 *    from inode->i_mode bits if plugin id is missing.
 *  3 Call ->init_inode() method of stat-data plugin to initialise inode fields.
 *  4 Call ->activate() method of object's plugin. Plugin is either read from
 *    from stat-data or guessed from mode bits
 *  5 Call ->inherit() method of object plugin to inherit as yet initialized
 *    plugins from parent.
 *
 * Easy induction proves that on last step all plugins of inode would be
 * initialized.
 *
 * When creating new object:
 *  1 obtain object plugin id (see next period)
 *  2 ->install() this plugin
 *  3 ->inherit() the rest from the parent
 *
 */
/*

  We need some examples of creating an object with default and
  non-default plugin ids.  Nikita, please create them.
 
*/

#include "../reiser4.h"

/** inheritance function for objects that have nothing to inherit from
    parents */
static int no_inheritance( struct inode *inode UNUSED_ARG,
			   struct inode *parent UNUSED_ARG, 
			   struct inode *root UNUSED_ARG )
{
	return 0;
}

/** inheritance function for objects that inherit everything from their parents.
    There is a small overhead, because regular files don't need hash
    at all. Just one assignment. */
int total_inheritance( struct inode *inode, 
		       struct inode *parent, struct inode *root )
{
	reiser4_plugin_ref *self;
	reiser4_plugin_ref *ancestor;
	int changed;

	assert( "nikita-702", inode != NULL );
	assert( "nikita-703", ( parent != NULL ) || ( root != NULL ) );
	assert( "nikita-705", ( parent == NULL ) || S_ISDIR( parent -> i_mode ) );
	assert( "nikita-706", ( root == NULL ) || S_ISDIR( root -> i_mode ) );

	self = get_object_state( inode );
	ancestor = get_object_state( parent ? : root );
	changed = 0;

	changed |= inherit_if_nil( &self -> hash, &ancestor -> hash );
	changed |= inherit_if_nil( &self -> tail, &ancestor -> tail );
	changed |= inherit_if_nil( &self -> perm, &ancestor -> perm );

	/* all plugins should be initialised at this point */
	assert( "nikita-707", self -> hash != NULL );
	assert( "nikita-708", self -> tail != NULL );
	assert( "nikita-709", self -> perm != NULL );

	return changed;
}

static int is_file_empty( const struct inode *inode )
{
	assert( "nikita-710", inode != NULL );

	return( inode -> i_size == 0 );
}

/** part of initialisation of new inode common for all types of objects */
int common_file_install( struct inode *inode, reiser4_plugin *plug,
			 struct inode *parent, reiser4_object_create_data *data )
{
	assert( "nikita-711", inode != NULL );
	assert( "nikita-712", parent != NULL );
	assert( "nikita-713", plug != NULL );
	assert( "nikita-714", data != NULL );
	assert( "nikita-715", plug -> h.type_id == REISER4_FILE_PLUGIN_ID );

	inode -> i_mode = data -> mode;
	inode -> i_generation = new_inode_generation( inode -> i_sb );
	/* this should be plugin decision */
	inode -> i_uid = current -> fsuid;
	inode -> i_mtime = inode -> i_atime = inode -> i_ctime = CURRENT_TIME;
	
	if( parent -> i_mode & S_ISGID )
		inode -> i_gid = parent -> i_gid;
	else
		inode -> i_gid = current -> fsgid;

	get_object_state( inode ) -> file = plug;

	/* this object doesn't have stat-data yet */
	*reiser4_inode_flags( inode ) |= REISER4_NO_STAT_DATA;
	/* setup inode and file-operations for this inode */
	setup_inode_ops( inode );
	/* i_nlink is left 0 here. It'll be increased by ->add_link() */
	return 0;
}

/** helper function to print errors */
static void key_warning( const char *error_message, reiser4_key *key, int code )
{
	assert( "nikita-716", key != NULL );
	
	warning( "nikita-717", "%s %i", error_message ? : "error for sd", code );
	print_key( "for key", key );
}


/** find sd of inode in a tree, deal with errors */
int lookup_sd( struct inode *inode, znode_lock_mode lock_mode, 
	       tree_coord *coord, reiser4_lock_handle *lh, reiser4_key *key )
{
	assert( "nikita-1692", inode != NULL );
	assert( "nikita-1693", coord != NULL );
	assert( "nikita-1694", key != NULL );

	build_sd_key( inode, key );
	return lookup_sd_by_key( tree_by_inode( inode ), 
				 lock_mode, coord, lh, key );
}

/** find sd of inode in a tree, deal with errors */
int lookup_sd_by_key( reiser4_tree *tree, znode_lock_mode lock_mode, 
		      tree_coord *coord, reiser4_lock_handle *lh, 
		      reiser4_key *key )
{
	int   result;
	const char *error_message;
#if REISER4_DEBUG
	reiser4_key key_found;
#endif
	assert( "nikita-718", tree != NULL );
	assert( "nikita-719", coord != NULL );
	assert( "nikita-720", key != NULL );

	result        = 0;
	error_message = NULL;
	/* look for the object's stat data in a tree. 
	   This returns in "node" pointer to a locked znode and in "pos"
	   position of an item found in node. Both are only valid if
	   coord_found is returned. */
	result = coord_by_key( tree, key, coord, lh,
			       lock_mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL );
	switch( result ) {
	case CBK_OOM:
		error_message = "out of memory while looking for sd of";
		break;
	case CBK_IO_ERROR: 
		error_message = "io error while looking for sd of";
		break;
	case CBK_COORD_NOTFOUND:
		error_message = "sd not found for";
		break;
	default:
		/* something other, for which we don't want to print a message */
		break;
	case CBK_COORD_FOUND: {
		assert( "nikita-1082", coord_of_unit( coord ) );
		assert( "nikita-721", item_plugin_by_coord( coord ) != NULL );
		/* next assertion checks that item we found really has
		   the key we've been looking for */
		assert( "nikita-722", 
			keycmp( unit_key_by_coord( coord, &key_found ), 
				key ) == EQUAL_TO );
		/* check that what we really found is stat data */
		if( item_type_by_coord( coord ) != STAT_DATA_ITEM_TYPE ) {
			error_message = "sd found, but it doesn't look like sd ";
			print_plugin( "found", item_plugin_by_coord( coord ) );
			result = -ENOENT;
		}
	}
	}
	if( result != 0 )
		key_warning( error_message, key, result );
	return result;
}

/** insert new stat-data into tree. Called with inode state
    locked. Return inode state locked. */
static int insert_new_sd( struct inode *inode )
{
	int result;
	reiser4_key key;
	tree_coord coord;
	reiser4_item_data  data;
	const char *error_message;
	char *area;
	reiser4_plugin_ref *ref;
	reiser4_lock_handle lh;
	oid_t oid;

	assert( "nikita-723", inode != NULL );

	/* stat data is already there */
	if( !( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA ) )
		return 0;

	ref = get_object_state( inode );

	data.plugin = ref -> sd;
	if( data.plugin == NULL ) {
		data.plugin = get_sd_plugin( inode );
		ref -> sd = data.plugin;
	}
	data.length = ref -> sd_len;
	if( data.length == 0 ) {
		data.length = data.plugin -> u.item.s.sd.save_len( inode );
		ref -> sd_len = data.length;
	}

	ref -> sd_len = data.length;
	data.data = NULL;

	result = allocate_oid
		( get_oid_allocator( inode -> i_sb ), &oid );
	if( result != 0 )
		return result;

	inode -> i_ino = oid;

	init_coord( &coord );
	init_lh( &lh );

	result = insert_by_key( tree_by_inode( inode ),
				build_sd_key( inode, &key ), &data, &coord, &lh,
				/* stat data lives on a leaf level */
				LEAF_LEVEL, 
				inter_syscall_ra( inode ), NO_RA );
	/* we don't want to re-check that somebody didn't insert
	   stat-data while we were doing io, because if it did,
	   insert_by_key() returned error. */
	/* but what _is_ possible is that plugin for inode's stat-data,
	   list of non-standard plugins or their state would change
	   during io, so that stat-data wouldn't fit into sd. To avoid
	   this race we keep inode_state lock. This lock has to be
	   taken each time you access inode in a way that would cause
	   changes in sd size: changing plugins etc. */

	error_message = NULL;
	switch( result ) {
	case IBK_OOM:
		error_message = "out of memory while inserting sd of";
		break;
	case IBK_ALREADY_EXISTS:
		error_message = "sd already exists for";
		break;
	default: 
		/* something other, for which we don't want to print a message */
		break;
	case IBK_IO_ERROR:
		error_message = "io error while inserting sd of";
		break;
	case IBK_NO_SPACE:
		error_message = "no space while inserting sd of";
		break;
	case IBK_INSERT_OK:
		assert( "nikita-725", /* have we really inserted stat data? */
			item_type_by_coord( &coord ) == STAT_DATA_ITEM_TYPE );
		area = item_body_by_coord( &coord );
		result = data.plugin -> u.item.s.sd.save( inode, &area );
		if( result == 0 )
			/* object has stat-data now */
			*reiser4_inode_flags( inode ) &= ~REISER4_NO_STAT_DATA;
		else {
			error_message = "cannot save sd of";
			result = -EIO;
		}
	}
	done_lh( &lh );
	done_coord( &coord );
	if( result != 0 )
		key_warning( error_message, &key, result );
	done_lh(&lh);
	return result;
}

/** Update existing stat-data in a tree. Called with inode state
    locked. Return inode state locked. */
static int update_sd( struct inode *inode )
{
	int result;
	reiser4_key key;
	tree_coord coord;
	reiser4_item_data  data;
	const char *error_message;
	reiser4_plugin_ref *state;
	reiser4_lock_handle lh;

	assert( "nikita-726", inode != NULL );

	/* no stat-data, nothing to update?! */
	if( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA )
		return -ENOENT;

	init_coord( &coord );
	init_lh( &lh );

	result = lookup_sd( inode, ZNODE_WRITE_LOCK, &coord, &lh, &key );
	error_message = NULL;
	state = get_object_state( inode );
	/* we don't want to re-check that somebody didn't remove stat-data
	   while we were doing io, because if it did, lookup_sd returned
	   error. */
	if( result == 0 ) {
		char *area;

		data.plugin = state -> sd;
		assert( "nikita-728", data.plugin != NULL );

		if( state -> sd_len == 0 ) {
			/* recalculate stat-data length */
			state -> sd_len = 
				data.plugin -> u.item.s.sd.save_len( inode );
		}
		/* data.length is how much space to add to (or remove
		   from if negative) sd */
		data.length = state -> sd_len - item_length_by_coord( &coord );

		/* if on-disk stat data is of different length than required
		   for this inode, resize it */
		if( 0 != data.length ) {
			data.data = NULL;
			/*
			 * FIXME-NIKITA resize can create new item.
			 */
			result = resize_item( &coord, 
					      0/*FIXME-NIKITA lh?*/, 
					      &key, &data );
			switch( result ) {
			case RESIZE_OOM:
				error_message = "out of memory while resizing sd of";
				break;
			default: 
				break;
			case RESIZE_IO_ERROR:
				error_message = "io error while resizing sd of";
				break;
			case RESIZE_NO_SPACE:
				error_message = "no space to resize sd of";
				break;
			case RESIZE_OK:
			}
		}
		if( result == 0 ) {
			assert( "nikita-729", 
				item_length_by_coord( &coord ) == state -> sd_len );
			area = item_body_by_coord( &coord );
			result = data.plugin -> u.item.s.sd.save( inode, &area );
		} else
			key_warning( error_message, &key, result );
	}
	done_lh( &lh );
	done_coord( &coord );
	return result;
}

/** save object's stat-data to disk */
int common_file_save( struct inode *inode )
{
	int result;

	assert( "nikita-730", inode != NULL );
	
	if( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA )
		/* object doesn't have stat-data yet */
		result = insert_new_sd( inode );
	else
		result = update_sd( inode );
	return result;
}

/** checks whether yet another hard links to this object can be added */
int common_file_can_add_link( const struct inode *object )
{
	assert( "nikita-732", object != NULL );

	return object -> i_nlink < ( ( nlink_t ) ~0 );
}

/**
 * common_file_delete() - delete object stat-data
 *
 */
int common_file_delete( struct inode *inode, struct inode *parent UNUSED_ARG )
{
	int result;

	assert( "nikita-1477", inode != NULL );

	if( ! ( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA ) ) {
		reiser4_key sd_key;

		build_sd_key( inode, &sd_key );
		result = cut_tree( tree_by_inode( inode ), &sd_key, &sd_key );
		if( result == 0 )
			*reiser4_inode_flags( inode ) &= ~REISER4_NO_STAT_DATA;
	} else
		result = 0;
	return result;
}

/* 
 * regular file plugin methods. This should be moved into
 * fs/reiser4/plugin/file.c
 */

/** create sd for ordinary file. Just pass control to
    fs/reiser4/plugin/object.c:common_file_save() */
int ordinary_file_create( struct inode *object, struct inode *parent UNUSED_ARG,
			  reiser4_object_create_data *data UNUSED_ARG )
{
	assert( "nikita-744", object != NULL );
	assert( "nikita-745", parent != NULL );
	assert( "nikita-747", data != NULL );
	assert( "nikita-748", 
		*reiser4_inode_flags( object ) & REISER4_NO_STAT_DATA );
	assert( "nikita-749", data -> id == REGULAR_FILE_PLUGIN_ID );
	
	return common_file_save( object );
}

static int reserve_one_page( struct inode *inode UNUSED_ARG )
{
	return 1;
}

#define BALANCE_CNT   (12) /* stub */

static int reserve_one_balance( reiser4_object_create_data *data UNUSED_ARG )
{
	return BALANCE_CNT;
}

#define __reserve_one_balance ( ( void * ) reserve_one_balance )
#define __reserve_one_page ( ( void * ) reserve_one_page )

file_lookup_result noent( struct inode *inode UNUSED_ARG, 
			  const struct qstr *name UNUSED_ARG,
			  reiser4_key *key UNUSED_ARG, 
			  reiser4_entry *entry UNUSED_ARG )
{
	return FILE_NAME_NOTFOUND;
}

int is_name_acceptable( const struct inode *inode, const char *name UNUSED_ARG, 
			int len )
{
	assert( "nikita-733", inode != NULL );
	assert( "nikita-734", name != NULL );
	assert( "nikita-735", len > 0 );
	
	return len <= reiser4_max_filename_len( inode );
}

/**
 * Determine object plugin for @inode based on i_mode.
 *
 * Most objects in reiser4 file system are controlled by standard object
 * plugins: regular file, directory, symlink, fifo, and so on.
 *
 * For such files we don't explicitly store plugin id in object stat
 * data. Rather required plugin is guessed from mode bits, where file "type"
 * is encoded (see stat(2)).
 *
 */
reiser4_plugin *guess_plugin_by_mode( struct inode *inode )
{
	int result;

	assert( "nikita-736", inode != NULL );

	result = -1;
	switch( inode -> i_mode & S_IFMT ) {
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		result = SPECIAL_FILE_PLUGIN_ID;
		break;
	case S_IFLNK:
		result = SYMLINK_FILE_PLUGIN_ID;
		break;
	case S_IFDIR:
		result = DIRECTORY_FILE_PLUGIN_ID;
		break;
	default:
		warning( "nikita-737", "wrong file mode: %o", inode -> i_mode );
	case S_IFREG:
		result = REGULAR_FILE_PLUGIN_ID;
		break;
	}
	assert( "nikita-738", result >= 0 );
	return plugin_by_id( REISER4_FILE_PLUGIN_ID, result );
}

/** standard implementation of ->owns_item() plugin method: compare objectids
    of keys in inode and coord */
int common_file_owns_item( const struct inode *inode, const tree_coord *coord )
{
	reiser4_key item_key;
	reiser4_key file_key;

	assert( "nikita-760", inode != NULL );
	assert( "nikita-761", coord != NULL );

	return /*coord_is_in_node( coord ) &&*/
		coord_of_item (coord) &&
		( get_key_objectid( build_sd_key ( inode, &file_key ) ) ==
		  get_key_objectid( item_key_by_coord( coord, &item_key ) ) );
}

/**
 * Default method to construct flow into @f according to user-supplied
 * data.
 */
int common_build_flow( struct file *file UNUSED_ARG, char *buf, size_t size, 
		       loff_t *off, rw_op op UNUSED_ARG, flow *f )
{
	assert( "nikita-1100", f != NULL );
	assert( "nikita-1101", file != NULL );
	assert( "nikita-1102", file -> f_dentry != NULL );
	assert( "nikita-1103", file -> f_dentry -> d_inode != NULL );

	f -> length = size;
	f -> data   = buf;
	build_sd_key( file -> f_dentry -> d_inode, &f -> key );
	set_key_type( &f -> key, KEY_BODY_MINOR );
	set_key_offset( &f -> key, ( __u64 ) *off );
	return 0;
}

/**
 * Create child in directory.
 *
 * . get object's plugin
 * . get fresh inode
 * . initialize inode
 * . ask parent and plugin how much space to reserve and start transaction 
 * . add object's stat-data
 * . add entry to the parent
 * . end transaction
 * . instantiate dentry
 *
 */
static int common_create_child( struct inode *parent, struct dentry *dentry, 
				reiser4_object_create_data *data )
{
        int result;

	reiser4_plugin      *plugin;
        reiser4_file_plugin *fplug;
	struct inode        *object;
	int                  reserved;
	reiser4_entry        entry;

	assert( "nikita-1418", parent != NULL );
	assert( "nikita-1419", dentry != NULL );
	assert( "nikita-1420", data   != NULL );

	fplug = get_file_plugin( parent );
	/* check permissions */
	if( perm_chk( parent, create, parent, dentry, data ) )
		return -EPERM;

	/* check, that name is acceptable for parent */
	if( fplug -> is_name_acceptable && 
	    !fplug -> is_name_acceptable( parent, 
						  dentry -> d_name.name, 
						  (int) dentry -> d_name.len ) )
		return -ENAMETOOLONG;

	result = 0;
	plugin = plugin_by_id( REISER4_FILE_PLUGIN_ID, ( int ) data -> id );
	if( plugin == NULL ) {
		warning( "nikita-430", "Cannot find plugin %i", data -> id );
		return -ENOENT;
	}
	object = new_inode( parent -> i_sb );
	if( object == NULL )
		return -ENOMEM;
	memset( &entry, 0, sizeof entry );
	entry.obj = object;

	result = plugin -> u.file.install( object, plugin, parent, data );
	if( result ) {
		warning( "nikita-431", "Cannot install plugin %i on %lx", 
			 data -> id, ( long ) object -> i_ino );
		return result;
	}
       
	result = plugin -> u.file.inherit
		( object, parent, object -> i_sb -> s_root -> d_inode );
	if( result < 0 ) {
		warning( "nikita-432", "Cannot inherit from %lx to %lx", 
			 ( long ) parent -> i_ino, ( long ) object -> i_ino );
		return result;
	}

	/* reget plugin after installation */
	plugin = get_object_state( object ) -> file;
	get_object_state( object ) -> locality_id = parent -> i_ino;

	/* reserve space in transaction to add new entry to parent */
	reserved = fplug -> estimate.add( parent, dentry, data );
	/* reserve space in transaction to create stat-data */
	reserved += plugin -> u.file.estimate.create( data );
	/* if addition of new entry to the parent fails, we have to
	   remove stat-data just created, prepare for this. */
	reserved += plugin -> u.file.estimate.destroy( object );

	result = txn_reserve( reserved );
	if( result == 0 ) {
		/* mark inode immutable. We disable changes to the file
		   being created until valid directory entry for it is
		   inserted. Otherwise, if file were expanded and
		   insertion of directory entry fails, we have to remove
		   file, but we only alloted enough space in transaction
		   to remove _empty_ file. 3.x code used to remove stat
		   data in different transaction thus possibly leaking
		   disk space on crash. This all only matters if it's
		   possible to access file without name, for example, by
		   inode number */
		*reiser4_inode_flags( object ) |= REISER4_IMMUTABLE;
		/* create stat-data, this includes allocation of new
		   objectid (if we support objectid reuse). For
		   directories this implies creation of dot and
		   dotdot */
		result = plugin -> u.file.create( object, parent, data );
		if( result == 0 ) {
			assert( "nikita-434", !( *reiser4_inode_flags( object ) & 
						 REISER4_NO_STAT_DATA ) );
			/* insert inode into VFS hash table */
			insert_inode_hash( object );
			/* create entry */
			result = fplug -> add_entry( parent, dentry,
							     data, &entry );
			if( result != 0 ) {
				if( plugin -> u.file.destroy != NULL )
					/*
					 * failure to create entry,
					 * remove object
					 */
					plugin -> u.file.destroy( object, parent );
				else {
					warning( "nikita-1164",
						 "Cannot cleanup failed create: %i"
						 " Possible disk space leak.",
						 result );
				}
			}
		}
		/* file has name now, clear immutable flag */
		*reiser4_inode_flags( object ) &= ~REISER4_IMMUTABLE;
	}
	if( result != 0 ) {
		/* iput() will call ->delete_inode(). We should keep
		   track of the existence of stat-data for this inode
		   and avoid attempt to remove it in
		   reiser4_delete_inode(). This is accomplished through
		   REISER4_NO_STAT_DATA bit in
		   inode.u.reiser4_i.plugin.flags */
		iput( object );
	} else
		d_instantiate( dentry, object );
	return result;
}

typedef enum { 
	UNLINK_BY_DELETE, 
	UNLINK_BY_PLUGIN, 
	UNLINK_BY_NLINK 
} unlink_f_type;

/** 
 * remove link from @parent directory to @victim object.
 *
 *     . get plugins
 *     . find entry in @parent
 *     . check permissions
 *     . decrement nlink on @victim
 *     . if nlink drops to 0, delete object
 *     . close transaction
 */
static int common_unlink( struct inode *parent, struct dentry *victim )
{
	int                        result;
	struct inode              *object;
	reiser4_file_plugin       *fplug;
	reiser4_file_plugin       *parent_fplug;
	reiser4_entry              entry;
	int reserved;
	unlink_f_type              uf_type;

	assert( "nikita-864", parent != NULL );
	assert( "nikita-865", victim != NULL );

	object = victim -> d_inode;
	assert( "nikita-1239", object != NULL );

	fplug = get_file_plugin( object );

	/* check for race with create_object() */
	if( *reiser4_inode_flags( object ) & REISER4_IMMUTABLE )
		return -EAGAIN;

	/* check permissions */
	if( perm_chk( parent, unlink, parent, victim ) )
		return -EPERM;

	parent_fplug = get_file_plugin( parent );

	memset( &entry, 0, sizeof entry );

	reserved = parent_fplug -> estimate.rem( parent, victim );
	/* removing last reference. Check that this is allowed.  This is
	 * optimization for common case when file having only one name
	 * is unlinked and is not opened by any process. */
	if( ( object -> i_nlink == 1 ) && 
	    ( atomic_read( &object -> i_count ) == 1 ) ) {
		if( perm_chk( object, delete, parent, victim ) )
			return -EPERM;
		/* remove file body. This is probably done in a whole
		 * lot of transactions and takes a lot of time. We keep
		 * @object locked. So, nlink shouldn't change. */
		result = truncate_object( object, ( loff_t ) 0 );
		if( result != 0 )
			return result;
		assert( "nikita-871", object -> i_nlink == 1 );
		assert( "nikita-873", atomic_read( &object -> i_count ) == 1 );
		assert( "nikita-872", object -> i_size == 0 );
		reserved += fplug -> estimate.destroy( object );
		uf_type = UNLINK_BY_DELETE;
	} else if( fplug -> add_link ) {
		/* call plugin to do actual removal of link */
		uf_type = UNLINK_BY_PLUGIN;
		reserved += fplug -> estimate.rem_link( object );
	} else {
		/* do reasonable default stuff */
		reserved += fplug -> estimate.save( object );
		uf_type = UNLINK_BY_NLINK;
	}

	result = txn_reserve( reserved );
	if( result != 0 )
		return result;
	/* first, delete directory entry */
	result = parent_fplug -> rem_entry( parent, victim, &entry );
	if( result == 0 ) {
		switch( uf_type ) {
		case UNLINK_BY_DELETE:
			result = fplug -> destroy( object, parent );
			break;
		case UNLINK_BY_PLUGIN:
			result = fplug -> rem_link( object );
			break;
		case UNLINK_BY_NLINK:
			-- object -> i_nlink;
			object -> i_ctime = CURRENT_TIME;
			result = fplug -> save_sd( object );
		default:
			wrong_return_value( "nikita-1478", "uf_type" );
		}
	}
	return result;
}

/** 
 * add link from @parent directory to @existing object.
 *
 *     . get plugins
 *     . check permissions
 *     . check that "existing" can hold yet another link
 *     . reserve space and start transaction
 *     . add link to "existing"
 *     . add entry to "parent"
 *     . if last step fails, remove link from "existing"
 *     . close transaction
 *
 *    Less bread, more taxes!
 */
static int common_link( struct inode *parent, struct dentry *existing, 
			struct dentry *where )
{
	int                        result;
	struct inode              *object;
	reiser4_file_plugin       *fplug;
	reiser4_file_plugin       *parent_fplug;
	reiser4_entry              entry;
	int                        reserved;
	reiser4_object_create_data data;

	assert( "nikita-1431", existing != NULL );
	assert( "nikita-1432", parent != NULL );
	assert( "nikita-1433", where != NULL );

	object = existing -> d_inode;
	assert( "nikita-1434", object != NULL );

	fplug = get_file_plugin( object );

	/* check for race with create_object() */
	if( *reiser4_inode_flags( object ) & REISER4_IMMUTABLE )
		return -EAGAIN;

	/* links to directories are not allowed if file-system
	   logical name-space should be ADG */
	if( reiser4_adg( parent -> i_sb ) && S_ISDIR( object -> i_mode ) )
		return -EPERM;

	/* check permissions */
	if( perm_chk( parent, link, existing, parent, where ) )
		return -EPERM;

	parent_fplug = get_file_plugin( parent );

	memset( &entry, 0, sizeof entry );

	data.mode = object -> i_mode;
	data.id   = get_object_state( object ) -> file -> h.id;
	/* reserve space in transaction to add new entry to parent */
	reserved = parent_fplug -> estimate.add( parent, existing, &data );
	/* reserve space in transaction to add link and may be to remove link */
	if( fplug -> add_link ) {
		/* if there is optional ->add_link() method in this plugin,
		   use it */
		reserved += fplug -> estimate.add_link( object );
		reserved += fplug -> estimate.rem_link( object );
	} else
		/* otherwise, update stat-data, may be twice */
		reserved += 2 * fplug -> estimate.save( object );

	result = txn_reserve( reserved );
	if( result != 0 )
		/* cannot open transaction, chiao. */
		return result;
	result = reiser4_add_nlink( object );
	if( result == 0 ) {
		/* add entry to the parent */
		result = parent_fplug -> add_entry( parent, 
						     where, &data, &entry );
		if( result != 0 ) {
			/* failure to add entry to the parent, remove
			   link from "existing" */
			result = reiser4_del_nlink( object );
			/* now, if this fails, we have a file with too
			   big nlink---space leak, much better than
			   directory entry pointing to nowhere */
			/* may be it should be recorded somewhere, but
			   if addition of link to parent and update of
			   object's stat data both failed, chances are
			   that something is going really wrong */
		}
	}
	if( result == 0 ) {
		atomic_inc( &object -> i_count );
		d_instantiate( where, object );
	}
	return result;
}

reiser4_plugin file_plugins[ LAST_FILE_PLUGIN_ID ] = {
	[ REGULAR_FILE_PLUGIN_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_FILE_PLUGIN_ID,
			.id      = REGULAR_FILE_PLUGIN_ID,
			.pops    = NULL,
			.label   = "reg",
			.desc    = "regular file",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.file = {
				.install             = common_file_install,
				.activate            = NULL,
				.save_sd             = common_file_save,
				.estimate = {
					.rw          = __reserve_one_balance,
					.add         = 0,
					.rem         = 0,
					.create      = reserve_one_balance,
					.destroy     = __reserve_one_balance,
					.add_link    = __reserve_one_page,
					.rem_link    = __reserve_one_page,
					.save        = __reserve_one_balance
				},
				.create_child        = NULL,
				.create              = ordinary_file_create,
				.unlink              = NULL,
				.link                = NULL,
				.destroy             = common_file_delete,
				.add_entry           = NULL,
				.rem_entry           = NULL,
				.add_link            = NULL,
				.rem_link            = NULL,
				.can_add_link        = common_file_can_add_link,
				.inherit             = total_inheritance,
				.is_empty            = is_file_empty,
				.io                  = common_io,
				.rw_f = {
					[ READ_OP ]  = reiser4_ordinary_file_read,
					[ WRITE_OP ] = reiser4_ordinary_file_write,
				},
				.build_flow          = common_build_flow,
				.is_name_acceptable  = NULL,
				.lookup              = noent,
				.owns_item           = common_file_owns_item,
				.item_plugin_at      = NULL,
				.truncate            = reiser4_ordinary_file_truncate,
				.find_item	     = reiser4_ordinary_file_find_item,
				.readpage	     = reiser4_ordinary_readpage
			}
		}
	},
	[ DIRECTORY_FILE_PLUGIN_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_FILE_PLUGIN_ID,
			.id      = DIRECTORY_FILE_PLUGIN_ID,
			.pops    = NULL,
			.label   = "dir",
			.desc    = "hashed directory",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.file = {
				.install             = common_file_install,
				.activate            = NULL,
				.save_sd             = common_file_save,
				.estimate = {
					.rw          = 0,
					.add         = __reserve_one_balance,
					.rem         = __reserve_one_balance,
					.create      = reserve_one_balance,
					.destroy     = __reserve_one_balance,
					.add_link    = __reserve_one_page,
					.rem_link    = __reserve_one_page,
					.save        = __reserve_one_balance
				},
				.create_child        = common_create_child,
				.create              = hashed_create,
				.unlink              = common_unlink,
				.link                = common_link,
				.destroy             = hashed_delete,
				.add_entry           = hashed_add_entry,
				.rem_entry           = hashed_rem_entry,
				.add_link            = NULL,
				.rem_link            = NULL,
				.can_add_link        = common_file_can_add_link,
				.inherit             = total_inheritance,
				/* directory is empty iff it only contains
				   dot and dotdot. */
				/* is_directory_empty() will be declared
				   in dir/hashed_dir.h */
				.is_empty            = NULL,
				.io                  = NULL,
				.rw_f = {
					[ READ_OP ]  = NULL,
					[ WRITE_OP ] = NULL,
				},
				.build_flow          = common_build_flow,
				.is_name_acceptable  = is_name_acceptable,
				.lookup              = hashed_lookup,
				.owns_item           = hashed_owns_item,
				.item_plugin_at      = NULL,
				.truncate            = NULL,
				.find_item            = NULL,
				.readpage	      = NULL
			}
		}
	},
	[ SYMLINK_FILE_PLUGIN_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_FILE_PLUGIN_ID,
			.id      = SYMLINK_FILE_PLUGIN_ID,
			.pops    = NULL,
			.label   = "symlink",
			.desc    = "symbolic link",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.file = {
				.install             = common_file_install,
				.activate            = NULL,
				.save_sd             = common_file_save,
				.estimate = {
					.rw          = 0,
					.add         = 0,
					.rem         = 0,
					.create      = reserve_one_balance,
					.destroy     = __reserve_one_balance,
					.add_link    = __reserve_one_page,
					.rem_link    = __reserve_one_page,
					.save        = __reserve_one_balance
				},
				.create_child        = NULL,
				.create              = ordinary_file_create,
				.unlink              = NULL,
				.link                = NULL,
				.destroy             = common_file_delete,
				.add_entry           = NULL,
				.rem_entry           = NULL,
				.add_link            = NULL,
				.rem_link            = NULL,
				.can_add_link        = common_file_can_add_link,
				.inherit             = no_inheritance,
				.is_empty            = is_file_empty,
				.io                  = NULL,
				.rw_f = {
					[ READ_OP ]  = NULL,
					[ WRITE_OP ] = NULL,
				},
				.build_flow          = common_build_flow,
				.is_name_acceptable  = NULL,
				.lookup              = noent,
				.owns_item           = common_file_owns_item,
				.item_plugin_at      = NULL,
				.truncate            = NULL,
				.find_item            = NULL,
				.readpage	      = NULL
			}
		}
	},
	[ SPECIAL_FILE_PLUGIN_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_FILE_PLUGIN_ID,
			.id      = SPECIAL_FILE_PLUGIN_ID,
			.pops    = NULL,
			.label   = "special",
			.desc    = "special file: fifo, device or socket",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.file = {
				.install             = common_file_install,
				.activate            = NULL,
				.save_sd             = common_file_save,
				.estimate = {
					.rw          = 0,
					.add         = 0,
					.rem         = 0,
					.create      = reserve_one_balance,
					.destroy     = __reserve_one_balance,
					.add_link    = __reserve_one_page,
					.rem_link    = __reserve_one_page,
					.save        = __reserve_one_balance
				},
				.create_child        = NULL,
				.create              = ordinary_file_create,
				.unlink              = NULL,
				.link                = NULL,
				.destroy             = common_file_delete,
				.add_entry           = NULL,
				.rem_entry           = NULL,
				.add_link            = NULL,
				.rem_link            = NULL,
				.can_add_link        = common_file_can_add_link,
				.inherit             = total_inheritance,
				.is_empty            = is_file_empty,
				.io                  = NULL,
				.rw_f = {
					[ READ_OP ]  = NULL,
					[ WRITE_OP ] = NULL,
				},
				.build_flow          = common_build_flow,
				.is_name_acceptable  = NULL,
				.lookup              = noent,
				.owns_item           = common_file_owns_item,
				.item_plugin_at      = NULL,
				.truncate            = NULL,
				.find_item            = NULL,
				.readpage	      = NULL
			}
		}
	},
};

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
