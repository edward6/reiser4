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
	
	warning( "nikita-717", "%llu: %s %i", 
		 get_key_objectid( key ), error_message ? : "error", code );
	print_key( "for key", key );
}


/** find sd of inode in a tree, deal with errors */
int lookup_sd( struct inode *inode /* inode to look sd for */, 
	       znode_lock_mode lock_mode /* lock mode */, 
	       coord_t *coord /* resulting coord */, 
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
		      coord_t *coord /* resulting coord */, 
		      lock_handle *lh /* resulting lock handle */, 
		      const reiser4_key *key /* resulting key */ )

{
	int   result;
	const char *error_message;
	__u32       flags;
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
	flags  = ( lock_mode == ZNODE_WRITE_LOCK ) ? CBK_FOR_INSERT : 0;
	flags |= CBK_UNIQUE;
	result = coord_by_key( tree, key, coord, lh,
			       lock_mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, 
			       flags );
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
		load_count lc = INIT_LOAD_COUNT_NODE( coord -> node );
;
		assert( "nikita-1082", WITH_DATA_RET
			( coord -> node, 1, coord_is_existing_unit( coord ) ) );
		assert( "nikita-721", WITH_DATA_RET
			( coord -> node, 1, item_plugin_by_coord( coord ) != NULL ) );
		/* next assertion checks that item we found really has the key
		 * we've been looking for */
		assert( "nikita-722", WITH_DATA_RET
			( coord -> node, 1, 
			  keyeq( unit_key_by_coord( coord, &key_found ), key ) ) );
		assert( "nikita-1897", 
			znode_get_level( coord -> node ) == LEAF_LEVEL );
		/* check that what we really found is stat data */
		result = incr_load_count( &lc );
		if( ( result = 0 ) && !item_is_statdata( coord ) ) {
			error_message = "sd found, but it doesn't look like sd ";
			print_plugin( "found", 
				      item_plugin_to_plugin( 
					      item_plugin_by_coord( coord ) ) );
			result = -ENOENT;
		}
		done_load_count( &lc );
		break;
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
	coord_t coord;
	reiser4_item_data  data;
	const char *error_message;
	char *area;
	reiser4_inode_info *ref;
	lock_handle lh;
	oid_t oid;


	assert( "nikita-723", inode != NULL );

	/* stat data is already there */
	if( !inode_get_flag( inode, REISER4_NO_STAT_DATA ) )
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

	if( data.length > tree_by_inode( inode ) -> nplug -> max_item_size() ) {
		/*
		 * This is silly check, but we don't know actual node where
		 * insertion will go into.
		 */
		return -ENAMETOOLONG;
	}
	result = oid_allocate( &oid );

	if( result != 0 )
		return result;

	inode -> i_ino = oid;

	coord_init_zero( &coord );
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
		if( result != 0 )
			break;

		assert( "nikita-725", /* have we really inserted stat
				       * data? */
			item_is_statdata( &coord ) );
		/*
		 * inode was just created. It is inserted into hash table, but
		 * no directory entry was yet inserted into parent. So, inode
		 * is inaccessible through ->lookup(). All places that
		 * directly grab inode from hash-table (like old knfsd),
		 * should check IMMUTABLE flag that is set by
		 * common_create_child.
		 */

		if( ref -> sd && ref -> sd -> s.sd.save ) {
			area = item_body_by_coord( &coord );
			result = ref -> sd -> s.sd.save( inode, &area );
			if( result == 0 ) {
				/* object has stat-data now */
				inode_clr_flag( inode, REISER4_NO_STAT_DATA );
				/* initialise stat-data seal */
				seal_init( &ref -> sd_seal, &coord, &key );
				ref -> sd_coord = coord;
			} else {
				error_message = "cannot save sd of";
				result = -EIO;
			}
		}
		zrelse( coord.node );
	}
	}
	done_lh( &lh );

	if( result != 0 )
		key_warning( error_message, &key, result );
	else 
		oid_count_allocated();

	return result;
}

/** Update existing stat-data in a tree. Called with inode state
    locked. Return inode state locked. */
