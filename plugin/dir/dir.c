/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Methods of directory plugin.
 */

/* version 3 has no directory read-ahead.  This is silly/wrong.  It
   would be nice if there was some commonality between file and
   directory read-ahead code, but I am not sure how well this can be
   done.  */

#include "../../reiser4.h"

/*
 * Directory read-ahead control.
 *
 * FIXME-NIKITA this is just stub. This function is supposed to be
 * called during lookup, readdir, and may be creation.
 *
 */
void directory_readahead( struct inode *dir /* directory being accessed */, 
			  coord_t *coord /* coord of access */ )
{
	assert( "nikita-1682", dir != NULL );
	assert( "nikita-1683", coord != NULL );
	assert( "nikita-1684", coord -> node != NULL );
	assert( "nikita-1685", znode_is_any_locked( coord -> node ) );

	trace_stamp( TRACE_DIR );
}

/** 
 * helper function. Standards require than for many file-system operations
 * on success ctime and mtime of parent directory is to be updated.
 */
static int update_dir( struct inode *parent )
{
	assert( "nikita-2525", parent != NULL );

	parent -> i_ctime = parent -> i_mtime = CURRENT_TIME;
	return reiser4_write_sd( parent );
}

typedef enum { 
	UNLINK_BY_DELETE, 
	UNLINK_BY_PLUGIN
} unlink_f_type;

/** 
 * add link from @parent directory to @existing object.
 *
 *     . get plugins
 *     . check permissions
 *     . check that "existing" can hold yet another link
 *     . start transaction
 *     . add link to "existing"
 *     . add entry to "parent"
 *     . if last step fails, remove link from "existing"
 *     . close transaction
 *
 */
static int common_link( struct inode *parent /* parent directory */, 
			struct dentry *existing /* dentry of object to which
						 * new link is being
						 * cerated */, 
			struct dentry *where /* new name */ )
{
	int                        result;
	struct inode              *object;
	file_plugin               *fplug;
	dir_plugin                *parent_dplug;
	reiser4_dir_entry_desc     entry;
	reiser4_object_create_data data;

	assert( "nikita-1431", existing != NULL );
	assert( "nikita-1432", parent != NULL );
	assert( "nikita-1433", where != NULL );

	object = existing -> d_inode;
	assert( "nikita-1434", object != NULL );

	fplug = inode_file_plugin( object );

	/* check for race with create_object() */
	if( inode_get_flag( object, REISER4_IMMUTABLE ) )
		return -EAGAIN;

	/* links to directories are not allowed if file-system
	   logical name-space should be ADG */
	if( reiser4_adg( parent -> i_sb ) && S_ISDIR( object -> i_mode ) )
		return -EISDIR;

	/* check permissions */
	if( perm_chk( parent, link, existing, parent, where ) )
		return -EPERM;

	parent_dplug = inode_dir_plugin( parent );

	xmemset( &entry, 0, sizeof entry );
	entry.obj = object;

	data.mode = object -> i_mode;
	data.id   = inode_file_plugin( object ) -> h.id;

