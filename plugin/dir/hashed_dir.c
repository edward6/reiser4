/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
 * file names to to files.
 */

#include "../../reiser4.h"

static int create_dot_dotdot( struct inode *object, struct inode *parent );
static int find_entry( const struct inode *dir, struct dentry *name, 
		       lock_handle *lh, 
		       znode_lock_mode mode, reiser4_dir_entry_desc *entry );
static int check_item( const struct inode *dir, 
		       const coord_t *coord, const char *name );

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
	assert( "nikita-685", inode_get_flag( object, REISER4_NO_STAT_DATA ) );
	assert( "nikita-686", data -> id == DIRECTORY_FILE_PLUGIN_ID );
	assert( "nikita-687", object -> i_mode & S_IFDIR );
	trace_stamp( TRACE_DIR );
	
	result = common_file_save( object );
	if( result == 0 )
		result = create_dot_dotdot( object, parent );
	else
		warning( "nikita-2223", 
			 "Failed to create sd of directory %lu: %i",
			 object -> i_ino, result );
	return result;
}

/**
 * ->delete() method of directory plugin
 *
 * Delete dot and dotdot, decrease nlink on parent and call
 * common_file_delete() to delete stat data.
 *
 */
int hashed_delete( struct inode *object /* object being deleted */, 
		   struct inode *parent /* parent object */ )
{
	assert( "nikita-1449", object != NULL );

	if( !inode_get_flag( object, REISER4_NO_STAT_DATA ) ) {
		int                    result;
		struct dentry          goodby_dots;
		reiser4_dir_entry_desc entry;

		xmemset( &entry, 0, sizeof entry );

		entry.obj = goodby_dots.d_inode = object;
		xmemset( &goodby_dots, 0, sizeof goodby_dots );
		goodby_dots.d_name.name = ".";
		goodby_dots.d_name.len = 1;
		result = hashed_rem_entry( object, &goodby_dots, &entry );
		if( result != 0 )
			/*
			 * only worth a warning
			 *
			 * "values of B will give rise to dom!\n"
			 *          -- v6src/s2/mv.c:89
			 */
			warning( "nikita-2252", "Cannot remove dot of %li: %i",
				 ( long ) object -> i_ino, result );

		entry.obj = goodby_dots.d_inode = parent;
		xmemset( &goodby_dots, 0, sizeof goodby_dots );
		goodby_dots.d_name.name = "..";
		goodby_dots.d_name.len = 2;
		result = hashed_rem_entry( object, &goodby_dots, &entry );
		if( result != 0 )
			warning( "nikita-2253", "Cannot remove dotdot of %li: %i",
				 ( long ) object -> i_ino, result );
		
		reiser4_del_nlink( parent, 1 );
		return common_file_delete( object, parent );
	} else
		return 0;
}

/**
 * ->owns_item() for hashed directory object plugin.
 */
int hashed_owns_item( const struct inode *inode /* object to check against */, 
		      const coord_t *coord /* coord of item to check */ )
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
static int create_dot_dotdot( struct inode *object /* object to create dot and
						    * dotdot for */, 
			      struct inode *parent /* parent of @object */ )
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
		result = reiser4_add_nlink( object, 1 );
	else
		warning( "nikita-2222", "Failed to create dot in %lu: %i",
			 object -> i_ino, result );

	if( result == 0 ) {
		entry.obj = dots_entry.d_inode = parent;
		dots_entry.d_name.name = "..";
		dots_entry.d_name.len = 2;
		result = hashed_add_entry( object, &dots_entry, NULL, &entry );
		/*
		 * if creation of ".." failed, iput() will delete object
		 * with ".".
		 */
		if( result != 0 )
			warning( "nikita-2234", 
				 "Failed to create dotdot in %lu: %i",
				 object -> i_ino, result );
	}

	if( result == 0 )
		result = reiser4_add_nlink( parent, 1 );

	return result;
}

/**
 * implementation of ->resolve_into_inode() method for hashed durectories.
 */
