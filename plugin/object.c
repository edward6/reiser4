/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
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


/** helper function to print errors */
static void key_warning( const char *error_message /* message to print */, 
			 const reiser4_key *key /* key to print */, 
			 int code /* error code to print */)
{
	assert( "nikita-716", key != NULL );
	
	warning( "nikita-717", "%s %i", error_message ? : "error for sd", code );
	print_key( "for key", key );
}


/** find sd of inode in a tree, deal with errors */
int lookup_sd( struct inode *inode /* inode to look sd for */, 
	       znode_lock_mode lock_mode /* lock mode */, 
	       new_coord *coord /* resulting coord */, 
	       lock_handle *lh /* resulting lock handle */, 
	       reiser4_key *key /* resulting key */ )
{
	assert( "nikita-1692", inode != NULL );
	assert( "nikita-1693", coord != NULL );
	assert( "nikita-1694", key != NULL );

	build_sd_key( inode, key );
	return lookup_sd_by_key( tree_by_inode( inode ), 
				 lock_mode, coord, lh, key );
}

/** find sd of inode in a tree, deal with errors */
int lookup_sd_by_key( reiser4_tree *tree /* tree to look in */, 
		      znode_lock_mode lock_mode /* lock mode */, 
		      new_coord *coord /* resulting coord */, 
		      lock_handle *lh /* resulting lock handle */, 
		      const reiser4_key *key /* resulting key */ )

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
			       lock_mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, 
			       CBK_UNIQUE | CBK_FOR_INSERT );
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
		assert( "nikita-1082", ncoord_is_existing_unit( coord ) );
		assert( "nikita-721", item_plugin_by_coord( coord ) != NULL );
		/* next assertion checks that item we found really has
		   the key we've been looking for */
		assert( "nikita-722", 
			keyeq( unit_key_by_coord( coord, &key_found ), key ) );
		assert( "nikita-1897", 
			znode_get_level( coord -> node ) == LEAF_LEVEL );
		/* check that what we really found is stat data */
		if( !item_is_statdata( coord ) ) {
			error_message = "sd found, but it doesn't look like sd ";
			print_plugin( "found", 
				      item_plugin_to_plugin( 
					      item_plugin_by_coord( coord ) ) );
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
static int insert_new_sd( struct inode *inode /* inode to create sd for */ )
{
	int result;
	reiser4_key key;
	new_coord coord;
	reiser4_item_data  data;
	const char *error_message;
	char *area;
	reiser4_inode_info *ref;
	lock_handle lh;
	oid_allocator_plugin *oplug;
	oid_t oid;


	assert( "nikita-723", inode != NULL );

	/* stat data is already there */
	if( !( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA ) )
		return 0;

	ref = reiser4_inode_data( inode );
	spin_lock_inode( ref );
	if( ref -> sd == NULL ) {
		ref -> sd = inode_sd_plugin( inode );
	}
	data.iplug = ref -> sd;
	data.length = ref -> sd_len;
	if( data.length == 0 ) {
		data.length = ref -> sd -> s.sd.save_len( inode );
		ref -> sd_len = data.length;
	}

	ref -> sd_len = data.length;
	spin_unlock_inode( ref );
	data.data = NULL;
	data.user = 0;

	assert( "vs-479", get_super_private( inode -> i_sb ) );
	oplug = get_super_private( inode -> i_sb ) -> oid_plug;
	assert( "vs-480", oplug && oplug -> allocate_oid );
	result = oplug -> allocate_oid( get_oid_allocator( inode -> i_sb ), 
					&oid );
	if( result != 0 )
		return result;

	inode -> i_ino = oid;

	ncoord_init_zero( &coord );
	init_lh( &lh );

	result = insert_by_key( tree_by_inode( inode ),
				build_sd_key( inode, &key ), &data, &coord, &lh,
				/* stat data lives on a leaf level */
				LEAF_LEVEL, 
				inter_syscall_ra( inode ), NO_RAP,
				CBK_UNIQUE );
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
	{
		result = zload( coord.node );
		if( result == 0 ) {
			assert( "nikita-725", /* have we really inserted stat
					       * data? */
				item_is_statdata( &coord ) );

			spin_lock_inode( ref );
			if( ref -> sd && ref -> sd -> s.sd.save ) {
				area = item_body_by_coord( &coord );
				result = ref -> sd -> s.sd.save( inode, &area );
				if( result == 0 ) {
					/* object has stat-data now */
					*reiser4_inode_flags( inode ) &= ~REISER4_NO_STAT_DATA;
					/* initialise stat-data seal */
					seal_init( &ref -> sd_seal, 
						   &coord, &key );
					ref -> sd_coord = coord;
				} else {
					error_message = "cannot save sd of";
					result = -EIO;
				}
			}
			spin_unlock_inode( ref );
			zrelse( coord.node );
		}
	}
	}
	done_lh( &lh );

	if( result != 0 )
		key_warning( error_message, &key, result );
	done_lh(&lh);
	return result;
}