	result = reiser4_add_nlink( object, 1 );
	if( result == 0 ) {
		/* add entry to the parent */
		result = parent_dplug -> add_entry( parent,
						    where, &data, &entry );
		if( result != 0 ) {
			/* failure to add entry to the parent, remove
			   link from "existing" */
			result = reiser4_del_nlink( object, 1 );
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
		/*
		 * Upon successful completion, link() shall mark for update
		 * the st_ctime field of the file. Also, the st_ctime and
		 * st_mtime fields of the directory that contains the new
		 * entry shall be marked for update. --SUS
		 */
		result = update_dir( parent );
	}
	return result;
}

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
/* Audited by: green(2002.06.15) */
static int common_unlink( struct inode *parent /* parent object */, 
			  struct dentry *victim /* name being removed from
						 * @parent */ )
{
	int                        result;
	struct inode              *object;
	file_plugin               *fplug;
	dir_plugin                *parent_dplug;
	reiser4_dir_entry_desc     entry;
	unlink_f_type              uf_type;

	assert( "nikita-864", parent != NULL );
	assert( "nikita-865", victim != NULL );

	object = victim -> d_inode;
	assert( "nikita-1239", object != NULL );

	fplug = inode_file_plugin( object );

	/* check for race with create_object() */
	if( inode_get_flag( object, REISER4_IMMUTABLE ) )
		return -EAGAIN;

	/* check permissions */
	if( perm_chk( parent, unlink, parent, victim ) )
		return -EPERM;

	/* ask object plugin */
	if( fplug -> can_rem_link != NULL ) {
		result = fplug -> can_rem_link( object );
		if( result != 0 )
			return result;
	}

	parent_dplug = inode_dir_plugin( parent );

	xmemset( &entry, 0, sizeof entry );

	/* removing last reference. Check that this is allowed.  This is
	 * optimization for common case when file having only one name
	 * is unlinked and is not opened by any process. */
	/*
	 * FIXME-NIKITA disable (temporarily): ->i_count is not properly
	 * locked.
	 */
	if( 0 && fplug -> single_link( object ) && 
	    ( atomic_read( &object -> i_count ) == 1 ) ) {
		if( perm_chk( object, delete, parent, victim ) )
			return -EPERM;
		/* remove file body. This is probably done in a whole
		 * lot of transactions and takes a lot of time. We keep
		 * @object locked. So, nlink shouldn't change. */
		if( fplug -> truncate != NULL ) {
			result = truncate_object( object, ( loff_t ) 0 );
			if( result != 0 )
				return result;
		}
		assert( "nikita-871", fplug -> single_link( object ) );
		assert( "nikita-873", atomic_read( &object -> i_count ) == 1 );

		uf_type = UNLINK_BY_DELETE;
	} else if( fplug -> rem_link == 0 )
		return -EPERM;
	else
		uf_type = UNLINK_BY_PLUGIN;

	/* first, delete directory entry */
	result = parent_dplug -> rem_entry( parent, victim, &entry );
	if( result == 0 ) {
		/* and second, remove or update stat data */
		switch( uf_type ) {
		case UNLINK_BY_DELETE:
			-- object -> i_nlink;
			result = fplug -> destroy_stat_data( object, parent );
			break;
		case UNLINK_BY_PLUGIN:
			result = reiser4_del_nlink( object, 1 );
			break;
		default:
			wrong_return_value( "nikita-1478", "uf_type" );
		}
	}
	/*
	 * Upon successful completion, unlink() shall mark for update the
	 * st_ctime and st_mtime fields of the parent directory. Also, if the
	 * file's link count is not 0, the st_ctime field of the file shall be
	 * marked for update. --SUS
	 */
	if( result == 0 )
		result = update_dir( parent );
		/*
		 * @object's i_ctime was updated by ->rem_link() method().
		 */
	return result;
}

/**
 * Create child in directory.
 *
 * . get object's plugin
 * . get fresh inode
 * . initialize inode
 * . start transaction 
 * . add object's stat-data
 * . add entry to the parent
 * . end transaction
 * . instantiate dentry
 *
 */
/* Audited by: green(2002.06.15) */
static int common_create_child( struct inode *parent /* parent object */, 
				struct dentry *dentry /* new name */, 
				reiser4_object_create_data *data /* parameters
								  * of new
								  * object */)
{
        int result;

        dir_plugin          *dplug;
	file_plugin         *fplug;
	struct inode        *object;
	reiser4_dir_entry_desc        entry;

	assert( "nikita-1418", parent != NULL );
	assert( "nikita-1419", dentry != NULL );
	assert( "nikita-1420", data   != NULL );

	dplug = inode_dir_plugin( parent );
	/* check permissions */
	if( perm_chk( parent, create, parent, dentry, data ) ) {
		return -EPERM;
	}

	/* check, that name is acceptable for parent */
	if( dplug -> is_name_acceptable && 
	    !dplug -> is_name_acceptable( parent, 
					  dentry -> d_name.name, 
					  (int) dentry -> d_name.len ) ) {
		return -ENAMETOOLONG;
	}

	result = 0;
	fplug = file_plugin_by_id( ( int ) data -> id );
	if( fplug == NULL ) {
		warning( "nikita-430", "Cannot find plugin %i", data -> id );
		return -ENOENT;
	}
	object = new_inode( parent -> i_sb );
	if( object == NULL )
		return -ENOMEM;
	xmemset( &entry, 0, sizeof entry );
	entry.obj = object;
	dentry -> d_inode = object; // So that on error iput will be called.

	reiser4_inode_data( object ) -> file = fplug;
	result = fplug -> set_plug_in_inode( object, parent, data );
	if( result ) {
		warning( "nikita-431", "Cannot install plugin %i on %llx", 
			 data -> id, get_inode_oid( object ) );
		return result;
	}

	/* reget plugin after installation */
	fplug = inode_file_plugin( object );

	if ( !fplug->create ) {
		return -EPERM;
	}

	/*
	 * if any of hash, tail, sd or permission plugins for newly created
	 * object are not set yet set them here inheriting them from parent
	 * directory
	 */
	assert( "nikita-2070", fplug -> adjust_to_parent != NULL );
	result = fplug -> adjust_to_parent
		( object, parent, object -> i_sb -> s_root -> d_inode );
	if( result != 0 ) {
		warning( "nikita-432", "Cannot inherit from %llx to %llx", 
			 get_inode_oid( parent ), get_inode_oid( object ) );
		return result;
	}

	reiser4_inode_data( object ) -> locality_id = get_inode_oid( parent );

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
	inode_set_flag( object, REISER4_IMMUTABLE );

	/* create empty object, this includes allocation of new objectid. For
	   directories this implies creation of dot and dotdot */
	assert( "nikita-2265", inode_get_flag( object, REISER4_NO_STAT_DATA ) );

	result = fplug -> create( object, parent, data );
	if( result == 0 ) {
		assert( "nikita-434", !inode_get_flag( object,
						       REISER4_NO_STAT_DATA ) );
		/* insert inode into VFS hash table */
		insert_inode_hash( object );
		/* create entry */
		result = dplug -> add_entry( parent, dentry, data, &entry );
		if( result == 0 ) {
			/*
			 * If O_CREAT is set and the file did not previously
			 * exist, upon successful completion, open() shall
			 * mark for update the st_atime, st_ctime, and
			 * st_mtime fields of the file and the st_ctime and
			 * st_mtime fields of the parent directory. --SUS
			 */
			/*
			 * @object times are already updated by ->set_plug()
			 */
			result = update_dir( parent );
		} else {
			if( fplug -> destroy_stat_data != NULL ) {
				/*
				 * failure to create entry,
				 * remove object
				 */
				fplug -> destroy_stat_data( object, parent );
			} else {
				warning( "nikita-1164",
					 "Cannot cleanup failed create: %i"
					 " Possible disk space leak.",
					 result );
			}
		}
	} else {
		warning( "nikita-2219", "Failed to create sd for %llu (%lx)",
			 get_inode_oid( object ), 
			 reiser4_inode_data( object ) -> flags );
	}

	/* file has name now, clear immutable flag */
	inode_clr_flag( object, REISER4_IMMUTABLE );

	/* 
	 * on error, iput() will call ->delete_inode(). We should keep track
	 * of the existence of stat-data for this inode and avoid attempt to
	 * remove it in reiser4_delete_inode(). This is accomplished through
	 * REISER4_NO_STAT_DATA bit in inode.u.reiser4_i.plugin.flags
	 */
	return result;
}

/** ->is_name_acceptable() method of directory plugin */
/* Audited by: green(2002.06.15) */
int is_name_acceptable( const struct inode *inode /* directory to check */, 
			const char *name UNUSED_ARG /* name to check */, 
			int len /* @name's length */)
{
	assert( "nikita-733", inode != NULL );
	assert( "nikita-734", name != NULL );
	assert( "nikita-735", len > 0 );
	
	return len <= reiser4_max_filename_len( inode );
}

/** actor function looking for any entry different from dot or dotdot. */
static int is_empty_actor( reiser4_tree *tree UNUSED_ARG /* tree scanned */,
			   coord_t *coord /* current coord */,
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
	assert( "nikita-2162", name != NULL );

	if( ( name[ 0 ] != '.' ) ||
	    ( ( name[ 1 ] != '.' ) && ( name[ 1 ] != '\0' ) ) )
		return -ENOTEMPTY;
	else
		return 1;
}

/** true if directory is empty (only contains dot and dotdot) */
int is_dir_empty( const struct inode *dir )
{
	reiser4_key de_key;
	int         result;
	struct qstr dot;
	coord_t  coord;
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

	result = inode_dir_plugin( dir ) -> entry_key( dir, &dot, &de_key );
	if( result != 0 )
		return result;

	coord_init_zero( &coord );
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
			 get_inode_oid( dir ) );
		result = 0;
		break;
	default:
		/* some other error */
		break;
	}
	done_lh( &lh );

	return result;
}