file_lookup_result hashed_lookup( struct inode *parent /* inode of directory to
							* lookup into */,
				  struct dentry *dentry /* name to look for */ )
{
	int                    result;
	coord_t               *coord;
	lock_handle            lh;
	const char            *name;
	int                    len;
	reiser4_dir_entry_desc entry;
	
	assert( "nikita-1247", parent != NULL );
	assert( "nikita-1248", dentry != NULL );
	assert( "nikita-1123", dentry -> d_name.name != NULL );

	if( perm_chk( parent, lookup, parent, dentry ) )
		return -EPERM;
	
	name = dentry -> d_name.name;
	len  = dentry -> d_name.len;
	

	if( !is_name_acceptable( parent, name, len ) ) {
		/* some arbitrary error code to return */
		return -ENAMETOOLONG;
	}

	/* set up operations on dentry. */
	dentry -> d_op = &reiser4_dentry_operation;

	coord = &reiser4_get_dentry_fsdata( dentry ) -> entry_coord;
	init_lh( &lh );

	trace_on( TRACE_DIR | TRACE_VFS_OPS, "lookup inode: %lli \"%s\"\n",
		  ( __u64 ) parent -> i_ino, dentry -> d_name.name );

	/* find entry in a directory. This is plugin method. */
	result = find_entry( parent, dentry, &lh, ZNODE_READ_LOCK, &entry );
	if( result == 0 ) {
		/* entry was found, extract object key from it. */
		result = WITH_DATA( coord -> node, 
				    item_plugin_by_coord( coord ) ->
				    s.dir.extract_key( coord, &entry.key ) );
	}
	done_lh( &lh );
	
	if( result == 0 ) {
		struct inode *inode;

		inode = reiser4_iget( parent -> i_sb, &entry.key );
		if( !IS_ERR(inode) ) {
			if( inode_get_flag( inode, REISER4_LIGHT_WEIGHT_INODE ) ) {
				inode -> i_uid = parent -> i_uid;
				inode -> i_gid = parent -> i_gid;
				/* clear light-weight flag. If inode would be
				   read by any other name, [ug]id wouldn't
				   change. */
				inode_clr_flag( inode, 
						REISER4_LIGHT_WEIGHT_INODE );
			}
			/* success */
			d_add( dentry, inode );
			if( inode -> i_state & I_NEW )
				unlock_new_inode( inode );
			result = 0;
		} else
			result = PTR_ERR(inode);
	}
	return result;
}

/**
 * re-bind existing name at @new_coord in @new_dir to point to @old_inode.
 *
 * Helper function called from hashed_rename()
 */
static int replace_name( struct inode *to_inode /* inode where @from_coord is
						 * to be re-targeted at */,
			 struct inode *from_dir /* directory where @from_coord
						 * lives */,
			 struct inode *from_inode /* inode @from_coord
						   * originally point to */, 
			 coord_t *from_coord /* where directory entry is in
					      * the tree */,
			 lock_handle *from_lh /* lock handle on @from_coord */ )
{
	item_plugin *from_item;
	int          result;

	from_item = item_plugin_by_coord( from_coord );
	if( item_type_by_coord( from_coord ) == DIR_ENTRY_ITEM_TYPE ) {
		reiser4_key to_key;

		build_sd_key( to_inode, &to_key );

		/*
		 * everything is found and prepared to change directory entry
		 * at @from_coord to point to @to_inode.
		 *
		 * @to_inode is just about to get new name, so bump its link
		 * counter.
		 *
		 */
		result = reiser4_add_nlink( to_inode, 0 );
		if( result != 0 ) {
			/*
			 * Don't issue warning: this may be plain -EMLINK
			 */
			return result;
		}
		from_item -> s.dir.update_key( from_coord, &to_key, from_lh );
		/*
		 * @from_inode just lost its name, he-he.
		 *
		 * If @from_inode was directory, it contained dotdot pointing
		 * to @from_dir. @from_dir i_nlink will be decreased when
		 * iput() will be called on @from_inode.
		 *
		 * FIXME-NIKITA if file-system is not ADG (hard-links are
		 * supported on directories), iput(from_inode) will not remove
		 * @from_inode, and thus above is incorrect, but hard-links on
		 * directories are problematic in many other respects.
		 */
		result = reiser4_del_nlink( from_inode, 0 );
		if( result != 0 ) {
			warning( "nikita-2330", 
				 "Cannot remove link from source: %i. "
				 "Possible disk space leak", result );
			/*
			 * Has to return success, because entry is already
			 * modified.
			 */
			result = 0;
		}

		/*
		 * FIXME-NIKITA consider calling plugin method in stead of
		 * accessing inode fields directly.
		 */
		from_dir -> i_mtime = from_dir -> i_ctime = CURRENT_TIME;
		from_inode -> i_ctime = CURRENT_TIME;
	} else {
		warning( "nikita-2326", "Unexpected item type" );
		print_plugin( "item", from_item );
		result = -EIO;
	}
	return result;
}