static int update_sd( struct inode *inode /* inode to update sd for */ )
{
	int result;
	reiser4_key key;
	coord_t  coord;
	seal_t      seal;
	reiser4_item_data  data;
	const char *error_message;
	reiser4_inode_info *state;
	lock_handle lh;

	assert( "nikita-726", inode != NULL );

	/* no stat-data, nothing to update?! */
	if( inode_get_flag( inode, REISER4_NO_STAT_DATA ) )
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
		if( REISER4_DEBUG && ( result == 0 ) &&
		    ( ( result = zload( coord.node ) ) == 0 ) ) {
			reiser4_key ukey;

			if( !coord_is_existing_unit( &coord ) ||
			    !item_plugin_by_coord( &coord ) ||
			    !keyeq( unit_key_by_coord( &coord, &ukey ), &key ) ||
			    ( znode_get_level( coord.node ) != LEAF_LEVEL ) ||
			    !item_is_statdata( &coord ) ) {
				warning( "nikita-1901", "Conspicuous seal" );
				/*print_inode( "inode", inode );*/
				print_key( "key", &key );
				print_coord( "coord", &coord, 1 );
				result = -EIO;
			}
			zrelse( coord.node );
		}
	} else
		result = -EAGAIN;

	if( result != 0 ) {
		coord_init_zero( &coord );
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
			case RESIZE_OK:
			default: 
				break;
			case RESIZE_IO_ERROR:
				error_message = "io error while resizing sd of";
				break;
			case RESIZE_NO_SPACE:
				error_message = "no space to resize sd of";
				break;
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
	
	if( inode_get_flag( inode, REISER4_NO_STAT_DATA ) )
		/* object doesn't have stat-data yet */
		result = insert_new_sd( inode );
	else 
		result = update_sd( inode );
	if( result != 0 )
		warning( "nikita-2221", "Failed to save sd for %lu: %i (%lx)",
			 inode -> i_ino, result, 
			 reiser4_inode_data( inode ) -> flags );
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

	/*
	 * Problem is that nlink_t is usually short, which doesn't left room
	 * for many links, and, in particular to many sub-directoires (each
	 * sub-directory has dotdot counting as link in a parent).
	 *
	 * Possible work-around (read: kludge) is to implement special object
	 * plugin that will save "true nlink" in private inode
	 * parent. Stat-data (static_stat.c) is ready for 32bit nlink
	 * counters.
	 */
	return object -> i_nlink < ( ( ( nlink_t ) ~0 ) >> 1 );
}

/** common_file_delete() - delete object stat-data */
int common_file_delete( struct inode *inode /* object to remove */, 
			struct inode *parent UNUSED_ARG /* parent object */ )
{
	int result;

	assert( "nikita-1477", inode != NULL );

	if( !inode_get_flag( inode, REISER4_NO_STAT_DATA ) ) {
		reiser4_key sd_key;

		build_sd_key( inode, &sd_key );
		result = cut_tree( tree_by_inode( inode ), &sd_key, &sd_key );

		if( result ) return result;

		result = oid_release( ( oid_t ) inode -> i_ino );

		if (result) return result;

		oid_count_released();
	} else
		result = 0;
	return result;
}

/** ->set_plug_in_inode() default method. */
static int common_set_plug( struct inode *object /* inode to set plugin on */, 
			    struct inode *parent /* parent object */, 
			    reiser4_object_create_data *data /* creational
							      * data */ )
{
	object -> i_mode = data -> mode;
	object -> i_generation = new_inode_generation( object -> i_sb );
	/* this should be plugin decision */
	object -> i_uid = current -> fsuid;
	object -> i_mtime = object -> i_atime = object -> i_ctime = CURRENT_TIME;
	
	if( parent -> i_mode & S_ISGID )
		object -> i_gid = parent -> i_gid;
	else
		object -> i_gid = current -> fsgid;

	/* this object doesn't have stat-data yet */
	inode_set_flag( object, REISER4_NO_STAT_DATA );
	/* setup inode and file-operations for this inode */
	setup_inode_ops( object, data );
	/* i_nlink is left 1 here as set by new_inode() */
	seal_init( &reiser4_inode_data( object ) -> sd_seal, NULL, NULL );
	reiser4_inode_data( object ) -> extmask = ( 1 << UNIX_STAT );
	return 0;
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
			   const coord_t *coord /* coord to check */ )
{
	reiser4_key item_key;
	reiser4_key file_key;

	assert( "nikita-760", inode != NULL );
	assert( "nikita-761", coord != NULL );

	return /*coord_is_in_node( coord ) &&*/
		coord_is_existing_item (coord) &&
		( get_key_objectid( build_sd_key ( inode, &file_key ) ) ==
		  get_key_objectid( item_key_by_coord( coord, &item_key ) ) );
}