/** compare two logical positions within the same directory */
cmp_t dir_pos_cmp( const dir_pos *p1, const dir_pos *p2 )
{
	cmp_t result;

	assert( "nikita-2534", p1 != NULL );
	assert( "nikita-2535", p2 != NULL );

	result = de_id_cmp( &p1 -> dir_entry_key, &p2 -> dir_entry_key );
	if( result == EQUAL_TO ) {
		int diff;

		diff = p1 -> pos - p2 -> pos;
		result = 
			( diff < 0 ) ? LESS_THAN : 
			( diff ? GREATER_THAN : EQUAL_TO );
	}
	return result;
}

void adjust_dir_pos( struct file *dir, readdir_pos *readdir_spot, 
		     const dir_pos *mod_point, int adj )
{
	dir_pos *pos;

	reiser4_stat_dir_add( readdir.adjust_pos );
	pos = &readdir_spot -> position;
	switch( dir_pos_cmp( mod_point, pos ) ) {
	case LESS_THAN:
		readdir_spot -> entry_no += adj;
		lock_kernel();
		assert( "nikita-2577", dir -> f_pos + adj >= 0 );
		dir -> f_pos += adj;
		unlock_kernel();
		if( de_id_cmp( &pos -> dir_entry_key, 
			       &mod_point -> dir_entry_key ) == EQUAL_TO ) {
			assert( "nikita-2575", mod_point -> pos < pos -> pos );
			pos -> pos += adj;
		}
		reiser4_stat_dir_add( readdir.adjust_lt );
		break;
	case GREATER_THAN:
		/*
		 * directory is modified after @pos: nothing to do.
		 */
		reiser4_stat_dir_add( readdir.adjust_gt );
		break;
	case EQUAL_TO:
		/*
		 * cannot insert an entry readdir is looking at, because it
		 * already exists.
		 */
		assert( "nikita-2576", adj < 0 );
		/*
		 * directory entry to which @pos points to is being
		 * removed. 
		 *
		 * FIXME-NIKITA: Right thing to do is to update @pos to point
		 * to the next entry. This is complex (we are under spin-lock
		 * for one thing). Just rewind it to the beginning. Next
		 * readdir will have to scan the beginning of
		 * directory. Proper solution is to use semaphore in
		 * spin lock's stead and use rewind_right() here.
		 */
		xmemset( readdir_spot, 0, sizeof *readdir_spot );
		reiser4_stat_dir_add( readdir.adjust_eq );
	}
}