/**
 * add new entry pointing to @inode into @dir at @coord, locked by @lh
 */
static int add_name( struct inode *inode /* inode where @coord is to be
					  * re-targeted at */,
		     struct inode *dir /* directory where @coord lives */,
		     coord_t *coord /* where directory entry is in the tree */,
		     lock_handle *lh /* lock handle on @coord */ )
{
}

/**
 * ->rename directory plugin method implementation for hashed directories. 
 *
 * See comments in the body.
 *
 * It is arguable that this function can be made generic so, that it will be
 * applicable to any kind of directory plugin that deals with directories
 * composed out of directory entries. The only obstacle here is that we don't
 * have any data-type to represent directory entry. This should be
 * re-considered when more than one different directory plugin will be
 * implemented.
 */
int hashed_rename( struct inode  *old_dir  /* directory where @old is located */,
		   struct dentry *old_name /* old name */,
		   struct inode  *new_dir  /* directory where @new is located */,
		   struct dentry *new_name /* new name */ )
{
	/*
	 * From `The Open Group Base Specifications Issue 6'
	 *
	 *
	 * If either the old or new argument names a symbolic link, rename()
	 * shall operate on the symbolic link itself, and shall not resolve
	 * the last component of the argument. If the old argument and the new
	 * argument resolve to the same existing file, rename() shall return
	 * successfully and perform no other action.
	 *
	 * [this is done by VFS: vfs_rename()]
	 *
	 *
	 * If the old argument points to the pathname of a file that is not a
	 * directory, the new argument shall not point to the pathname of a
	 * directory. 
	 *
	 * [checked by VFS: vfs_rename->may_delete()]
	 *
	 *            If the link named by the new argument exists, it shall
	 * be removed and old renamed to new. In this case, a link named new
	 * shall remain visible to other processes throughout the renaming
	 * operation and refer either to the file referred to by new or old
	 * before the operation began. 
	 *
	 * [we should assure this]
	 *
	 *                             Write access permission is required for
	 * both the directory containing old and the directory containing new.
	 *
	 * [checked by VFS: vfs_rename->may_delete(), may_create()]
	 *
	 * If the old argument points to the pathname of a directory, the new
	 * argument shall not point to the pathname of a file that is not a
	 * directory. 
	 *
	 * [checked by VFS: vfs_rename->may_delete()]
	 *
	 *            If the directory named by the new argument exists, it
	 * shall be removed and old renamed to new. In this case, a link named
	 * new shall exist throughout the renaming operation and shall refer
	 * either to the directory referred to by new or old before the
	 * operation began. 
	 *
	 * [we should assure this]
	 *
	 *                  If new names an existing directory, it shall be
	 * required to be an empty directory.
	 *
	 * [we should check this]
	 *
	 * If the old argument points to a pathname of a symbolic link, the
	 * symbolic link shall be renamed. If the new argument points to a
	 * pathname of a symbolic link, the symbolic link shall be removed.
	 *
	 * The new pathname shall not contain a path prefix that names
	 * old. Write access permission is required for the directory
	 * containing old and the directory containing new. If the old
	 * argument points to the pathname of a directory, write access
	 * permission may be required for the directory named by old, and, if
	 * it exists, the directory named by new.
	 *
	 * [checked by VFS: vfs_rename(), vfs_rename_dir()]
	 *
	 * If the link named by the new argument exists and the file's link
	 * count becomes 0 when it is removed and no process has the file
	 * open, the space occupied by the file shall be freed and the file
	 * shall no longer be accessible. If one or more processes have the
	 * file open when the last link is removed, the link shall be removed
	 * before rename() returns, but the removal of the file contents shall
	 * be postponed until all references to the file are closed.
	 *
	 * [iput() handles this, but we can do this manually, a la
	 * reiser4_unlink()]
	 *
	 * Upon successful completion, rename() shall mark for update the
	 * st_ctime and st_mtime fields of the parent directory of each file.
	 *
	 * [N/A]
	 *
	 */