/*
 * @count bytes of flow @f got written, update correspondingly f->length,
 * f->data and f->key
 */
void move_flow_forward (flow_t * f, unsigned count)
{
	if (f->data)
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


/** default ->add_link() method of file plugin */
static int common_add_link( struct inode *object )
{
	++ object -> i_nlink;
	object -> i_ctime = CURRENT_TIME;
	return 0;
}

/** default ->rem_link() method of file plugin */
static int common_rem_link( struct inode *object )
{
	assert( "nikita-2021", object != NULL );
	assert( "nikita-2163", object -> i_nlink > 0 );

	-- object -> i_nlink;
	object -> i_ctime = CURRENT_TIME;
	return 0;
}

/** ->single_link() method for file plugins */
static int common_single_link( const struct inode *inode )
{
	assert( "nikita-2007", inode != NULL );
	return ( inode -> i_nlink == 1 );
}

/** ->single_link() method for directory file plugin */
static int dir_single_link( const struct inode *inode )
{
	assert( "nikita-2008", inode != NULL );
	/* one link from dot */
	return ( inode -> i_nlink == 2 );
}

#define grab_plugin( self, ancestor, plugin )			\
	if( ( self ) -> plugin == NULL )			\
		( self ) -> plugin = ( ancestor ) -> plugin

/** ->adjust_to_parent() method for regular files */
static int common_adjust_to_parent( struct inode *object /* new object */,
				    struct inode *parent /* parent directory */,
				    struct inode *root /* root directory */ )
{
	reiser4_inode_info *self;
	reiser4_inode_info *ancestor;

	assert( "nikita-2165", object != NULL );
	if( parent == NULL )
		parent = root;
	assert( "nikita-2069", parent != NULL );

	self     = reiser4_inode_data( object );
	ancestor = reiser4_inode_data( parent );

	grab_plugin( self, ancestor, file );
	grab_plugin( self, ancestor, sd );
	grab_plugin( self, ancestor, tail );
	grab_plugin( self, ancestor, perm );
	return 0;
}

/** ->adjust_to_parent() method for directory files */
static int dir_adjust_to_parent( struct inode *object /* new object */,
				 struct inode *parent /* parent directory */,
				 struct inode *root /* root directory */ )
{
	reiser4_inode_info *self;
	reiser4_inode_info *ancestor;

	assert( "nikita-2166", object != NULL );
	if( parent == NULL )
		parent = root;
	assert( "nikita-2167", parent != NULL );

	self     = reiser4_inode_data( object );
	ancestor = reiser4_inode_data( parent );

	grab_plugin( self, ancestor, file );
	grab_plugin( self, ancestor, dir );
	grab_plugin( self, ancestor, sd );
	grab_plugin( self, ancestor, hash );
	grab_plugin( self, ancestor, tail );
	grab_plugin( self, ancestor, perm );
	grab_plugin( self, ancestor, dir_item );
	return 0;
}

static loff_t dir_seek( struct file *file UNUSED_ARG, 
			loff_t offset UNUSED_ARG, int origin UNUSED_ARG )
{
	loff_t result;

	result = default_llseek( file, offset, origin );
	if( result >= 0 ) {
		reiser4_file_fsdata *fsdata;

		fsdata = reiser4_get_file_fsdata( file );
		fsdata -> dir.readdir_offset = ( __u64 ) 0;
		fsdata -> dir.skip = ( __u64 ) 0;
	}
	return result;
}

#define REISER4_PREFE

/** simplest implementation of ->getattr() method. Completely static. */
static int common_getattr( struct vfsmount *mnt UNUSED_ARG,
			   struct dentry *dentry, struct kstat *stat )
{
	struct inode *obj;

	assert( "nikita-2298", dentry != NULL );
	assert( "nikita-2299", stat != NULL );
	assert( "nikita-2300", dentry -> d_inode != NULL );

	obj = dentry -> d_inode;

	stat -> dev     = obj -> i_dev;
	stat -> ino     = obj -> i_ino;
	stat -> mode    = obj -> i_mode;
	stat -> nlink   = obj -> i_nlink;
	stat -> uid     = obj -> i_uid;
	stat -> gid     = obj -> i_gid;
	stat -> rdev    = kdev_t_to_nr( obj -> i_rdev );
	stat -> atime   = obj -> i_atime;
	stat -> mtime   = obj -> i_mtime;
	stat -> ctime   = obj -> i_ctime;
	stat -> size    = obj -> i_size;
	stat -> blocks  = obj -> i_blocks;
	stat -> blksize = REISER4_OPTIMAL_IO_SIZE( obj -> i_sb, obj );

	return 0;
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
			.writepage           = unix_file_writepage,
			.read                = unix_file_read,
			.write               = unix_file_write,
			.release             = unix_file_release,
			.mmap                = unix_file_mmap,
			.get_block           = unix_file_get_block,
			.flow_by_inode       = common_build_flow/*NULL*/,
			.key_by_inode        = unix_key_by_inode,
			.set_plug_in_inode   = common_set_plug,
			.adjust_to_parent    = common_adjust_to_parent,
			.create              = unix_file_create,
			.destroy_stat_data   = common_file_delete,
			.add_link            = common_add_link,
			.rem_link            = common_rem_link,
			.owns_item           = unix_file_owns_item,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = NULL,
			.single_link         = common_single_link,
			.setattr             = inode_setattr,
			.getattr             = common_getattr,
			.seek                = NULL
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
			.writepage           = NULL,
			.read                = NULL, /* EISDIR */
			.write               = NULL, /* EISDIR */
			.release             = NULL,
			.mmap                = NULL,
			.get_block           = NULL,
			.flow_by_inode       = NULL,
			.key_by_inode        = NULL,
			.set_plug_in_inode   = common_set_plug,
			.adjust_to_parent    = dir_adjust_to_parent,
			.create              = hashed_create,
			.destroy_stat_data   = hashed_delete,
			.add_link            = common_add_link,
			.rem_link            = common_rem_link,
			.owns_item           = hashed_owns_item,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = is_dir_empty,
			.single_link         = dir_single_link,
			.setattr             = inode_setattr,
			.getattr             = common_getattr,
			.seek                = dir_seek
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
			.writepage           = NULL,
			.read                = NULL,
			.write               = NULL,
			.release             = NULL,
			.mmap                = NULL,
			.get_block           = NULL,
			.flow_by_inode       = NULL,
			.key_by_inode        = NULL,
			.set_plug_in_inode   = common_set_plug,
			.adjust_to_parent    = common_adjust_to_parent,
			.create              = symlink_create,
			/*
			 * FIXME-VS: symlink should probably have its own destroy method
			 */
			.destroy_stat_data   = common_file_delete,
			.add_link            = common_add_link,
			.rem_link            = common_rem_link,
			.owns_item           = NULL,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = NULL,
			.single_link         = common_single_link,
			.setattr             = inode_setattr,
			.getattr             = common_getattr,
			.seek                = NULL
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
			.create              = unix_file_create,
			.write_sd_by_inode   = common_file_save,
			.readpage            = NULL,
			.writepage           = NULL,
			.read                = NULL,
			.write               = NULL,
			.release             = NULL,
			.mmap                = NULL,
			.get_block           = NULL,
			.flow_by_inode       = NULL,
			.key_by_inode        = NULL,
			.set_plug_in_inode   = common_set_plug,
			.adjust_to_parent    = common_adjust_to_parent,
			.destroy_stat_data   = common_file_delete,
			.add_link            = common_add_link,
			.rem_link            = common_rem_link,
			.owns_item           = common_file_owns_item,
			.can_add_link        = common_file_can_add_link,
			.can_rem_link        = NULL,
			.single_link         = common_single_link,
			.setattr             = inode_setattr,
			.getattr             = common_getattr,
			.seek                = NULL
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