/**
 * scan all file-descriptors for this directory and adjust their positions
 * respectively.
 */
void adjust_dir_file( struct inode *dir, const coord_t *coord,
		      int offset, int adj )
{
	reiser4_file_fsdata *scan;
	reiser4_inode  *info;
	reiser4_key          de_key;
	dir_pos              mod_point;

	assert( "nikita-2536", dir != NULL );
	assert( "nikita-2538", coord != NULL );
	assert( "nikita-2539", adj != 0 );

	WITH_DATA( coord -> node, unit_key_by_coord( coord, &de_key ) );
	build_de_id_by_key( &de_key, &mod_point.dir_entry_key );
	mod_point.pos = offset;

	info  = reiser4_inode_data( dir );
	spin_lock( &info -> guard );
	for( scan = readdir_list_front( &info -> readdir_list ) ;
	     !readdir_list_end( &info -> readdir_list, scan ) ;
	     scan = readdir_list_next( scan ) ) {
		adjust_dir_pos( scan -> back, 
				&scan -> dir.readdir, &mod_point, adj );
	}
	spin_unlock( &info -> guard );
}

static int dir_go_to( struct file *dir, readdir_pos *pos, tap_t *tap )
{
	reiser4_key   key;
	int           result;
	struct inode *inode;

	assert( "nikita-2554", pos != NULL );

	inode = dir -> f_dentry -> d_inode;
	result = inode_dir_plugin( inode ) -> readdir_key( dir, &key );
	if( result != 0 )
		return result;
	result = coord_by_key( tree_by_inode( inode ), &key, 
			       tap -> coord, tap -> lh, tap -> mode, 
			       FIND_MAX_NOT_MORE_THAN,
			       LEAF_LEVEL, LEAF_LEVEL, 0 );
	if( result == CBK_COORD_FOUND )
		result = rewind_right( tap, ( int ) pos -> position.pos );
	return result;
}