	int result;

	struct inode *old_inode;
	struct inode *new_inode;

	reiser4_dir_entry_desc old_entry;
	reiser4_dir_entry_desc new_entry;

	coord_t old_coord;
	coord_t new_coord;

	lock_handle old_lh;
	lock_handle new_lh;

	dir_plugin *old_dplug;
	dir_plugin *new_dplug;

	lookup_result old_search;
	lookup_result new_search;

	assert( "nikita-2318", old_dir != NULL );
	assert( "nikita-2319", new_dir != NULL );
	assert( "nikita-2320", old_name != NULL );
	assert( "nikita-2321", new_name != NULL );

	old_inode = old_name -> d_inode;
	new_inode = new_name -> d_inode;

	old_dplug = inode_dir_plugin( old_dir );
	new_dplug = inode_dir_plugin( new_dir );

	xmemset( &old_entry, 0, sizeof old_entry );
	xmemset( &new_entry, 0, sizeof new_entry );

	/*
	 * @new_entry and @old_entry are describing directory entries pointing
	 * to @old_inode, but under names @new_name and @old_name
	 * respectively.
	 */
	new_entry.obj = old_entry.obj = old_inode;

	/* build keys of directory entries */
	result = old_dplug -> entry_key( old_dir, old_name, &old_entry.key );
	if( result != 0 )
		return result;

	result = new_dplug -> entry_key( new_dir, new_name, &new_entry.key );
	if( result != 0 )
		return result;

	coord_init_zero( &old_coord );
	coord_init_zero( &new_coord );

	init_lh( &old_lh );
	init_lh( &new_lh );

	/*
	 * 1. find both entries if they exist, or find insertion points.
	 *
	 * This is somewhat complicated by our tree node locking rules. It is
	 * easy to show that if one starts top-to-bottom tree traversal, while
	 * keeping lock on the node somewhere in the tree, deadlock is
	 * possible. Hence it is not possible to simply find one directory
	 * entry and, keeping lock on it, look for second. Nonetheless,
	 * lookup_couple() is cunning function that can search for two keys at
	 * once by means of some seal-related magic.
	 *
	 */

	result = lookup_couple( current_tree, 
				&old_entry.key, &new_entry.key,
				&old_coord, &new_coord, &old_lh, &new_lh,
				ZNODE_WRITE_LOCK,
				FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, 
				CBK_FOR_INSERT, &old_search, &new_search );

	if( result != 0 ) {
		/*
		 * something horrible happened.
		 */
		warning( "nikita-2322", "lookup_couple failed with %i", result );
		return result;
	}