/** Update existing stat-data in a tree. Called with inode state
    locked. Return inode state locked. */
static int update_sd( struct inode *inode /* inode to update sd for */ )
{
	int result;
	reiser4_key key;
	new_coord  coord;
	seal_t      seal;
	reiser4_item_data  data;
	const char *error_message;
	reiser4_inode_info *state;
	lock_handle lh;

	assert( "nikita-726", inode != NULL );

	/* no stat-data, nothing to update?! */
	if( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA )
		return -ENOENT;

	init_lh( &lh );

	state = reiser4_inode_data( inode );
	spin_lock_inode( state );
	coord = state -> sd_coord;
	seal  = state -> sd_seal;
	spin_unlock_inode( state );

	if( seal_is_set( &seal ) ) {
		/* first, try to use seal */
		build_sd_key( inode, &key );
		result = seal_validate( &seal, &coord, 
					&key, LEAF_LEVEL, &lh, FIND_EXACT, 
					ZNODE_WRITE_LOCK, ZNODE_LOCK_LOPRI );
		if( REISER4_DEBUG && ( result == 0 ) ) {
			reiser4_key ukey;

			if( !ncoord_is_existing_unit( &coord ) ||
			    !item_plugin_by_coord( &coord ) ||
			    !keyeq( unit_key_by_coord( &coord, &ukey ), &key ) ||
			    ( znode_get_level( coord.node ) != LEAF_LEVEL ) ||
			    !item_is_statdata( &coord ) ) {
				warning( "nikita-1901", "Conspicuous seal" );
				print_inode( "inode", inode );
				print_key( "key", &key );
				ncoord_print( "coord", &coord, 1 );
				result = -EIO;
			}
		}
	} else
		result = -EAGAIN;

	if( result != 0 ) {
		ncoord_init_zero( &coord );
		result = lookup_sd( inode, ZNODE_WRITE_LOCK, &coord, &lh, &key );
	}
	error_message = NULL;
	/* we don't want to re-check that somebody didn't remove stat-data
	   while we were doing io, because if it did, lookup_sd returned
	   error. */
	if( result == 0 && ( ( result = zload( coord.node ) ) == 0 ) ) {
		char *area;

		spin_lock_inode( state );
		assert( "nikita-728", state -> sd != NULL );
		data.iplug = state -> sd;

		if( state -> sd_len == 0 ) {
			/* recalculate stat-data length */
			state -> sd_len = 
				state -> sd -> s.sd.save_len( inode );

		}
		/* data.length is how much space to add to (or remove
		   from if negative) sd */
		data.length = state -> sd_len - item_length_by_coord( &coord );
		if( data.length < 0 ) {
			info( "sd_len: %i, item: %i\n", state -> sd_len, 
			      item_length_by_coord( &coord ) );
		}
		spin_unlock_inode( state );

		zrelse( coord.node );

		/* if on-disk stat data is of different length than required
		   for this inode, resize it */
		if( 0 != data.length ) {
			data.data = NULL;
			data.user = 0;
			/*
			 * FIXME-NIKITA resize can create new item.
			 */
			result = resize_item( &coord, &data, &key,
					      0/*FIXME-NIKITA lh?*/, 
					      0/*flags*/ );
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
		if( result == 0 && ( ( result = zload( coord.node ) ) == 0 ) ) {
			area = item_body_by_coord( &coord );
			spin_lock_inode( state );
			assert( "nikita-729", 
				item_length_by_coord( &coord ) == state -> sd_len );
			result = state -> sd -> s.sd.save( inode, &area );
			znode_set_dirty( coord.node );
			/* re-initialise stat-data seal */
			seal_init( &state -> sd_seal, &coord, &key );
			state -> sd_coord = coord;
			spin_unlock_inode( state );
			zrelse( coord.node );
		} else {
			key_warning( error_message, &key, result );
		}
	}
	done_lh( &lh );

	return result;
}

/** save object's stat-data to disk */
int common_file_save( struct inode *inode /* object to save */ )
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


int common_write_inode( struct inode *inode UNUSED_ARG )
{
	return -EINVAL;
}


/** checks whether yet another hard links to this object can be added */
int common_file_can_add_link( const struct inode *object /* object to check */ )
{
	assert( "nikita-732", object != NULL );

	return object -> i_nlink < ( ( nlink_t ) ~0 );
}

/** common_file_delete() - delete object stat-data */
int common_file_delete( struct inode *inode /* object to remove */, 
			struct inode *parent UNUSED_ARG /* parent object */ )
{
	int result;

	assert( "nikita-1477", inode != NULL );

	if( ! ( *reiser4_inode_flags( inode ) & REISER4_NO_STAT_DATA ) ) {
		reiser4_key sd_key;

		build_sd_key( inode, &sd_key );
		result = cut_tree( tree_by_inode( inode ), &sd_key, &sd_key );
		if( result == 0 ) {
			oid_allocator_plugin *oplug;

			*reiser4_inode_flags( inode ) &= ~REISER4_NO_STAT_DATA;
			assert( "nikita-1902", 
				get_super_private( inode -> i_sb ) );
			oplug = get_super_private( inode -> i_sb ) -> oid_plug;
			assert( "nikita-1903", ( oplug != NULL ) && 
				( oplug -> release_oid != NULL ) );
			result = oplug -> release_oid
				( get_oid_allocator( inode -> i_sb ), 
				  ( oid_t ) inode -> i_ino );
		}
	} else
		result = 0;
	return result;
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
int guess_plugin_by_mode( struct inode *inode /* object to guess plugins
					       * for */ )
{
	int fplug_id;
	int dplug_id;

	assert( "nikita-736", inode != NULL );

	dplug_id = fplug_id = -1;

	switch( inode -> i_mode & S_IFMT ) {
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		fplug_id = SPECIAL_FILE_PLUGIN_ID;
		break;
	case S_IFLNK:
		fplug_id = SYMLINK_FILE_PLUGIN_ID;
		break;
	case S_IFDIR:
		fplug_id = DIRECTORY_FILE_PLUGIN_ID;
		dplug_id = HASHED_DIR_PLUGIN_ID;
		break;
	default:
		warning( "nikita-737", "wrong file mode: %o", inode -> i_mode );
		return -EIO;
	case S_IFREG:
		fplug_id = REGULAR_FILE_PLUGIN_ID;
		break;
	}
	reiser4_inode_data( inode ) -> file = 
		( fplug_id >= 0 ) ? file_plugin_by_id( fplug_id ) : NULL;
	reiser4_inode_data( inode ) -> dir = 
		( dplug_id >= 0 ) ? dir_plugin_by_id( dplug_id ) : NULL;
	return 0;
}

/** standard implementation of ->owns_item() plugin method: compare objectids
    of keys in inode and coord */
int common_file_owns_item( const struct inode *inode /* object to check
						      * against */, 
			   const new_coord *coord /* coord to check */ )
{
	reiser4_key item_key;
	reiser4_key file_key;

	assert( "nikita-760", inode != NULL );
	assert( "nikita-761", coord != NULL );

	return /*coord_is_in_node( coord ) &&*/
		ncoord_is_existing_item (coord) &&
		( get_key_objectid( build_sd_key ( inode, &file_key ) ) ==
		  get_key_objectid( item_key_by_coord( coord, &item_key ) ) );
}

/*
 * @count bytes of flow @f got written, update correspondingly f->length,
 * f->data and f->key
 */
void move_flow_forward (flow_t * f, unsigned count)
{
	f->data += count;
	f->length -= count;
	set_key_offset (&f->key, get_key_offset (&f->key) + count);
}

/**
 * Default method to construct flow into @f according to user-supplied
 * data.
 */
int common_build_flow( struct inode *inode /* file to build flow for */, 
		       char *buf /* user level buffer */,
		       int user /* 1 if @buf is of user space, 0 - if it is
				   kernel space */,
		       size_t size /* buffer size */, 
		       loff_t off /* offset to start io from */, 
		       rw_op op UNUSED_ARG /* io operation */, 
		       flow_t *f /* resulting flow */ )
{
	file_plugin *fplug;

	assert( "nikita-1100", inode != NULL );

	f -> length = size;
	f -> data   = buf;
	f -> user   = user;
	fplug = inode_file_plugin( inode );
	assert( "nikita-1931", fplug != NULL );
	assert( "nikita-1932", fplug -> key_by_inode != NULL );
	return fplug -> key_by_inode( inode, off, &f -> key );
}

static int unix_key_by_inode ( struct inode *inode, loff_t off, reiser4_key *key )
{
	build_sd_key (inode, key);
	set_key_type (key, KEY_BODY_MINOR );
	set_key_offset (key, ( __u64 ) off);
	return 0;
}

/** actor function looking for any entry different from dot or dotdot. */
static int is_empty_actor( reiser4_tree *tree UNUSED_ARG /* tree scanned */,
			   new_coord *coord /* current coord */,
			   lock_handle *lh UNUSED_ARG /* current lock
						       * handle */, 
			   void *arg /* readdir arguments */ )
{
	struct inode *dir;
	file_plugin  *fplug;
	item_plugin  *iplug;
	char         *name;

	assert( "nikita-2004", tree != NULL );
	assert( "nikita-2005", coord != NULL );
	assert( "nikita-2006", arg != NULL );

	dir = arg;
	assert( "nikita-2003", dir != NULL );

	if( item_id_by_coord( coord ) !=
	    item_id_by_plugin( inode_dir_item_plugin( dir ) ) )
		return 0;

	fplug = inode_file_plugin( dir );
	if( ! fplug -> owns_item( dir, coord ) )
		return 0;

	iplug = item_plugin_by_coord( coord );
	name = iplug -> s.dir.extract_name( coord );
	assert( "nikita-1371", name != NULL );

	if( ( name[ 0 ] != '.' ) ||
	    ( ( name[ 1 ] != '.' ) && ( name[ 1 ] != '\0' ) ) )
		return -ENOTEMPTY;
	else
		return 1;
}

/** default ->rem_link() method of file plugin */
static int common_rem_link( struct inode *object )
{
	assert( "nikita-2021", object != NULL );
	assert( "nikita-2022", object -> i_nlink > 0 );
	assert( "nikita-2023", 
		! inode_file_plugin( object ) -> single_link( object ) );

	-- object -> i_nlink;
	object -> i_ctime = CURRENT_TIME;
	return inode_file_plugin( object ) -> write_sd_by_inode( object );
}

/** ->can_rem_link() method for file plugin of directories */
static int dir_can_rem_link( const struct inode *dir )
{
	reiser4_key de_key;
	int         result;
	struct qstr dot;
	new_coord  coord;
	lock_handle lh;

	assert( "nikita-1976", dir != NULL );

	/* Directory has to be empty. */

	/*
	 * FIXME-NIKITA this is not correct if hard links on directories are
	 * supported in this fs (if reiser4_adg( dir -> i_sb ) is false). But
	 * then, how to determine that last "outer" link is removed?
	 *
	 */

	dot.name = ".";
	dot.len  = 1;

	result = build_entry_key( dir, &dot, &de_key );
	if( result != 0 )
		return result;

	ncoord_init_zero( &coord );
	init_lh( &lh );
		
	/* 
	 * FIXME-NIKITA this looks almost exactly like code in
	 * readdir(). Consider implementing iterate_dir( dir, actor )
	 * function.
	 */
	result = coord_by_key( tree_by_inode( dir ), &de_key, &coord, &lh, 
			       ZNODE_READ_LOCK, FIND_MAX_NOT_MORE_THAN,
			       LEAF_LEVEL, LEAF_LEVEL, 0 );
	switch( result ) {
	case CBK_COORD_FOUND:
		result = iterate_tree( tree_by_inode( dir ), &coord, &lh, 
				       is_empty_actor, ( void * ) dir, 
				       ZNODE_READ_LOCK, 1 );
		switch( result ) {
		default:
		case -ENOTEMPTY:
			break;
		case 0:
		case -ENAVAIL:
			result = 0;
			break;
		}
		break;
	case CBK_COORD_NOTFOUND:
		/* no entries?! */
		warning( "nikita-2002", "Directory %lli is TOO empty",
			 ( __u64 ) dir -> i_ino );
		result = 0;
		break;
	default:
		/* some other error */
		break;
	}
	done_lh( &lh );

	return result;
}

/** ->single_link() method for file plugins */
int common_single_link( const struct inode *inode )
{
	assert( "nikita-2007", inode != NULL );
	return ( inode -> i_nlink == 1 );
}

/** ->single_link() method for directory file plugin */
int dir_single_link( const struct inode *inode )
{
	assert( "nikita-2008", inode != NULL );
	/* one link from dot */
	return ( inode -> i_nlink == 2 );
}


reiser4_plugin file_plugins[ LAST_FILE_PLUGIN_ID ] = {
	[ REGULAR_FILE_PLUGIN_ID ] = {
		.file = {
			.h = {
				.type_id = REISER4_FILE_PLUGIN_TYPE,
				.id      = REGULAR_FILE_PLUGIN_ID,
				.pops    = NULL,
				.label   = "reg",
				.desc    = "regular file",
				.linkage = TS_LIST_LINK_ZERO
			},
			.write_flow          = NULL,
			.read_flow           = NULL,
			.truncate            = unix_file_truncate,
			.write_sd_by_inode   = common_file_save,
			.readpage            = unix_file_readpage,
			.read                = unix_file_read,
			.write               = unix_file_write,
			.release             = unix_file_release,
			.flow_by_inode       = common_build_flow/*NULL*/,
			.key_by_inode        = unix_key_by_inode,
			.set_plug_in_sd      = NULL,
			.set_plug_in_inode   = NULL,
			.create_blank_sd     = NULL,
			.create              = unix_file_create,
			.destroy_stat_data   = common_file_delete,
			.add_link            = NULL,
			.rem_link            = common_rem_link,
			.owns_item           = unix_file_owns_item,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = NULL,
			.single_link         = common_single_link
		}
	},
	[ DIRECTORY_FILE_PLUGIN_ID ] = {
		.file = {
			.h = {
				.type_id = REISER4_FILE_PLUGIN_TYPE,
				.id      = DIRECTORY_FILE_PLUGIN_ID,
				.pops    = NULL,
				.label   = "dir",
				.desc    = "hashed directory",
				.linkage = TS_LIST_LINK_ZERO
			},
			.write_flow          = NULL,
			.read_flow           = NULL,
			.truncate            = NULL, /* EISDIR */
			.write_sd_by_inode   = common_file_save,
			.readpage            = NULL, /* EISDIR */
			.read                = NULL, /* EISDIR */
			.write               = NULL, /* EISDIR */
			.release             = NULL,
			.flow_by_inode       = NULL,
			.key_by_inode        = NULL,
			.set_plug_in_sd      = NULL,
			.set_plug_in_inode   = NULL,
			.create_blank_sd     = NULL,
			.create              = hashed_create,
			.destroy_stat_data   = hashed_delete,
			.add_link            = NULL,
			.rem_link            = common_rem_link,
			.owns_item           = hashed_owns_item,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = dir_can_rem_link,
			.single_link         = dir_single_link
		}
	},
	[ SYMLINK_FILE_PLUGIN_ID ] = {
		.file = {
			.h = {
				.type_id = REISER4_FILE_PLUGIN_TYPE,
				.id      = SYMLINK_FILE_PLUGIN_ID,
				.pops    = NULL,
				.label   = "symlink",
				.desc    = "symbolic link",
				.linkage = TS_LIST_LINK_ZERO
			},
			.write_flow          = NULL,
			.read_flow           = NULL,
			.truncate            = NULL,
			.write_sd_by_inode   = common_file_save,
			.readpage            = NULL,
			.read                = NULL,
			.write               = NULL,
			.release             = NULL,
			.flow_by_inode       = NULL,
			.key_by_inode        = NULL,
			.set_plug_in_sd      = NULL,
			.set_plug_in_inode   = NULL,
			.create_blank_sd     = NULL,
			.create              = NULL,
			/*
			 * FIXME-VS: symlink should probably have its own destroy method
			 */
			.destroy_stat_data   = common_file_delete,
			.add_link            = NULL,
			.rem_link            = common_rem_link,
			.owns_item           = NULL,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = NULL,
			.single_link         = common_single_link
		}
	},
	[ SPECIAL_FILE_PLUGIN_ID ] = {
		.file = {
			.h = {
				.type_id = REISER4_FILE_PLUGIN_TYPE,
				.id      = SPECIAL_FILE_PLUGIN_ID,
				.pops    = NULL,
				.label   = "special",
				.desc    = "special: fifo, device or socket",
				.linkage = TS_LIST_LINK_ZERO
			},
			.write_flow          = NULL,
			.read_flow           = NULL,
			.truncate            = NULL,
			.create              = NULL,
			.write_sd_by_inode   = common_file_save,
			.readpage            = NULL,
			.read                = NULL,
			.write               = NULL,
			.release             = NULL,
			.flow_by_inode       = NULL,
			.key_by_inode        = NULL,
			.set_plug_in_sd      = NULL,
			.set_plug_in_inode   = NULL,
			.create_blank_sd     = NULL,
			.destroy_stat_data   = common_file_delete,
			.add_link            = NULL,
			.rem_link            = common_rem_link,
			.owns_item           = common_file_owns_item,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = NULL,
			.single_link         = common_single_link
		}
	}
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