static int dir_rewind( struct file *dir, readdir_pos *pos,
		       loff_t offset, tap_t *tap )
{
	__u64 destination;
	int   shift;
	int   result;

	assert( "nikita-2553", dir != NULL );
	assert( "nikita-2548", pos != NULL );
	assert( "nikita-2551", tap -> coord != NULL );
	assert( "nikita-2552", tap -> lh != NULL );

	if( offset < 0 )
		return -EINVAL;
	else if( offset == 0ll ) {
		/*
		 * rewind to the beginning of directory
		 */
		xmemset( pos, 0, sizeof *pos );
		reiser4_stat_dir_add( readdir.reset );
		return dir_go_to( dir, pos, tap );
	}

	destination = ( __u64 ) offset;

	shift = pos -> entry_no - destination;
	if( unlikely( abs( shift ) > 100000 ) )
		/*
		 * something strange: huge seek
		 */
		warning( "nikita-2549", "Strange seekdir: %llu->%llu",
			 pos -> entry_no, destination );
	if( shift >= 0 ) {
		/*
		 * rewinding to the left
		 */
		reiser4_stat_dir_add( readdir.rewind_left );
		if( shift <= ( int ) pos -> position.pos ) {
			/*
			 * destination is within sequence of entries with
			 * duplicate keys.
			 */
			pos -> position.pos -= shift;
			reiser4_stat_dir_add( readdir.left_non_uniq );
			result = dir_go_to( dir, pos, tap );
		} else {
			shift -= pos -> position.pos;
			pos -> position.pos = 0;
			while( 1 ) {
				/*
				 * repetitions: deadlock is possible when
				 * going to the left.
				 */
				result = dir_go_to( dir, pos, tap );
				if( result == 0 ) {
					result = rewind_left( tap, shift );
					if( result == -EDEADLK ) {
						tap_done( tap );
						reiser4_stat_dir_add( readdir.left_restart );
						continue;
					}
				}
			}
		}
	} else {
		/*
		 * rewinding to the right
		 */
		reiser4_stat_dir_add( readdir.rewind_right );
		result = dir_go_to( dir, pos, tap );
		if( result == 0 )
			result = rewind_right( tap, -shift );
	}
	return result;
}

/**
 * Function that is called by common_readdir() on each directory item
 * while doing readdir.
 */
static int feed_entry( readdir_pos *pos, 
		       coord_t *coord, filldir_t filldir, void *dirent )
{
	item_plugin *iplug;
	char        *name;
	reiser4_key  sd_key;
	reiser4_key  de_key;
	int          result;
	de_id       *did;

	iplug = item_plugin_by_coord( coord );

	name = iplug -> s.dir.extract_name( coord );
	assert( "nikita-1371", name != NULL );
	if( iplug -> s.dir.extract_key( coord, &sd_key ) != 0 )
		return -EIO;

	/* get key of directory entry */
	unit_key_by_coord( coord, &de_key );
	trace_on( TRACE_DIR | TRACE_VFS_OPS, "readdir: %s, %llu, %llu\n",
		  name, pos -> entry_no + 1, get_key_objectid( &sd_key ) );

	/*
	 * update @pos
	 */
	++ pos -> entry_no;
	did = &pos -> position.dir_entry_key;
	if( de_id_key_cmp( did, &de_key ) == EQUAL_TO )
		/*
		 * we are within sequence of directory entries
		 * with duplicate keys.
		 */
		++ pos -> position.pos;
	else {
		pos -> position.pos = 0;
		result = build_de_id_by_key( &de_key, did );
	}

	/*
	 * send information about directory entry to the ->filldir() filler
	 * supplied to us by caller (VFS).
	 */
	if( filldir( dirent, name, ( int ) strlen( name ),
		     /*
		      * offset of the next entry
		      */
		     ( loff_t ) pos -> entry_no + 1,
		     /*
		      * inode number of object bounden by this entry
		      */
		     oid_to_uino( get_key_objectid( &sd_key ) ),
		     iplug -> s.dir.extract_file_type( coord ) ) < 0 ) {
		/*
		 * ->filldir() is satisfied.
		 */
		result = 1;
	} else
		result = 0;
	return result;
}

int dir_readdir_init( struct file *f, tap_t *tap, readdir_pos **pos )
{
	struct inode        *inode;
	reiser4_file_fsdata *fsdata;
	reiser4_inode  *info;

	assert( "nikita-1359", f != NULL );
	inode = f -> f_dentry -> d_inode;
	assert( "nikita-1360", inode != NULL );

	if( ! S_ISDIR( inode -> i_mode ) )
		return -ENOTDIR;

	fsdata = reiser4_get_file_fsdata( f );
	assert( "nikita-2571", fsdata != NULL );
	if( IS_ERR( fsdata ) )
		return PTR_ERR( fsdata );

	info = reiser4_inode_data( inode );

	spin_lock( &info -> guard );
	if( readdir_list_is_clean( fsdata ) )
		readdir_list_push_front( &info -> readdir_list, fsdata );
	*pos = &fsdata -> dir.readdir;
	spin_unlock( &info -> guard );

	/*
	 * move @tap to the current position
	 */
	return dir_rewind( f, *pos, f -> f_pos, tap );
}