	if( old_result == CBK_COORD_FOUND ) {
		int is_dir; /* is @old directory */

		/*
		 * ok, old_coord contains item with directory entry for @old
		 */
		is_dir = S_ISDIR( old_inode -> i_mode );
		if( new_node != NULL ) {
			/*
			 * target (@new_name) exists.
			 */
			/*
			 * FIXME-NIKITA not clear what to do with objects that
			 * are both directories and files at the same time.
			 */
			if( is_dir && !is_dir_empty( new_inode ) )
				result = -ENOTEMPTY;
			else if( new_result != CBK_COORD_FOUND ) {
				warning( "nikita-2324", "Target not found" );
				result = -ENOENT;
			} else
				result = replace_name( old_inode, new_dir,
						       new_inode, new_coord,
						       new_lh );
		} else {
			/*
			 * target (@new_name) doesn't exists.
			 */
			if( is_dir )
				/*
				 * ext2 does this in different order: first
				 * inserts new entry, then increases directory
				 * nlink. We don't want do this, because
				 * reiser4_add_nlink() calls ->add_link()
				 * plugin method that can fail for whatever
				 * reason, leaving as with cleanup problems.
				 */
				result = reiser4_add_nlink( new_dir, 0 );
			if( result == 0 ) {
				/*
				 * @old_inode is getting new name
				 */
				reiser4_add_nlink( old_inode, 0 );
				/*
				 * create @new_name in @new_dir pointing to
				 * @old_inode
				 */
				result = hashed_add_entry( new_dir, new_name,
							   NULL, &new_entry );
				if( result != 0 ) {
					result = reiser4_del_nlink( old_inode, 0 );
					if( result != 0 ) {
						warning( "nikita-2327", "Cannot drop link on source: %i. Possible disk space leak",
							 result );
					}
					result = reiser4_del_nlink( new_dir, 0 );
					if( result != 0 ) {
						warning( "nikita-2328", "Cannot drop link on target dir: %i. Possible disk space leak",
							 result );
					}
				}
			}
		}
	} else {
		warning( "nikita-2323", "Cannot find old entry %i", old_result );
		result = -ENOENT;
	}

	done_lh( &new_lh );
	done_lh( &old_lh );

