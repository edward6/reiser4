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
			  coord_t *coord /* coord of acces */ )
{
	assert( "nikita-1682", dir != NULL );
	assert( "nikita-1683", coord != NULL );
	assert( "nikita-1684", coord -> node != NULL );
	assert( "nikita-1685", znode_is_any_locked( coord -> node ) );

	trace_stamp( TRACE_DIR );
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
/* Audited by: green(2002.06.15) */
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

	result = reiser4_add_nlink( object );
	if( result == 0 ) {
		/* add entry to the parent */
		result = parent_dplug -> add_entry( parent, 
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
	if( result == 0 )
		atomic_inc( &object -> i_count );
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
	reiser4_dir_entry_desc              entry;
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
	if( fplug -> single_link( object ) && 
	    ( atomic_read( &object -> i_count ) == 1 ) ) {
		if( perm_chk( object, delete, parent, victim ) )
			return -EPERM;
		/* remove file body. This is probably done in a whole
		 * lot of transactions and takes a lot of time. We keep
		 * @object locked. So, nlink shouldn't change. */
		object -> i_size = 0;
		if( fplug -> truncate != NULL ) {
			result = truncate_object( object, ( loff_t ) 0 );
			if( result != 0 )
				return result;
		}
		assert( "nikita-871", fplug -> single_link( object ) );
		assert( "nikita-873", atomic_read( &object -> i_count ) == 1 );
		assert( "nikita-872", object -> i_size == 0 );

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
			result = fplug -> rem_link( object );
			break;
		default:
			wrong_return_value( "nikita-1478", "uf_type" );
		}
	}
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
		warning( "nikita-431", "Cannot install plugin %i on %lx", 
			 data -> id, ( long ) object -> i_ino );
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
		warning( "nikita-432", "Cannot inherit from %lx to %lx", 
			 ( long ) parent -> i_ino, ( long ) object -> i_ino );
		return result;
	}

	reiser4_inode_data( object ) -> locality_id = parent -> i_ino;

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
		result = dplug -> add_entry( parent, dentry,
					     data, &entry );
		if( result != 0 ) {
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
		warning( "nikita-2219", "Failed to create sd for %lu (%lx)",
			 object -> i_ino, reiser4_inode_data( object ) -> flags );
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
			.entry_key           = build_readdir_stable_entry_key,
			.add_entry           = hashed_add_entry,
			.rem_entry           = hashed_rem_entry,
			.create_child        = common_create_child
		}
	},
	[ LARGE_DIR_PLUGIN_ID ] = {
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
			.add_entry           = hashed_add_entry,
			.rem_entry           = hashed_rem_entry,
			.create_child        = common_create_child
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