/** ->readdir method of directory plugin */
static int common_readdir( struct file *f /* directory file being read */, 
			   void *dirent /* opaque data passed to us by VFS */, 
			   filldir_t filld /* filler function passed to us
					      * by VFS */ )
{
	int           result;
	struct inode *inode;
	coord_t       coord;
	lock_handle   lh;
	tap_t         tap;
	file_plugin  *fplug;
	readdir_pos  *pos;

	assert( "nikita-1359", f != NULL );
	inode = f -> f_dentry -> d_inode;
	assert( "nikita-1360", inode != NULL );

	reiser4_stat_dir_add( readdir.calls );

	if( ! S_ISDIR( inode -> i_mode ) )
		return -ENOTDIR;

	coord_init_zero( &coord );
	init_lh( &lh );
	tap_init( &tap, &coord, &lh, ZNODE_READ_LOCK );

	trace_on( TRACE_DIR | TRACE_VFS_OPS, 
		  "readdir: inode: %llu offset: %lli\n", 
		  get_inode_oid( inode ), f -> f_pos );

	fplug = inode_file_plugin( inode );
	result = dir_readdir_init( f, &tap, &pos );
	if( result == 0 ) {
		result = tap_load( &tap );
		if( result == 0 )
			pos -> entry_no = f -> f_pos - 1;
		/*
		 * scan entries one by one feeding them to @filld
		 */
		while( result == 0 ) {
			coord_t *coord;

			coord = tap.coord;
			assert( "nikita-2572", coord_is_existing_unit( coord ) );

			if( item_type_by_coord( coord ) != DIR_ENTRY_ITEM_TYPE )
				break;
			else if( !fplug -> owns_item( inode, coord ) )
				break;
			result = feed_entry( pos, coord, filld, dirent );
			if( result > 0 ) {
				result = 0;
				break;
			} else if( result == 0 ) {
				result = go_next_unit( &tap );
				if( result == -ENAVAIL || result == -ENOENT ) {
					result = 0;
					break;
				}
			}
		}
		tap_relse( &tap );

		if( result == 0 ) {
			f -> f_pos = pos -> entry_no + 1;
			f -> f_version = inode -> i_version;
		}
	}
	tap_done( &tap );
	return result;
}

reiser4_plugin dir_plugins[ LAST_DIR_ID ] = {
	[ HASHED_DIR_PLUGIN_ID ] = {
		.dir = {
			.h = {
				.type_id = REISER4_DIR_PLUGIN_TYPE,
				.id      = HASHED_DIR_PLUGIN_ID,
				.pops    = NULL,
				.label   = "dir",
				.desc    = "hashed directory",
				.linkage = TS_LIST_LINK_ZERO
			},
			.resolve             = NULL,
			.resolve_into_inode  = hashed_lookup,
			.unlink              = common_unlink,
			.link                = common_link,
			.is_name_acceptable  = is_name_acceptable,
			.entry_key           = build_entry_key,
			.readdir_key         = build_readdir_key,
			.add_entry           = hashed_add_entry,
			.rem_entry           = hashed_rem_entry,
			.create_child        = common_create_child,
			.rename              = hashed_rename,
			.readdir             = common_readdir
		}
	},
	[ SEEKABLE_HASHED_DIR_PLUGIN_ID ] = {
		.dir = {
			.h = {
				.type_id = REISER4_DIR_PLUGIN_TYPE,
				.id      = HASHED_DIR_PLUGIN_ID,
				.pops    = NULL,
				.label   = "dir",
				.desc    = "hashed directory",
				.linkage = TS_LIST_LINK_ZERO
			},
			.resolve             = NULL,
			.resolve_into_inode  = hashed_lookup,
			.unlink              = common_unlink,
			.link                = common_link,
			.is_name_acceptable  = is_name_acceptable,
			.entry_key           = build_readdir_stable_entry_key,
			.readdir_key         = build_readdir_key,
			.add_entry           = hashed_add_entry,
			.rem_entry           = hashed_rem_entry,
			.create_child        = common_create_child,
			.rename              = hashed_rename,
			.readdir             = common_readdir
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