	old_de = ext2_find_entry (old_dir, old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = ext2_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page *new_page;
		struct ext2_dir_entry_2 *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !ext2_empty_dir (new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = ext2_find_entry (new_dir, new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		ext2_inc_count(old_inode);
		ext2_set_link(new_dir, new_de, new_page, old_inode);
		new_inode->i_ctime = CURRENT_TIME;
		if (dir_de)
			new_inode->i_nlink--;
		ext2_dec_count(new_inode);
	} else {
		if (dir_de) {
			err = -EMLINK;
			if (new_dir->i_nlink >= EXT2_LINK_MAX)
				goto out_dir;
		}
		ext2_inc_count(old_inode);
		err = ext2_add_link(new_dentry, old_inode);
		if (err) {
			ext2_dec_count(old_inode);
			goto out_dir;
		}
		if (dir_de)
			ext2_inc_count(new_dir);
	}

	ext2_delete_entry (old_de, old_page);
	ext2_dec_count(old_inode);

	if (dir_de) {
		ext2_set_link(old_inode, dir_de, dir_page, new_dir);
		ext2_dec_count(old_dir);
	}
	return 0;
out_dir:
	if (dir_de) {
		kunmap(dir_page);
		page_cache_release(dir_page);
	}
out_old:
	kunmap(old_page);
	page_cache_release(old_page);
out:
	return err;
}

/**
 * ->add_entry() method for hashed directory object plugin.
 */
int hashed_add_entry( struct inode *object /* directory to add new name
					    * in */, 
		      struct dentry *where /* new name */, 
		      reiser4_object_create_data *data UNUSED_ARG /* parameters
								   * of new
								   * object */, 
		      reiser4_dir_entry_desc *entry /* parameters of new
						     * directory entry */ )
{
	int                    result;
	coord_t               *coord;
	lock_handle            lh;
	reiser4_dentry_fsdata *fsdata;

	assert( "nikita-1114", object != NULL );
	assert( "nikita-1250", where != NULL );

	fsdata = reiser4_get_dentry_fsdata( where );
	if( unlikely( IS_ERR( fsdata ) ) )
		return PTR_ERR( fsdata );

	init_lh( &lh );

	trace_on( TRACE_DIR, "[%i]: creating \"%s\" in %lx\n", current_pid,
		  where -> d_name.name, object -> i_ino );

	coord = &fsdata -> entry_coord;

	/*
	 * check for this entry in a directory. This is plugin method.
	 */
	result = find_entry( object, where, &lh, ZNODE_WRITE_LOCK, entry );
	if( likely( result == -ENOENT ) ) {
		/*
		 * add new entry. Just pass control to the directory
		 * item plugin.
		 */
		assert( "nikita-1709", inode_dir_item_plugin( object ) );
		assert( "nikita-2230", coord -> node == lh.node );
		seal_done( &fsdata -> entry_seal );
		result = inode_dir_item_plugin( object ) ->
			s.dir.add_entry( object, coord, &lh, where, entry );
	} else if( result == 0 ) {
		assert( "nikita-2232", coord -> node == lh.node );
		result = -EEXIST;
	}
	done_lh( &lh );

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
	coord_t           *coord;
	znode             *loaded;
	lock_handle        lh;
	reiser4_dentry_fsdata *fsdata;

	assert( "nikita-1124", object != NULL );
	assert( "nikita-1125", where != NULL );

	init_lh( &lh );

	/*
	 * check for this entry in a directory. This is plugin method.
	 */
	result = find_entry( object, where, &lh, ZNODE_WRITE_LOCK, entry );
	fsdata = reiser4_get_dentry_fsdata( where );
	coord = &fsdata -> entry_coord;
	loaded = coord -> node;
	if( result == 0 ) {
		/*
		 * remove entry. Just pass control to the directory item
		 * plugin.
		 */
		assert( "vs-542", inode_dir_item_plugin( object ) );
		seal_done( &fsdata -> entry_seal );
		result = WITH_DATA( loaded, inode_dir_item_plugin( object ) ->
				    s.dir.rem_entry( object, 
						     coord, &lh, entry ) );
	}
	done_lh( &lh );

	return result;
}


static int entry_actor( reiser4_tree *tree /* tree being scanned */, 
			coord_t *coord /* current coord */, 
			lock_handle *lh /* current lock handle */,
			void *args /* argument to scan */ );

typedef struct entry_actor_args {
	const char  *name;
	reiser4_key *key;
#if REISER4_USE_COLLISION_LIMIT || REISER4_STATS
	int          max_non_uniq;
	int          non_uniq;
#endif
	int          not_found;
	znode_lock_mode mode;

	coord_t          last_coord;
	lock_handle last_lh;
	const struct inode *inode;
} entry_actor_args;

/**
 * Look for given @name within directory @dir.
 *
 * This is called during lookup, creation and removal of directory
 * entries.
 *
 * First calculate key that directory entry for @name would have. Search
 * for this key in the tree. If such key is found, scan all items with
 * the same key, checking name in each directory entry along the way.
 *
 */
static int find_entry( const struct inode *dir /* directory to scan */, 
		       struct dentry *de /* name to search for */, 
		       lock_handle *lh /* resulting lock handle */,
		       znode_lock_mode mode /* required lock mode */,
		       reiser4_dir_entry_desc *entry /* parameters of found
						      * directory entry */ )
{
	const struct qstr *name;
	seal_t            *seal;
	coord_t           *coord;
	int                result;

	assert( "nikita-1130", lh != NULL );
	assert( "nikita-1128", dir != NULL );

	name = &de -> d_name;
	assert( "nikita-1129", name != NULL );
	
	/* 
	 * dentry private data don't require lock, because dentry
	 * manipulations are protected by i_sem on parent.  
	 *
	 * This is not so for inodes, because there is no -the- parent in
	 * inode case.
	 */
	coord = &reiser4_get_dentry_fsdata( de ) -> entry_coord;
	seal  = &reiser4_get_dentry_fsdata( de ) -> entry_seal;
	/* compose key of directory entry for @name */
	result = inode_dir_plugin( dir ) -> entry_key( dir, name, &entry -> key );
	if( result != 0 )
		return result;

	if( seal_is_set( seal ) ) {
		/* check seal */
		result = seal_validate( seal, coord, &entry -> key, LEAF_LEVEL,
					lh, FIND_EXACT, mode, ZNODE_LOCK_LOPRI );
		if( result == 0 ) {
			/* key was found. Check that it is really item we are
			 * looking for. */
			result = WITH_DATA( coord -> node, 
					    check_item( dir, coord, name -> name ) );
			if( result == 0 )
				return 0;
		}
	}
	result = coord_by_key( tree_by_inode( dir ), &entry -> key, coord, lh,
			       mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, 
			       CBK_FOR_INSERT );
	if( result == CBK_COORD_FOUND ) {
		entry_actor_args arg;

		/* Iterate through all units with the same keys. */
		arg.name      = name -> name;
		arg.key       = &entry -> key;
		arg.not_found = 0;
#if REISER4_USE_COLLISION_LIMIT
		arg.non_uniq = 0;
		arg.max_non_uniq = max_hash_collisions( dir );
#endif
		arg.mode = mode;
		arg.inode = dir;
		coord_init_zero( &arg.last_coord );
		init_lh( &arg.last_lh );

		result = iterate_tree( tree_by_inode( dir ), 
				       coord, lh, entry_actor, &arg, mode, 1 );
		/*
		 * if end of the tree or extent was reached during
		 * scanning.
		 */
		if( arg.not_found || ( result == -ENAVAIL ) ) {
			/* step back */
			done_lh( lh );

			result = zload( arg.last_coord.node );
			if( result == 0 ) {
				coord_dup( coord, &arg.last_coord );
				move_lh( lh, &arg.last_lh );
				result = -ENOENT;
				zrelse( arg.last_coord.node );
			}
		}

		done_lh( &arg.last_lh );
		if( result == 0 )
			seal_init( seal, coord, &entry -> key );
	}
	return result;
}

/**
 * Function called by find_entry() to look for given name in the directory.
 */
static int entry_actor( reiser4_tree *tree UNUSED_ARG /* tree being scanned */, 
			coord_t *coord /* current coord */, 
			lock_handle *lh /* current lock handle */,
			void *entry_actor_arg /* argument to scan */ )
{
	reiser4_key       unit_key;
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
	
	if( !keyeq( args -> key, unit_key_by_coord( coord, &unit_key ) ) ) {
		assert( "nikita-1791", 
			keylt( args -> key, 
			       unit_key_by_coord( coord, &unit_key ) ) );
		args -> not_found = 1;
		args -> last_coord.between = AFTER_UNIT;
		return 0;
	}

	coord_dup( &args -> last_coord, coord );
	if( args -> last_lh.node != lh -> node ) {
		int lock_result;

		done_lh( &args -> last_lh );
		assert( "nikita-1896", znode_is_any_locked( lh -> node ) );
		lock_result = longterm_lock_znode( &args -> last_lh, lh -> node,
						   args -> mode, 
						   ZNODE_LOCK_HIPRI );
		if( lock_result != 0 )
			return lock_result;
	}
	return check_item( args -> inode, coord, args -> name );
}

static int check_item( const struct inode *dir, 
		       const coord_t *coord, const char *name )
{
	item_plugin      *iplug;

	iplug = item_plugin_by_coord( coord );
	if( iplug == NULL ) {
		warning( "nikita-1135", "Cannot get item plugin" );
		print_coord( "coord", coord, 1 );
		return -EIO;
	} else if( item_id_by_coord( coord ) !=
		   item_id_by_plugin( inode_dir_item_plugin( dir ) ) ) {
		/* item id of current item does not match to id of items a
		 * directory is built of */
		warning( "nikita-1136", "Wrong item plugin" );
		print_coord( "coord", coord, 1 );
		print_plugin( "plugin", item_plugin_to_plugin (iplug) );
		return -EIO;
	}
	assert( "nikita-1137", iplug -> s.dir.extract_name );

	trace_on( TRACE_DIR, "[%i]: check_item: \"%s\", \"%s\" in %lli\n",
		  current_pid, name, 
		  iplug -> s.dir.extract_name( coord ),
		  *znode_get_block( coord -> node ) );
	/*
	 * Compare name stored in this entry with name we are looking for.
	 * 
	 * FIXME-NIKITA Here should go code for support of something like
	 * unicode, code tables, etc.
	 */
	return !!strcmp( name, iplug -> s.dir.extract_name( coord ) );
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

