/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Interface to VFS. Reiser4 {file|inode|address_space|dentry}_operations
 * are defined here.
 */

#include "reiser4.h"

/* inode operations */

static int reiser4_create (struct inode *,struct dentry *,int);
static struct dentry * reiser4_lookup (struct inode *,struct dentry *);
static int reiser4_link (struct dentry *,struct inode *,struct dentry *);
static int reiser4_unlink (struct inode *,struct dentry *);
static int reiser4_rmdir( struct inode *, struct dentry * );
static int reiser4_symlink (struct inode *,struct dentry *,const char *);
static int reiser4_mkdir (struct inode *,struct dentry *,int);
static int reiser4_mknod (struct inode *,struct dentry *,int,int);
static int reiser4_rename (struct inode *, struct dentry *,
			   struct inode *, struct dentry *);
static int reiser4_readlink (struct dentry *, char *,int);
static int reiser4_follow_link (struct dentry *, struct nameidata *);
static void reiser4_truncate (struct inode *);
static int reiser4_permission (struct inode *, int);
static int reiser4_setattr (struct dentry *, struct iattr *);
static int reiser4_getattr (struct vfsmount *mnt, struct dentry *, struct kstat *);
static int reiser4_setxattr(struct dentry *, const char *, void *, size_t, int);
static ssize_t reiser4_getxattr(struct dentry *, const char *, void *, size_t);
static ssize_t reiser4_listxattr(struct dentry *, char *, size_t);
static int reiser4_removexattr(struct dentry *, const char *);

/* file operations */

static loff_t reiser4_llseek (struct file *, loff_t, int);
static ssize_t reiser4_read (struct file *, char *, size_t, loff_t *);
static ssize_t reiser4_write (struct file *, const char *, size_t, loff_t *);
static int reiser4_readdir (struct file *, void *, filldir_t);
static unsigned int reiser4_poll (struct file *, struct poll_table_struct *);
static int reiser4_ioctl (struct inode *, 
			  struct file *, unsigned int, unsigned long);
static int reiser4_mmap (struct file *, struct vm_area_struct *);
static int reiser4_open (struct inode *, struct file *);
static int reiser4_flush (struct file *);
static int reiser4_release (struct inode *, struct file *);
static int reiser4_fsync (struct file *, struct dentry *, int datasync);
static int reiser4_fasync (int, struct file *, int);
static int reiser4_lock (struct file *, int, struct file_lock *);
static ssize_t reiser4_readv (struct file *, 
			      const struct iovec *, unsigned long, loff_t *);
static ssize_t reiser4_writev (struct file *, 
			       const struct iovec *, unsigned long, loff_t *);
static ssize_t reiser4_sendpage (struct file *, struct page *, 
				 int, size_t, loff_t *, int);
static unsigned long reiser4_get_unmapped_area(struct file *, unsigned long, 
					       unsigned long, unsigned long, 
					       unsigned long);
/* super operations */

static struct inode *reiser4_alloc_inode( struct super_block *super );
static void reiser4_destroy_inode( struct inode *inode );
static void reiser4_dirty_inode (struct inode *);
static void reiser4_write_inode (struct inode *, int);
static void reiser4_put_inode (struct inode *);
static void reiser4_delete_inode (struct inode *);
static void reiser4_write_super (struct super_block *);
static void reiser4_write_super_lockfs (struct super_block *);
static void reiser4_unlockfs (struct super_block *);
static int reiser4_statfs (struct super_block *, struct statfs *);
static int reiser4_remount_fs (struct super_block *, int *, char *);
static void reiser4_clear_inode (struct inode *);
static void reiser4_kill_super (struct super_block *);
static struct dentry * reiser4_fh_to_dentry(struct super_block *sb, __u32 *fh, 
					    int len, int fhtype, int parent);
static int reiser4_dentry_to_fh(struct dentry *, __u32 *fh, 
				int *lenp, int need_parent);
static int reiser4_show_options( struct seq_file *m, struct vfsmount *mnt );
static int reiser4_fill_super(struct super_block *s, void *data, int silent);


/* address space operations */

static int reiser4_writepage(struct page *);
static int reiser4_readpage(struct file *, struct page *);
static int reiser4_sync_page(struct page *);
static int reiser4_vm_writeback( struct page *page, int *nr_to_write );
/*
static int reiser4_prepare_write(struct file *, 
				 struct page *, unsigned, unsigned);
static int reiser4_commit_write(struct file *, 
				struct page *, unsigned, unsigned);
*/
static int reiser4_bmap(struct address_space *, long);
/*
static int reiser4_direct_IO(int, struct inode *, 
			     struct kiobuf *, unsigned long, int);
*/
extern struct dentry_operations reiser4_dentry_operation;

static struct file_system_type reiser4_fs_type;

static int invoke_create_method( struct inode *parent, 
				 struct dentry *dentry, 
				 reiser4_object_create_data *data );


static int readdir_actor( reiser4_tree *tree, 
			  coord_t *coord, lock_handle *lh,
			  void *arg );

/**
 * reiser4_lookup() - entry point for ->lookup() method.
 *
 * This is a wrapper for lookup_object which is a wrapper for the directory plugin that does the lookup.
 *
 * This is installed in ->lookup() in reiser4_inode_operations.
 */
/* Audited by: umka (2002.06.12) */
static struct dentry *reiser4_lookup( struct inode *parent, /* directory within which we are to look for the name
							     * specified in dentry */
				      struct dentry *dentry /* this contains the name that is to be looked for on entry,
							       and on exit contains a filled in dentry with a pointer to
							       the inode (unless name not found) */

)
{
	dir_plugin *dplug;
	int retval;
	struct dentry *result;
	REISER4_ENTRY_PTR( parent -> i_sb );

	assert( "nikita-403", parent != NULL );
	assert( "nikita-404", dentry != NULL );

	/* find @parent directory plugin and make sure that it has lookup
	 * method */
	dplug = inode_dir_plugin( parent );
	if( dplug == NULL || !dplug -> resolve_into_inode/*lookup*/ ) {
		REISER4_EXIT_PTR( ERR_PTR( -ENOTDIR ));
	}

	/* call its lookup method */
	retval = dplug -> resolve_into_inode( parent, dentry );
	result = NULL;
	if( retval == 0 ) {
		assert( "nikita-1943", dentry -> d_inode != NULL );
		if( dentry -> d_inode -> i_state & I_NEW )
			unlock_new_inode( dentry -> d_inode );
	} else if( retval == -ENOENT ) {
		/* object not found */
		d_add( dentry, NULL );
	} else
		result = ERR_PTR( retval );
	/* success */
	REISER4_EXIT_PTR( result );
}

/** ->create() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int reiser4_create( struct inode *parent /* inode of parent
						 * directory */, 
			   struct dentry *dentry /* dentry of new object to
						  * create */, 
			   int mode /* new object mode */ )
{
	reiser4_object_create_data data;
	
	data.mode = S_IFREG | mode;
	data.id   = REGULAR_FILE_PLUGIN_ID;
	return invoke_create_method( parent, dentry, &data );
}

/** ->mkdir() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int reiser4_mkdir( struct inode *parent /* inode of parent
						* directory */, 
			  struct dentry *dentry /* dentry of new object to
						 * create */, 
			  int mode /* new object's mode */ )
{
	reiser4_object_create_data data;
	
	data.mode = S_IFDIR | mode;
	data.id   = DIRECTORY_FILE_PLUGIN_ID;
	return invoke_create_method( parent, dentry, &data );
}

/** ->symlink() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int reiser4_symlink( struct inode *parent /* inode of parent
						  * directory */, 
			    struct dentry *dentry /* dentry of new object to
						   * create */, 
			    const char *linkname /* pathname to put into
						  * symlink */ )
{
	reiser4_object_create_data data;
	
	data.name = linkname;
	data.id   = SYMLINK_FILE_PLUGIN_ID;
	data.mode = S_IFLNK | S_IRWXUGO;
	return invoke_create_method( parent, dentry, &data );
}

/** ->mknod() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int reiser4_mknod( struct inode *parent /* inode of parent directory */, 
			  struct dentry *dentry /* dentry of new object to
						 * create */, 
			  int mode /* new object's mode */, 
			  int rdev /* minor and major of new device node */ )
{
	reiser4_object_create_data data;
	
	data.mode = mode;
	data.rdev = rdev;
	data.id   = SPECIAL_FILE_PLUGIN_ID;
	return invoke_create_method( parent, dentry, &data );
}

/** ->rename() inode operation */
static int reiser4_rename( struct inode *old_dir, struct dentry *old,
			   struct inode *new_dir, struct dentry *new )
{
	int    result;
	REISER4_ENTRY( old_dir -> i_sb );

	assert( "nikita-2314", old_dir != NULL );
	assert( "nikita-2315", old != NULL );
	assert( "nikita-2316", new_dir != NULL );
	assert( "nikita-2317", new != NULL );

	result = perm_chk( old_dir, rename, old_dir, old, new_dir, new );
	if( result == 0 ) {
		dir_plugin *dplug;

		dplug = inode_dir_plugin( old_dir );
		assert( "nikita-2271", dplug != NULL );
		if( dplug -> rename != NULL )
			result = dplug -> rename( old_dir, old, new_dir, new );
		else
			result = -EPERM;
	}
	REISER4_EXIT( result );
}

static int reiser4_readlink (struct dentry *dentry,
			     char *buf, int buflen)
{
	assert( "vs-852", S_ISLNK( dentry -> d_inode -> i_mode ) );
	if( !dentry -> d_inode -> u.generic_ip ||
	    !inode_get_flag( dentry -> d_inode, REISER4_GENERIC_VP_USED ) )
		return -EINVAL;
	return vfs_readlink( dentry, buf, buflen,
			     dentry -> d_inode -> u.generic_ip );
}

static int reiser4_follow_link (struct dentry *dentry,
				struct nameidata *data)
{
	assert( "vs-851", S_ISLNK( dentry -> d_inode -> i_mode ) );

	if( !dentry -> d_inode -> u.generic_ip ||
	    !inode_get_flag( dentry -> d_inode, REISER4_GENERIC_VP_USED ) )
		return -EINVAL;
	return vfs_follow_link (data, dentry -> d_inode -> u.generic_ip);
}

/*
 * ->setattr() inode operation
 *
 * Called from notify_change.
 */
static int reiser4_setattr( struct dentry *dentry, struct iattr *attr )
{
	struct       inode *inode = dentry -> d_inode;
	int          result;
	REISER4_ENTRY( inode -> i_sb );

	assert( "nikita-2269", attr != NULL );

	result = perm_chk( inode, setattr, dentry, attr );
	if( result == 0 ) {
		file_plugin *fplug;

		fplug = inode_file_plugin( inode );
		assert( "nikita-2271", fplug != NULL );
		assert( "nikita-2296", fplug -> setattr != NULL );
		result = fplug -> setattr( inode, attr );
	}
	REISER4_EXIT( result );
}

/**
 * ->getattr() inode operation called (indirectly) by sys_stat().
 */
static int reiser4_getattr( struct vfsmount *mnt UNUSED_ARG,
			    struct dentry *dentry, struct kstat *stat )
{
	struct       inode *inode = dentry -> d_inode;
	int          result;
	REISER4_ENTRY( inode -> i_sb );

	result = perm_chk( inode, getattr, mnt, dentry, stat );
	if( result == 0 ) {
		file_plugin *fplug;

		fplug = inode_file_plugin( inode );
		assert( "nikita-2295", fplug != NULL );
		assert( "nikita-2297", fplug -> getattr != NULL );
		result = fplug -> getattr( mnt, dentry, stat );
	}
	REISER4_EXIT( result );
}


/** ->read() VFS method in reiser4 file_operations */
/* Audited by: umka (2002.06.12) */
static ssize_t reiser4_read( struct file *file /* file to read from */,
			     char *buf /* user-space buffer to put data read
					* from the file */, 
			     size_t size /* bytes to read */, 
			     loff_t *off /* offset to start reading from. This
					  * is updated to indicate actual
					  * number of bytes read */ )
{
	file_plugin *fplug;
	ssize_t result;

	REISER4_ENTRY( file -> f_dentry -> d_inode -> i_sb );
	
	assert( "umka-072", file != NULL );
	assert( "umka-073", buf != NULL );
	assert( "umka-074", off != NULL );
	
	trace_on( TRACE_VFS_OPS, "READ: (i_ino %li, size %lld): %u bytes from pos %lli\n",
		  file -> f_dentry -> d_inode -> i_ino,
		  file -> f_dentry -> d_inode -> i_size, size, *off );

	fplug = inode_file_plugin( file -> f_dentry -> d_inode );
	assert( "nikita-417", fplug != NULL );

	if( fplug->read == NULL ) {
		result = -EPERM;
	} else {
		result = fplug -> read ( file, buf, size, off );
	}

	REISER4_EXIT( result );
}

/** ->write() VFS method in reiser4 file_operations */
/* Audited by: umka (2002.06.12) */
static ssize_t reiser4_write( struct file *file /* file to write on */,
			      const char *buf /* user-space buffer to get data
					       * to write on the file */, 
			      size_t size /* bytes to write */, 
			      loff_t *off /* offset to start writing
					   * from. This is updated to indicate
					   * actual number of bytes written */ )
{
	file_plugin *fplug;
	struct inode *inode;
	ssize_t result;

	REISER4_ENTRY( ( inode = file -> f_dentry -> d_inode ) -> i_sb );
	
	assert( "nikita-1421", file != NULL );
	assert( "nikita-1422", buf != NULL );
	assert( "nikita-1424", off != NULL );

	trace_on( TRACE_VFS_OPS, "WRITE: (i_ino %li, size %lld): %u bytes to pos %lli\n",
		  inode -> i_ino, inode -> i_size, size, *off );

	fplug = inode_file_plugin( inode );
	if( fplug -> write != NULL ) {
		result = fplug -> write( file, buf, size, off );
	} else {
		result = -EPERM;
	}
	REISER4_EXIT( result );
}

/** ->truncate() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static void reiser4_truncate( struct inode *inode /* inode to truncate */)
{
	__REISER4_ENTRY( inode -> i_sb, );
	
	assert( "umka-075", inode != NULL );
	
	trace_on( TRACE_VFS_OPS, "TRUNCATE: i_ino %li to size %lli\n",
		  inode -> i_ino, inode -> i_size );

	truncate_object( inode, inode -> i_size );
	/* 
	 * for mysterious reasons ->truncate() VFS call doesn't return
	 * value 
	 */

	__REISER4_EXIT( &__context );
}

/** return number of files in a filesystem. It is used in reiser4_statfs to
 * fill ->f_ffiles */
/* Audited by: umka (2002.06.12) */
static long oids_used( struct super_block *s /* super block of file system in
					      * queried */ )
{
	oid_allocator_plugin *oplug;
	__u64 used;

	assert( "umka-076", s != NULL );
	assert( "vs-484", get_super_private( s ) );

	oplug = get_super_private( s ) -> oid_plug;
	if( !oplug || !oplug -> oids_used )
		return (long)-1;

	used = oplug -> oids_used( &get_super_private( s ) -> oid_allocator );
	if( used < ( __u64 )( ( long )~0 ) >> 1 )
		return ( long )used;
	else
		return ( long )-1;
}


/** number of oids available for use by users. It is used in reiser4_statfs to
 * fill ->f_ffree */
/* Audited by: umka (2002.06.12) */
static long oids_free( struct super_block *s /* super block of file system in
					      * queried */ )
{
	oid_allocator_plugin *oplug;
	__u64 used;


	assert( "umka-077", s != NULL );
	assert( "vs-485", get_super_private( s ) );

	oplug = get_super_private( s ) -> oid_plug;
	if( !oplug || !oplug -> oids_free )
		return (long)-1;

	used = oplug -> oids_free( &get_super_private( s ) -> oid_allocator );
	if( used < ( __u64 )( ( long )~0 ) >> 1 )
		return ( long )used;
	else
		return ( long )-1;
}

/** ->statfs() VFS method in reiser4 super_operations */
/* Audited by: umka (2002.06.12) */
static int reiser4_statfs( struct super_block *super /* super block of file
						      * system in queried */, 
			   struct statfs *buf /* buffer to fill with
					       * statistics */)
{
	REISER4_ENTRY( super );

	assert( "nikita-408", super != NULL );
	assert( "nikita-409", buf != NULL );


	buf -> f_type    = statfs_type( super );
        /* applications use this not to know what is the block size, but to
         * know what is the optimal size for performing IOs, it is
         * mis-named. So we give them what they want.
	 * 
	 * FIXME-NIKITA why this is constant? Hans? Shouldn't it depend on
	 * super block?
	 *
	 * As for applications, they use ->st_blksize field as reported by
	 * stat(2).
	 * FIXME-GREEN: This is used by df and friends as a blocksize, so I set
	 * it to blocksize. Also SUSv2 devines f_bsize as block size, so
	 * it is probably wrong linux manpage that cause all the confusion.
	 */
	buf -> f_bsize   = super->s_blocksize;
	buf -> f_blocks  = reiser4_block_count( super );
	buf -> f_bfree   = reiser4_free_blocks( super );
	buf -> f_bavail  = buf -> f_bfree - 
		reiser4_reserved_blocks( super, 0, 0 );
	buf -> f_files   = oids_used( super );
	buf -> f_ffree   = oids_free( super );
	/* maximal acceptable name length depends on directory plugin. */
	buf -> f_namelen = -1;
	REISER4_EXIT( 0 );
}

/*
 * address space operations
 */

/** ->writepage() VFS method in reiser4 address_space_operations */
/* Audited by: umka (2002.06.12) */
static int reiser4_writepage( struct page *page )
{	
	int result;
	file_plugin * fplug;
	jnode * j;
	REISER4_ENTRY( page -> mapping -> host -> i_sb );


	trace_on( TRACE_VFS_OPS, "WRITEPAGE: (i_ino %li, page index %lu)\n",
		  page -> mapping -> host -> i_ino, page ->index );

	fplug = inode_file_plugin( page -> mapping -> host );
	if( !PagePrivate( page ) ) {
		/* there is no jnode */
		result = fplug -> writepage( page );
		if( result )
			REISER4_EXIT( result );
	} else {
		/* there is jnode. Call writepage if it has no disk mapping */
		j = jnode_of_page( page );
		if( !jnode_mapped( j ) ) {
			result = fplug -> writepage( page );
			if( result )
				REISER4_EXIT( result );
		}
		jput( j );
	}
	result = 1;
	REISER4_EXIT( page_common_writeback( page, &result, 
					     JNODE_FLUSH_MEMORY_UNFORMATTED ) );
}

/** ->readpage() VFS method in reiser4 address_space_operations */
static int reiser4_readpage( struct file *f /* file to read from */, 
			     struct page *page /* page where to read data
						* into */ )
{
	struct inode *inode;
	file_plugin  *fplug;
	int           result;
	REISER4_ENTRY( f -> f_dentry -> d_inode -> i_sb );
	
	assert( "umka-078", f != NULL );
	assert( "umka-079", page != NULL );
	assert( "nikita-2280", PageLocked( page ) );
	
	assert( "vs-318", page -> mapping && page -> mapping -> host );
	assert( "nikita-1352", 
		( f == NULL ) ||
		( f -> f_dentry -> d_inode == page -> mapping -> host ) );

	inode = page -> mapping -> host;
	fplug = inode_file_plugin( inode );
	if( fplug -> readpage != NULL )
		result = fplug -> readpage( f, page );
	else
		result = -EINVAL;
	if( result != 0 ) {
		/*
		 * callers don't check ->readpage() return value, so we have
		 * to kill page ourselves.
		 */
		remove_inode_page( page );
		page_cache_release(page);
		unlock_page( page );
	}
	REISER4_EXIT( result );
}

static int reiser4_vm_writeback( struct page *page, int *nr_to_write )
{
	return page_common_writeback( page, nr_to_write, JNODE_FLUSH_MEMORY_UNFORMATTED);
}

/* ->sync_page()
   ->writepages()
   ->vm_writeback()
   ->set_page_dirty()
   ->readpages()
   ->prepare_write()
   ->commit_write()
*/   

/* ->bmap() VFS method in reiser4 address_space_operations */
static int reiser4_bmap(struct address_space * mapping, long block)
{
	file_plugin * fplug;

	assert( "vs-693", mapping && mapping -> host );

	fplug = inode_file_plugin( mapping -> host );
	if( !fplug || !fplug -> get_block ) {
		return -EINVAL;
	}

	return generic_block_bmap( mapping, (sector_t)block, fplug -> get_block );
}

/* ->invalidatepage()
   ->releasepage()
*/

/* 
 * FIXME-VS: 
 * for some reasons we are not satisfied with address space's readpages() method.
 *
 * Andrew Morton says:
 * Probably, we make do_page_cache_readahead an a_op.  It's a pretty small
 * function, so the fs can take a copy and massage it to suit.
 * We'll have to get that a_op to pass back to page_cache_readahead() the
 * start/nr_pages which it actually did start I/O against, so the readahead
 * logic can adjust its state.
 *
 * So, if/when this method will be added - the below is reiser4's
 * implementation
 * @start_page - number of page to start readahead from
 * @intrafile_readahead_amount - number of pages to issue i/o for
 * return value: number of pages for which i/o is started
 */
int reiser4_do_page_cache_readahead (struct file * file,
				     unsigned long start_page,
				     unsigned long intrafile_readahead_amount)
{
	int result = 0;
	struct inode * inode;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	file_plugin * fplug;
	item_plugin * iplug;
	unsigned long last_page, cur_page;
	

	assert ("vs-754", file && file->f_dentry && file->f_dentry->d_inode);
	inode = file->f_dentry->d_inode;
	if (inode->i_size == 0)
		return 0;

	coord_init_zero (&coord);
	init_lh (&lh);

	/* next page after last one */
	last_page = ((inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT);
	if (start_page + intrafile_readahead_amount > last_page)
		/* do not read past current file size */
		intrafile_readahead_amount = last_page - start_page;

	/* make sure that we can calculate a key by inode and offset we want to
	 * read from */
	fplug = inode_file_plugin (inode);
	assert ("vs-755", fplug && fplug->key_by_inode);

	cur_page = start_page;
	while (intrafile_readahead_amount) {
		/* calc key of next page to readahead */
		fplug->key_by_inode (inode, (loff_t)cur_page << PAGE_CACHE_SHIFT, &key);

		result = find_next_item (file, &key, &coord, &lh, ZNODE_READ_LOCK);
		if (result != CBK_COORD_FOUND) {
			break;
		}

		result = zload (coord.node);
		if (result) {
			break;
		}

		iplug = item_plugin_by_coord (&coord);
		if (!iplug->s.file.page_cache_readahead) {
			result = -EINVAL;
			zrelse (coord.node);
			break;
		}
		/* item's readahead returns number of pages for which readahead
		 * is started */
		result = iplug->s.file.page_cache_readahead (file, &coord, &lh,
							     cur_page,
							     intrafile_readahead_amount);
		zrelse (coord.node);
		if (result <= 0) {
			break;
		}
		assert ("vs-794", (unsigned long)result <= intrafile_readahead_amount);
		intrafile_readahead_amount -= result;
		cur_page += result;
	}
	done_lh (&lh);
	return result <= 0 ? result : (cur_page - start_page);
}


/**
 * ->link() VFS method in reiser4 inode_operations
 *
 * entry point for ->link() method.
 *
 * This is installed as ->link inode operation for reiser4
 * inodes. Delegates all work to object plugin
 */
/* Audited by: umka (2002.06.12) */
static int reiser4_link( struct dentry *existing /* dentry of existing
						  * object */, 
			 struct inode *parent /* parent directory */, 
			 struct dentry *where /* new name for @existing */ )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY( parent -> i_sb );
	
	assert( "umka-080", existing != NULL );
	assert( "nikita-1031", parent != NULL );
	
	dplug = inode_dir_plugin( parent );
	assert( "nikita-1430", dplug != NULL );
	if( dplug -> link != NULL ) {
		result = dplug -> link( parent, existing, where );
		if( result == 0 ) {
			d_instantiate( where, existing -> d_inode );
		}
	} else {
		result = -EPERM;
	}
	REISER4_EXIT( result );
}

static loff_t reiser4_llseek( struct file *file, loff_t off, int origin )
{
	loff_t        result;
	file_plugin  *fplug;
	struct inode *inode = file -> f_dentry -> d_inode;
	loff_t     ( *seek_fn )( struct file *, loff_t, int );
	REISER4_ENTRY( inode -> i_sb );
	
	trace_on( TRACE_VFS_OPS, "llseek: (i_ino %li, size %lld): off %lli, origin %d\n",
		  inode -> i_ino, inode -> i_size, off, origin );

	fplug = inode_file_plugin( inode );
	assert( "nikita-2291", fplug != NULL );
	seek_fn = fplug -> seek ? : default_llseek;
	result = seek_fn( file, off, origin );
	REISER4_EXIT( result );
}

typedef struct readdir_actor_args {
	void        *dirent;
	filldir_t    filldir;
	struct file *dir;
	__u64        skip;
	__u64        skipped;
	reiser4_key  key;
} readdir_actor_args;

/**
 * reiser4_readdir() - our readdir() method.
 *
 * readdir(2)/getdents(2) interface is based on implicit assumption that
 * readdir can be restarted from any particular point by supplying file
 * system with off_t-full of data. That is, file system fill ->d_off
 * field in struct dirent and later user passes ->d_off to the
 * seekdir(3), which is, actually, implemented by glibc as lseek(2) on
 * directory.
 *
 * Reiser4 cannot restart readdir from 64 bits of data, because two last
 * components of the key of directory entry are unknown, which given 128
 * bits: locality and type fields in the key of directory entry are
 * always known, to start readdir() from given point objectid and offset
 * fields have to be filled.
 *
 * To work around this, implementation stores highest 64 bits of
 * information (objectid of the next directory entry to be read) in the
 * file ->f_pos field and last 64 bits (offset of the next directory
 * entry) in the file system specific data attached to the file
 * descriptor (->private_data field in struct file, allocated lazily by
 * reiser4_get_file_fsdata() and recycled in reiser4_release()).
 *
 * As a result, successive calls to readdir() work correctly. If user
 * manually calls lseek (seekdir) on a directory, highest 64 bits of
 * next entry key are reset and lower 64 bits are cleared (FIXME-NIKITA
 * tbd). Next ->readdir() will proceed from "roughly" the same point.
 *
 */
/* Audited by: umka (2002.06.12) */
static int reiser4_readdir( struct file *f /* directory file being read */, 
			    void *dirent /* opaque data passed to us by VFS */, 
			    filldir_t filldir /* filler function passed to us
					       * by VFS */ )
{
	int                 result;
	struct inode       *inode;
	coord_t             coord;
	readdir_actor_args  arg;
	lock_handle lh;

	REISER4_ENTRY( f -> f_dentry -> d_inode -> i_sb );
	
	assert( "nikita-1359", f != NULL );
	inode = f -> f_dentry -> d_inode;
	assert( "nikita-1360", inode != NULL );

	if( ! S_ISDIR( inode -> i_mode ) )
		REISER4_EXIT( -ENOTDIR );

	/*init_coord( &coord );*/
	coord_init_zero( &coord );
	init_lh( &lh );

	result = inode_dir_plugin( inode ) -> readdir_key( f, &arg.key );

	trace_on( TRACE_DIR | TRACE_VFS_OPS, 
		  "readdir: inode: %lli offset: %lli\n", 
		  (__u64) inode -> i_ino, f -> f_pos );
	trace_if( TRACE_DIR | TRACE_VFS_OPS, print_key( "readdir", &arg.key ) );

	if( result == 0 ) {
		result = coord_by_key( tree_by_inode( inode ), &arg.key, 
				       &coord, &lh, 
				       ZNODE_READ_LOCK, FIND_MAX_NOT_MORE_THAN,
				       LEAF_LEVEL, LEAF_LEVEL, 0 );
		if( result == CBK_COORD_FOUND ) {
			reiser4_file_fsdata *fsdata;

			arg.dirent   = dirent;
			arg.filldir  = filldir;
			arg.dir      = f;
			arg.skip     = reiser4_get_file_fsdata( f ) -> dir.skip;
			arg.skipped  = 0;

			result = iterate_tree
				( tree_by_inode( inode ), &coord, &lh, 
				  readdir_actor, &arg, ZNODE_READ_LOCK, 1 );
			/*
			 * if end of the tree or extent was reached
			 * during scanning. That's fine.
			 */
			if( result == -ENAVAIL )
				result = 0;

			f -> f_version = inode -> i_version;
			f -> f_pos = get_key_objectid( &arg.key );
			fsdata = reiser4_get_file_fsdata( f );
			fsdata -> dir.readdir_offset = get_key_offset( &arg.key );
			fsdata -> dir.skip = arg.skip;
		}
	}

	UPDATE_ATIME( inode );
	done_lh( &lh );

	REISER4_EXIT( result );
}


/** ->mmap() VFS method in reiser4 file_operations */
static int reiser4_mmap (struct file * file, struct vm_area_struct * vma)
{
	struct inode * inode;
	int            result;
	REISER4_ENTRY (file -> f_dentry -> d_inode -> i_sb);

	trace_on( TRACE_VFS_OPS, "MMAP: (i_ino %li, size %lld)\n",
		  file -> f_dentry -> d_inode -> i_ino,
		  file -> f_dentry -> d_inode -> i_size );

	inode = file->f_dentry->d_inode;
	if (inode_file_plugin (inode)->mmap == NULL)
		result = -ENOSYS;
	else
		result = inode_file_plugin (inode)->mmap (file, vma);
	REISER4_EXIT (result);
}


/* Audited by: umka (2002.06.12) */
static int unlink_file( struct inode *parent /* parent directory */, 
			struct dentry *victim /* name of object being
					       * unlinked */ )
{
	int         result;
	dir_plugin *dplug;
	REISER4_ENTRY( parent -> i_sb );

	assert( "nikita-1435", parent != NULL );
	assert( "nikita-1436", victim != NULL );

	trace_on( TRACE_DIR | TRACE_VFS_OPS, "unlink: %li/%s\n",
		  ( long ) parent -> i_ino, victim -> d_name.name );

	dplug = inode_dir_plugin( parent );
	assert( "nikita-1429", dplug != NULL );
	if( dplug -> unlink != NULL )
		result = dplug -> unlink( parent, victim );
	else
		result = -EPERM;
	/* 
	 * @victim can be already removed from the disk by this time. Inode is
	 * then marked so that iput() wouldn't try to remove stat data. But
	 * inode itself is still there.
	 */
	REISER4_EXIT( result );
}

/** 
 * ->unlink() VFS method in reiser4 inode_operations
 *
 * remove link from @parent directory to @victim object: delegate work
 * to object plugin
 */
/* Audited by: umka (2002.06.12) */
static int reiser4_unlink( struct inode *parent /* parent directory */, 
			   struct dentry *victim /* name of object being
						  * unlinked */ )
{
	assert( "nikita-2011", parent != NULL );
	assert( "nikita-2012", victim != NULL );
	assert( "nikita-2013", victim -> d_inode != NULL );
	if( inode_dir_plugin( victim -> d_inode ) == NULL )
		return unlink_file( parent, victim );
	else
		return -EISDIR;
}

/** 
 * ->rmdir() VFS method in reiser4 inode_operations
 *
 * The same as unlink, but only for directories.
 *
 */
/* Audited by: umka (2002.06.12) */
static int reiser4_rmdir( struct inode *parent /* parent directory */, 
			  struct dentry *victim /* name of directory being
						 * unlinked */ )
{
	assert( "nikita-2014", parent != NULL );
	assert( "nikita-2015", victim != NULL );
	assert( "nikita-2016", victim -> d_inode != NULL );

	if( inode_dir_plugin( victim -> d_inode ) != NULL )
		/* there is no difference between unlink and rmdir for
		 * reiser4 */
		return unlink_file( parent, victim );
	else
		return -ENOTDIR;
}

/** ->permission() method in reiser4_inode_operations. */
/* Audited by: umka (2002.06.12) */
static int reiser4_permission( struct inode *inode /* object */, 
			       int mask /* mode bits to check permissions
					 * for */ )
{
	int result;
	REISER4_ENTRY( inode -> i_sb );
	assert( "nikita-1687", inode != NULL );

	result = perm_chk( inode, mask, inode, mask ) ? -EACCES : 0;
	REISER4_EXIT( result );
}

/**
 * update inode stat-data by calling plugin
 */
int reiser4_write_sd( struct inode *object )
{
	file_plugin *fplug;

	assert( "nikita-2338", object != NULL );

	fplug = inode_file_plugin( object );
	assert( "nikita-2339", fplug != NULL );
	return fplug -> write_sd_by_inode( object );
}

/**
 * helper function: increase inode nlink count and call plugin method to save
 * updated stat-data.
 *
 * Used by link/create and during creation of dot and dotdot in mkdir
 */
int reiser4_add_nlink( struct inode *object /* object to which link is added */,
		       int write_sd_p /* true is stat-data has to be
				       * updated */ )
{
	file_plugin *fplug;
	int          result;

	assert( "nikita-1351", object != NULL );

	fplug = inode_file_plugin( object );
	assert( "nikita-1445", fplug != NULL );

	/* ask plugin whether it can add yet another link to this
	   object */
	if( !fplug -> can_add_link( object ) ) {
		return -EMLINK;
	}

	assert( "nikita-2211", fplug -> add_link != NULL );
	/* call plugin to do actual addition of link */
	result = fplug -> add_link( object );
	if( ( result == 0 ) && write_sd_p )
		result = fplug -> write_sd_by_inode( object );
	return result;
}

/**
 * helper function: decrease inode nlink count and call plugin method to save
 * updated stat-data.
 *
 * Used by unlink/create
 */
/* Audited by: umka (2002.06.12) */
int reiser4_del_nlink( struct inode *object /* object from which link is
					     * removed */,
		       int write_sd_p /* true is stat-data has to be
				       * updated */ )
{
	file_plugin *fplug;
	int          result;

	assert( "nikita-1349", object != NULL );

	fplug = inode_file_plugin( object );
	assert( "nikita-1350", fplug != NULL );
	assert( "nikita-1446", object -> i_nlink > 0 );
	assert( "nikita-2210", fplug -> rem_link != NULL );

	/* call plugin to do actual deletion of link */
	result = fplug -> rem_link( object );
	if( ( result == 0 ) && write_sd_p )
		result = fplug -> write_sd_by_inode( object );
	return result;
}

/** call ->create() directory plugin method. */
/* Audited by: umka (2002.06.12) */
static int invoke_create_method( struct inode *parent /* parent directory */, 
				 struct dentry *dentry /* dentry of new
							* object */, 
				 reiser4_object_create_data *data /* information
								   * necessary
								   * to create
								   * new
								   * object */ )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY( parent -> i_sb );

	assert( "nikita-426", parent != NULL );
	assert( "nikita-427", dentry != NULL );
	assert( "nikita-428", data != NULL );

	dplug = inode_dir_plugin( parent );
	if( dplug == NULL )
		result = -ENOTDIR;
	else if( dplug -> create_child != NULL ) {
		struct inode *child;

		result = dplug -> create_child( parent, dentry, data );
		child = dentry -> d_inode;
		if( unlikely( result != 0 ) ) {
			if( child != NULL ) {
				dentry -> d_inode = NULL;
				reiser4_make_bad_inode( child );
				iput( child );
			}
		} else
			d_instantiate( dentry, child );
	} else
		result = -EPERM;

	REISER4_EXIT( result );
}

/** helper function: call object plugin to truncate file to @size */
/* Audited by: umka (2002.06.12) */
int truncate_object( struct inode *inode /* object to truncate */, 
		     loff_t size /* size to truncate object to */ )
{
	file_plugin *fplug;

	assert( "nikita-1026", inode != NULL );
	assert( "nikita-1027", is_reiser4_inode( inode ) );
	assert( "nikita-1028", inode -> i_sb != NULL );

	fplug = inode_file_plugin( inode );
	assert( "vs-142", fplug != NULL );

	if( fplug -> truncate != NULL ) {
		int result;
		result = fplug -> truncate( inode, size );
		if( result != 0 ) {
			warning( "nikita-1602", "Truncate error: %i for %li",
				 result, inode -> i_ino );
		}
		return result;
	} else {
		return -EPERM;
	}
}

/** initial prefix of names of pseudo-files like ..plugin, ..acl,
    ..whatnot, ..and, ..his, ..dog 

    Reminder: This is an optional style convention, not a requirement.
    If anyone builds in any dependence in the parser or elsewhere on a
    prefix existing for all pseudo files, and thereby hampers creating
    pseudo-files without this prefix, I will be pissed.  -Hans */
static const char PSEUDO_FILES_PREFIX[] = "..";

/** check whether "name" represents one of reiser4 `pseudo' files like
    host/..plugin, host/..acl etc. If so, return new inode with operations
    set. Otherwise return NULL */
/* this was coded exactly the way it was not supposed to be coded and was removed. */
/*
 * FIXME-NIKITA nikita: more descriptive comment would be appreciated
 */

/**
 * Return and lazily allocate if necessary per-dentry data that we
 * attach to each dentry.
 */
/* Audited by: umka (2002.06.12) */
reiser4_dentry_fsdata *reiser4_get_dentry_fsdata( struct dentry *dentry /* dentry
									 * queried */ )
{
	assert( "nikita-1365", dentry != NULL );

	if( dentry -> d_fsdata == NULL ) {
		reiser4_stat_file_add( fsdata_alloc );
		/* FIXME-NIKITA use slab in stead */
		dentry -> d_fsdata = reiser4_kmalloc( sizeof( reiser4_dentry_fsdata ),
					      GFP_KERNEL );
		if( dentry -> d_fsdata == NULL )
			return ERR_PTR( -ENOMEM );
		xmemset( dentry -> d_fsdata, 0, sizeof( reiser4_dentry_fsdata ) );
	}
	return dentry -> d_fsdata;
}

void reiser4_free_dentry_fsdata( struct dentry *dentry /* dentry released */ )
{
	if( dentry -> d_fsdata != NULL )
		reiser4_kfree( dentry -> d_fsdata, 
			       sizeof( reiser4_dentry_fsdata ) );
}

/** Release reiser4 dentry. This is d_op->d_delease() method. */
/* Audited by: umka (2002.06.12) */
static void reiser4_d_release( struct dentry *dentry /* dentry released */ )
{
	__REISER4_ENTRY( dentry -> d_sb, );
	reiser4_free_dentry_fsdata( dentry );
	__REISER4_EXIT( &__context );
}

/**
 * Return and lazily allocate if necessary per-file data that we attach
 * to each struct file.
 */
/* Audited by: umka (2002.06.12) */
reiser4_file_fsdata *reiser4_get_file_fsdata( struct file *f /* file
							      * queried */ )
{
	assert( "nikita-1603", f != NULL );

	if( f -> private_data == NULL ) {
		reiser4_stat_file_add( private_data_alloc );
		/* FIXME-NIKITA use slab in stead */
		f -> private_data = reiser4_kmalloc( sizeof( reiser4_file_fsdata ),
					     GFP_KERNEL );
		if( f -> private_data == NULL )
			return ERR_PTR( -ENOMEM );
		xmemset( f -> private_data, 0, sizeof( reiser4_file_fsdata ) );
	}
	return f -> private_data;
}


static const char *tail_status( const struct inode *inode )
{
	if (!inode_get_flag( inode, REISER4_TAIL_STATE_KNOWN ) )
		return "unknown";
	if (inode_get_flag( inode, REISER4_HAS_TAIL ) )
		return "tail";
	return "notail";		
}


/** Release reiser4 file. This is f_op->release() method. Called when last
 * holder closes a file */
/* Audited by: umka (2002.06.12) */
static int reiser4_release( struct inode *i /* inode released */,
			    struct file *f /* file released */ )
{
	file_plugin *fplug;
	int result;
	REISER4_ENTRY( i -> i_sb );
	
	assert( "umka-081", i != NULL );
	assert( "nikita-1447", f != NULL );

	fplug = inode_file_plugin( i );
	assert( "umka-082", fplug != NULL );
	
	trace_on( TRACE_VFS_OPS, "RELEASE: (i_ino %li, size %lld, tail status: %s)\n",
		  i -> i_ino, i -> i_size, tail_status( i ) );

	if( fplug -> release )
		result = fplug -> release( f );
	else
		result = 0;

	if( f -> private_data != NULL )
		reiser4_kfree( f -> private_data, 
			       sizeof( reiser4_file_fsdata ) );

	REISER4_EXIT( result );
}

/**
 * Function that is called by reiser4_readdir() on each directory item
 * while doing readdir.
 */
/* Audited by: umka (2002.06.12) */
static int readdir_actor( reiser4_tree *tree UNUSED_ARG /* tree scanned */,
			  coord_t *coord /* current coord */,
			  lock_handle *lh UNUSED_ARG /* current lock
							      * handle */, 
			  void *arg /* readdir arguments */ )
{
	readdir_actor_args  *args;
	file_plugin         *fplug;
	item_plugin         *iplug;
	struct inode        *inode;
	char                *name;
	reiser4_key          de_key;
	reiser4_key          sd_key;

	assert( "nikita-1367", tree != NULL );
	assert( "nikita-1368", coord != NULL );
	assert( "nikita-1369", arg != NULL );
	

	args = arg;
	inode = args -> dir -> f_dentry -> d_inode;
	assert( "nikita-1370", inode != NULL );

	if( item_id_by_coord( coord ) !=
	    item_id_by_plugin( inode_dir_item_plugin( inode ) ) )
		return 0;

	fplug = inode_file_plugin( inode );
	assert( "umka-083", fplug != NULL );
	
	if( ! fplug -> owns_item( inode, coord ) ) {
		return 0;
	}

	iplug = item_plugin_by_coord( coord );
	assert( "umka-084", iplug != NULL );
	
	name = iplug -> s.dir.extract_name( coord );
	assert( "nikita-1371", name != NULL );
	if( iplug -> s.dir.extract_key( coord, &sd_key ) != 0 ) {
		return -EIO;
	}
	/* get key of directory entry */
	unit_key_by_coord( coord, &de_key );
	/*
	 * skip some entries that we already processed during previous
	 * readdir()
	 */
	if( keyeq( &de_key, &args -> key ) ) {
		++ args -> skipped;
		if( args -> skipped <= args -> skip ) {
			return 1;
		}
		++ args -> skip;
		assert( "nikita-1756", args -> skip == args -> skipped );
	} else {
		assert( "nikita-1720", keygt( &de_key, &args -> key ) );
		args -> skipped = args -> skip = 1;
		args -> key = de_key;
	}

	trace_on( TRACE_DIR | TRACE_VFS_OPS,
		  "readdir_actor: %lli: name: %s, off: 0x%llx, ino: %lli\n",
		  ( __u64 ) inode -> i_ino, name, get_key_objectid( &de_key ),
		  get_key_objectid( &sd_key ) );

	/*
	 * send information about directory entry to the ->filldir() filler
	 * supplied to us by caller (VFS).
	 */
	if( args -> filldir( args -> dirent, name, ( int ) strlen( name ),
			     /* FIXME-NIKITA into kassign.c */
			     /*
			      * offset of the next entry
			      */
			     ( loff_t ) get_key_objectid( &de_key ), 
			     /* FIXME-NIKITA into kassign.c */
			     /*
			      * inode number of object bounden by this entry
			      */
			     ( ino_t ) get_key_objectid( &sd_key ), 
			     iplug -> s.dir.extract_file_type( coord ) ) < 0 ) {
		/*
		 * ->filldir() is satisfied.
		 *
		 * Last item wasn't passed to the user, so it shouldn't be
		 * skipped on the next readdir.
		 */
		assert( "nikita-1721", args -> skip > 0 );
		-- args -> skip;
		return 0;
	}
	/* continue with the next entry */
	return 1;
}

/** our ->read_inode() is no-op. Reiser4 inodes should be loaded
    through fs/reiser4/inode.c:reiser4_iget() */
static void noop_read_inode( struct inode *inode UNUSED_ARG )
{}

/***************************************************
 * initialisation and shutdown
 ***************************************************/

/** slab cache for inodes */
static kmem_cache_t *inode_cache;

/**
 * initalisation function passed to the kmem_cache_create() to init new pages
 * grabbed by our inodecache.
 */
/* Audited by: umka (2002.06.12) */
static void init_once( void *obj /* pointer to new inode */, 
		       kmem_cache_t *cache UNUSED_ARG /* slab cache */,
		       unsigned long flags /* cache flags */ )
{
	struct reiser4_inode_info *info;

	info = obj;

	if( ( flags & ( SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR ) ) == 
	    SLAB_CTOR_CONSTRUCTOR ) {
		/*
		 * FIXME-NIKITA add here initialisations for locks, list
		 * heads, etc. that will be added to our private inode part.
		 */
		/*
		 * FIXME-NIKITA where inode is zeroed?
		 */
		inode_init_once( &info -> vfs_inode );
		spin_lock_init( &info -> guard );
		init_rwsem( &info -> sem );
	}
}

/** initialise slab cache where reiser4 inodes will live */
/* Audited by: umka (2002.06.12) */
int init_inodecache( void )
{
	inode_cache = kmem_cache_create( "reiser4_inode_cache", 
					 sizeof( reiser4_inode_info ), 0, 
					 SLAB_HWCACHE_ALIGN, init_once, NULL );
	return ( inode_cache != NULL ) ? 0 : - ENOMEM;
}

/** initialise slab cache where reiser4 inodes lived */
/* Audited by: umka (2002.06.12) */
static void destroy_inodecache(void)
{
	if( kmem_cache_destroy( inode_cache ) != 0 )
		warning( "nikita-1695", "not all inodes were freed" );
}

/** ->alloc_inode() super operation: allocate new inode */
/* Audited by: umka (2002.06.12) */
static struct inode *reiser4_alloc_inode( struct super_block *super UNUSED_ARG /* super
										* block
										* new
										* inode
										* is
										* allocated
										* for */ )
{
	struct reiser4_inode_info *info;

	assert( "nikita-1696", super != NULL );
	info = kmem_cache_alloc( inode_cache, SLAB_KERNEL );
	if( info != NULL ) {
		info -> flags = 0;
		info -> file  = NULL;
		info -> dir   = NULL;
		info -> perm  = NULL;
		info -> tail  = NULL;
		info -> hash  = NULL;
		info -> sd    = NULL;
		info -> dir_item = NULL;
		info -> bytes = 0ull;
		info -> extmask = 0ull;
		info -> sd_len = 0;
		info -> locality_id = 0ull;
		seal_init( &info -> sd_seal, NULL, NULL );
		coord_init_invalid( &info -> sd_coord, NULL );
		xmemset( &info -> ra, 0, sizeof info -> ra );
		return &info -> vfs_inode;
	} else
		return NULL;
}

/** ->destroy_inode() super operation: recycle inode */
/* Audited by: umka (2002.06.12) */
static void reiser4_destroy_inode( struct inode *inode /* inode being
							* destroyed */ )
{
	assert( "nikita-1697", inode != NULL );
	if( inode_get_flag( inode, REISER4_GENERIC_VP_USED ) ) {
		assert( "vs-839", S_ISLNK( inode -> i_mode ) );
		kfree( inode -> u.generic_ip );
		inode -> u.generic_ip = 0;
		inode_clr_flag( inode, REISER4_GENERIC_VP_USED );
	}
	kmem_cache_free( inode_cache, reiser4_inode_data( inode ) );
}


/**
 * read super block from device and fill remaining fields in @s.
 *
 * This is read_super() of the past.  
 */
/* Audited by: umka (2002.06.12) */
const char *REISER4_SUPER_MAGIC_STRING = "R4Sb";
const int REISER4_MAGIC_OFFSET = 16 * 4096; /* offset to magic string from the
					     * beginning of device */

typedef enum {
	OPT_STRING,
	OPT_BIT,
	OPT_FORMAT,
	OPT_ONEOF
} opt_type_t;

typedef struct opt_desc {
	const char *name;
	opt_type_t type;
	union {
		char *string;
		struct {
			int   nr;
			void *addr;
		} bit;
		struct {
			const char *format;
			int   nr_args;
			void *arg1;
			void *arg2;
			void *arg3;
			void *arg4;
		} f;
		struct {
		} oneof;
	} u;
} opt_desc_t;

static int parse_option( char *opt_string, opt_desc_t *opt )
{
	/* 
	 * foo=bar, 
	 * ^   ^  ^
	 * |   |  +-- replaced to '\0'
	 * |   +-- val_start
	 * +-- opt_string
	 */
	char *val_start;

	val_start = strchr( opt_string, '=' );
	if( val_start != NULL ) {
		*val_start = '\0';
		++ val_start;
	}

	switch( opt -> type ) {
	case OPT_STRING:
		if( val_start == NULL ) {
			warning( "nikita-2101", "Arg missing for \"%s\"",
				 opt -> name );
			return -EINVAL;
		}
		opt -> u.string = val_start;
		break;
	case OPT_BIT:
		if( val_start != NULL )
			warning( "nikita-2097", "Value ignored for \"%s\"",
				 opt -> name );
		set_bit( opt -> u.bit.nr, opt -> u.bit.addr );
		break;
	case OPT_FORMAT:
		if( sscanf( val_start, opt -> u.f.format, 
			    opt -> u.f.arg1,
			    opt -> u.f.arg2,
			    opt -> u.f.arg3,
			    opt -> u.f.arg4 ) != opt -> u.f.nr_args ) {
			warning( "nikita-2098", "Wrong conversion for \"%s\"",
				 opt -> name );
			return -EINVAL;
		}
		break;
	case OPT_ONEOF:
		not_implemented( "nikita-2099", "Oneof" );
		break;
	default:
		wrong_return_value( "nikita-2100", "opt -> type" );
		break;
	}
	return 0;
}

static int parse_options( char *opt_string, opt_desc_t *opts, int nr_opts )
{
	int result;
	
	result = 0;
	while( ( result == 0 ) && opt_string && *opt_string ) {
		int j;
		char *next;

		next = strchr( opt_string, ',' );
		if( next != NULL ) {
			*next = '\0';
			++ next;
		}
		for( j = 0 ; j < nr_opts ; ++ j ) {
			if( !strncmp( opt_string, opts[ j ].name, 
				      strlen( opts[ j ].name ) ) ) {
				result = parse_option( opt_string, &opts[ j ] );
				break;
			}
		}
		if( j == nr_opts ) {
			warning( "nikita-2307", "Unrecognized option: \"%s\"",
				 opt_string );
			/* 
			 * traditionally, -EINVAL is returned on wrong mount
			 * option
			 */
			result = -EINVAL;
		}
		opt_string = next;
	}
	return result;
}

static int reiser4_parse_options( struct super_block * s, char *opt_string )
{
	int result;

	opt_desc_t opts[] = {
		{
			/*
			 * trace=N
			 *
			 * set trace flags to be N for this mount. N can be C
			 * numeric literal recognized by %i scanf specifier.
			 * It is treated as bitfield filled by values of
			 * debug.h:reiser4_trace_flags enum
			 */
			.name = "trace",
			.type = OPT_FORMAT,
			.u = {
				.f = {
					.format  = "%i",
					.nr_args = 1,
					/*
					 * neat gcc feature: allow
					 * non-constant initializers.
					 */
					.arg1 = &get_super_private (s) -> trace_flags,
					.arg2 = NULL,
					.arg3 = NULL,
					.arg4 = NULL
				}
			}
		},
		{
			/*
			 * debug=N
			 *
			 * set debug flags to be N for this mount. N can be C
			 * numeric literal recognized by %i scanf specifier.
			 * It is treated as bitfield filled by values of
			 * debug.h:reiser4_debug_flags enum
			 */
			.name = "debug",
			.type = OPT_FORMAT,
			.u = {
				.f = {
					.format  = "%i",
					.nr_args = 1,
					.arg1 = &get_super_private (s) -> debug_flags,
					.arg2 = NULL,
					.arg3 = NULL,
					.arg4 = NULL
				}
			}
		},
		{
			/*
			 * atom_max_size=N
			 *
			 * Atoms containing more than N blocks will be forced
			 * to commit. N is decimal.
			 */
			.name = "atom_max_size",
			.type = OPT_FORMAT,
			.u = {
				.f = {
					.format  = "%d",
					.nr_args = 1,
					.arg1 = &get_super_private (s) -> txnmgr.atom_max_size,
					.arg2 = NULL,
					.arg3 = NULL,
					.arg4 = NULL
				}
			}
		},
		{
			/*
			 * atom_max_age=N
			 *
			 * Atoms older than N seconds will be forced
			 * to commit. N is decimal.
			 */
			.name = "atom_max_age",
			.type = OPT_FORMAT,
			.u = {
				.f = {
					.format  = "%ld",
					.nr_args = 1,
					.arg1 = &get_super_private (s) -> txnmgr.atom_max_age,
					.arg2 = NULL,
					.arg3 = NULL,
					.arg4 = NULL
				}
			}
		}
	};

	get_super_private (s) -> txnmgr.atom_max_size = REISER4_ATOM_MAX_SIZE;
	get_super_private (s) -> txnmgr.atom_max_age = REISER4_ATOM_MAX_AGE / HZ;
	result = parse_options( opt_string, opts, sizeof_array( opts ) );
	get_super_private (s) -> txnmgr.atom_max_age *= HZ;
	return result;
}

static int reiser4_show_options( struct seq_file *m, struct vfsmount *mnt )
{
	struct super_block      *super;
	reiser4_super_info_data *info;

	super = mnt -> mnt_sb;
	info = get_super_private( super );

	seq_printf( m, ",trace=0x%x", info -> trace_flags );
	seq_printf( m, ",debug=0x%x", info -> debug_flags );
	seq_printf( m, ",atom_max_size=0x%x", info -> txnmgr.atom_max_size );

	seq_printf( m, ",default plugins: file=\"%s\"",
		    default_file_plugin( super ) -> h.label );
	seq_printf( m, ",dir=\"%s\"",
		    default_dir_plugin( super ) -> h.label );
	seq_printf( m, ",hash=\"%s\"",
		    default_hash_plugin( super ) -> h.label );
	seq_printf( m, ",tail=\"%s\"",
		    default_tail_plugin( super ) -> h.label );
	seq_printf( m, ",perm=\"%s\"",
		    default_perm_plugin( super ) -> h.label );
	seq_printf( m, ",dir_item=\"%s\"",
		    default_dir_item_plugin( super ) -> h.label );
	seq_printf( m, ",sd=\"%s\"",
		    default_sd_plugin( super ) -> h.label );
	return 0;
}

extern ktxnmgrd_context kdaemon;

static int reiser4_fill_super (struct super_block * s, void * data,
			       int silent UNUSED_ARG)
{
	struct buffer_head * super_bh;
	struct reiser4_master_sb * master_sb;
	reiser4_super_info_data *info;
	int plugin_id;
	layout_plugin * lplug;
	struct inode * inode;
	int result;
	unsigned long blocksize;
	reiser4_context __context;

	assert( "umka-085", s != NULL );
	
	if (REISER4_DEBUG || REISER4_DEBUG_MODIFY || REISER4_TRACE ||
	    REISER4_STATS || REISER4_DEBUG_MEMCPY)
		warning ("nikita-2372", "Debugging is on. Benchmarking is invalid.");

	/* this is common for every disk layout. It has a pointer where layout
	 * specific part of info can be attached to, though */
	info = kmalloc (sizeof (reiser4_super_info_data), GFP_KERNEL);

	if (!info)
		return -ENOMEM;

	s->u.generic_sbp = info;
	memset (info, 0, sizeof (*info));
	ON_DEBUG (INIT_LIST_HEAD (&info->all_jnodes));

	result = init_context (&__context, s);
	if (result) {
		kfree (info);
		s->u.generic_sbp = NULL;
		return result;
	}

 read_super_block:
	/* look for reiser4 magic at hardcoded place */
	super_bh = sb_bread (s, (int)(REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh) {
		result = -EIO;
		goto error1;
	}
	
	master_sb = (struct reiser4_master_sb *)super_bh->b_data;
	/* check reiser4 magic string */
	result = -EINVAL;
	if (!strncmp (master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4)) {
		/* reset block size if it is not a right one FIXME-VS: better comment is needed */
		blocksize = d16tocpu (&master_sb->blocksize);
		
		if (blocksize != PAGE_CACHE_SIZE) {
			info ("reiser4_fill_super: %s: wrong block size %ld\n",
			      s->s_id, blocksize);
			brelse (super_bh);
			goto error1;
		}
		if (blocksize != s->s_blocksize) {
			brelse (super_bh);
			if (!sb_set_blocksize (s, (int)blocksize)) {
				goto error1;
			}
			goto read_super_block;
		}

		plugin_id = d16tocpu (&master_sb->disk_plugin_id);
		/* only two plugins are available for now */
		assert ("vs-476", (plugin_id == LAYOUT_40_ID ||
				   plugin_id == TEST_LAYOUT_ID));
		lplug = layout_plugin_by_id (plugin_id);
		brelse (super_bh);
	} else {
		/* no standard reiser4 super block found */
		brelse (super_bh);
		/* FIXME-VS: call guess method for all available layout
		 * plugins */
		/* 
		 * umka (2002.06.12) Is it possible when format-specific super
		 * block exists but there no master super block?
		 */
		goto error1;
	}

	s->s_op = &reiser4_super_operations;

	spin_lock_init (&info->guard);

	/* init layout plugin */
	info->lplug = lplug;

	txn_mgr_init (&info->tmgr);

	result = ktxnmgrd_attach (& kdaemon, &info->tmgr);
	if (result) {
		goto error2;
	}

	/* initialize fake inode, formatted nodes will be read/written through
	 * it */
	result = init_formatted_fake (s);
	if (result) {
		goto error2;
	}

	/* call disk format plugin method to do all the preparations like
	 * journal replay, reiser4_super_info_data initialization, read oid
	 * allocator, etc */
	result = lplug->get_ready (s, data);
	if (result) {
		goto error3;
	}

	/*
	 * FIXME-NIKITA actually, options should be parsed by plugins also.
	 */
	result = reiser4_parse_options (s, data);
	if (result) {
		goto error4;
	}

	inode = reiser4_iget (s, lplug->root_dir_key (s));
	if ( IS_ERR( inode ) ) {
		result = PTR_ERR (inode);
		goto error4;
	}
	/* allocate dentry for root inode, It works with inode == 0 */
	s->s_root = d_alloc_root (inode);
	if (!s->s_root) {
		result = -ENOMEM;
		goto error4;
	}
	s->s_root->d_op = &reiser4_dentry_operation;

	if( inode -> i_state & I_NEW ) {
		reiser4_inode_info *info;
		
		info = reiser4_inode_data (inode);
		
		if( info -> file == NULL )
			info -> file = default_file_plugin(s);
		if( info -> dir == NULL )
			info -> dir = default_dir_plugin(s);
		if( info -> sd == NULL )
			info -> sd = default_sd_plugin(s);
		if( info -> hash == NULL )
			info -> hash = default_hash_plugin(s);
		if( info -> tail == NULL )
			info -> tail = default_tail_plugin(s);
		if( info -> perm == NULL )
			info -> perm = default_perm_plugin(s);
		if( info -> dir_item == NULL )
			info -> dir_item = default_dir_item_plugin(s);
		assert( "nikita-1951", info -> file != NULL );
		assert( "nikita-1814", info -> dir  != NULL );
		assert( "nikita-1815", info -> sd   != NULL );
		assert( "nikita-1816", info -> hash != NULL );
		assert( "nikita-1817", info -> tail != NULL );
		assert( "nikita-1818", info -> perm != NULL );
		assert( "vs-545",      info -> dir_item != NULL );
		unlock_new_inode (inode);
	}

	REISER4_EXIT (0);

 error4:
	get_super_private (s)->lplug->release (s);
 error3:
	done_formatted_fake (s);
	/* shutdown daemon */
	ktxnmgrd_detach (&info->tmgr);
 error2:
	txn_mgr_done (&info->tmgr);
 error1:
	kfree (info);
	s->u.generic_sbp = NULL;

	REISER4_EXIT (result);
}

static void reiser4_kill_super (struct super_block *s)
{
	reiser4_super_info_data *info;
	__REISER4_ENTRY (s,);

	info = (reiser4_super_info_data *) s->u.generic_sbp;
	if (!info) {
		/* mount failed */
		s->s_op = 0;
		kill_block_super(s);
		__REISER4_EXIT (&__context);
		return;
	}

	trace_on (TRACE_VFS_OPS, "kill_super\n");

	if (reiser4_is_debugged (s, REISER4_VERBOSE_UMOUNT)) {
		get_current_context() -> trace_flags |= (TRACE_PCACHE|
							 TRACE_TXN|
							 TRACE_FLUSH|
							 TRACE_ZNODES|
							 TRACE_IO_R|
							 TRACE_IO_W);
	}

	/* flushes transactions, etc. */
	get_super_private (s)->lplug->release (s);

	/* shutdown daemon if last mount is removed */
	ktxnmgrd_detach (&info->tmgr);

	done_formatted_fake (s);

	/*
	 * we don't want ->write_super to be called any more.
	 */
	s->s_op->write_super = NULL;
	kill_block_super(s);

#if REISER4_DEBUG
	{
		list_t *scan;

		/*
		 * print jnode that survived umount.
		 */
		list_for_each(scan, &info->all_jnodes) {
			jnode *busy;

			busy = list_entry(scan, jnode, jnodes);
			info_jnode ("\nafter umount", busy);
		}
	}
#endif

	/*
	 * FIXME-VS: here?
	 */
	reiser4_print_stats ();

	/* no assertions below this line */
	__REISER4_EXIT (&__context);

	kfree(info);
	s->u.generic_sbp = NULL;
}

static void reiser4_write_super (struct super_block * s)
{
	int ret;
	__REISER4_ENTRY (s,);

	if ((ret = txn_mgr_force_commit (s))) {
		warning ("jmacd-77113", "txn_force failed in write_super: %d", ret);
	}

	/* Oleg says do this: */
	s->s_dirt = 0;

	__REISER4_EXIT (&__context);
}

/** ->get_sb() method of file_system operations. */
/* Audited by: umka (2002.06.12) */
static struct super_block *reiser4_get_sb( struct file_system_type *fs_type /* file
									     * system
									     * type */,
					   int flags /* flags */, 
					   char *dev_name /* device name */, 
					   void *data /* mount options */ )
{
	return get_sb_bdev( fs_type, flags, dev_name, data, reiser4_fill_super );
}

/** ->invalidatepage method for reiser4 */
int reiser4_invalidatepage( struct page *page, unsigned long offset )
{
	REISER4_ENTRY( page -> mapping -> host -> i_sb );
	if( offset == 0 ) {
		txn_delete_page( page );
		page_clear_jnode( page );
	}
	/*
	 * return with page still locked. truncate_list_pages() expects this.
	 */
	REISER4_EXIT( 0 );
}

/** ->releasepage method for reiser4 */
int reiser4_releasepage( struct page *page, int gfp UNUSED_ARG )
{
	jnode        *node;
	reiser4_tree *tree;

	assert( "nikita-2257", PagePrivate( page ) );
	assert( "nikita-2259", PageLocked( page ) );

	node = jnode_by_page( page );
	assert( "nikita-2258", node != NULL );

	/*
	 * is_page_cache_freeable() check 
	 *
	 * (mapping + private + page_cache_get() by shrink_cache())
	 */
	if( page_count( page ) > 3 )
		return 0;
	if( PageDirty( page ) )
		return 0;

	spin_lock_jnode( node );
	/*
	 * can only release page if real block number is assigned to
	 * it. Simple check for ->atom wouldn't do, because it is possible for
	 * node to be clean, not it atom yet, and still having fake block
	 * number. For example, node just created in jinit_new().
	 */
	if( atomic_read( &node -> d_count ) || jnode_is_loaded( node ) ||
	    blocknr_is_fake( jnode_get_block( node ) ) || 
	    JF_ISSET( node, JNODE_WANDER ) ) {
		spin_unlock_jnode( node );
		return 0;
	}
	page_clear_jnode_nolock( page, node );
	spin_unlock_jnode( node );
	tree = tree_by_page( page );
	page_cache_release( page );

	spin_lock_tree( tree );
	/*
	 * we are under memory pressure so release jnode
	 * also. jdrop() internally re-checks x_count.
	 */
	jdrop_in_tree( node, tree );
	spin_unlock_tree( tree );
	/*
	 * return with page still locked. shrink_cache() expects this.
	 */
	return 1;
}

int reiser4_writepages( struct address_space *mapping UNUSED_ARG, 
			int *nr_to_write UNUSED_ARG )
{
	trace_on( TRACE_PCACHE, "Writepages called and ignored for %li\n",
		  ( long ) mapping -> host -> i_ino );
	return 0;
}

/**
 * initialise reiser4: this is called either at bootup or at module load.
 */
/* Audited by: umka (2002.06.12) I'd reorganize this function in more simple maner. */
static int __init init_reiser4(void)
{
	int result;

	/* This kind of stair-steping code sucks ass. */
	info( KERN_INFO "Loading Reiser4. See www.namesys.com for a description of Reiser4.\n" );
	result = init_inodecache();
	if( result == 0 ) {
		init_context_mgr();
		result = znodes_init();
		if( result == 0 ) {
			result = init_plugins();
			if( result == 0 ) {
				result = txn_init_static();
				if( result == 0 ) {
					result = init_fakes();
					if( result == 0 ) {
						result = register_filesystem ( &reiser4_fs_type );

						if( result == 0 ) {

							result = jnode_init_static ();

							if (result != 0) {
								jnode_done_static ();
							}
						}
					}
				}
			}
			if( result != 0 )
				znodes_done();
		}
		if( result != 0 )
			destroy_inodecache();
	}
	return result;
}

/**
 * finish with reiser4: this is called either at shutdown or at module unload.
 */
static void __exit done_reiser4(void)
{
        unregister_filesystem( &reiser4_fs_type );
	znodes_done();
	destroy_inodecache();
	txn_done_static();
	jnode_done_static(); /* why no error checks here? */
	/*
	 * FIXME-NIKITA more cleanups here
	 */
}

module_init( init_reiser4 );
module_exit( done_reiser4 );

MODULE_DESCRIPTION( "Reiser4 filesystem" );
MODULE_AUTHOR( "Hans Reiser <Reiser@Namesys.COM>" );

/*
 * FIXME-NIKITA is this correct?
 */
MODULE_LICENSE( "GPL" );

/**
 * description of the reiser4 file system type in the VFS eyes.
 */
static struct file_system_type reiser4_fs_type = {
	.owner     = THIS_MODULE,
	.name      = "reiser4",
	.get_sb    = reiser4_get_sb,
	.kill_sb   = reiser4_kill_super,

	/*
	 * FIXME-NIKITA something more?
	 */
	.fs_flags  = FS_REQUIRES_DEV,
	.next      = NULL
};

struct inode_operations reiser4_inode_operations = {
	.create      = reiser4_create, /* d */
	.lookup      = reiser4_lookup, /* d */
	.link        = reiser4_link, /* d */
 	.unlink      = reiser4_unlink, /* d */
	.symlink     = reiser4_symlink, /* d */
	.mkdir       = reiser4_mkdir, /* d */
 	.rmdir       = reiser4_rmdir, /* d */
	.mknod       = reiser4_mknod, /* d */
 	.rename      = reiser4_rename,
 	.readlink    = NULL,
 	.follow_link = NULL,
 	.truncate    = reiser4_truncate, /* d */
 	.permission  = reiser4_permission, /* d */
 	.setattr     = reiser4_setattr,  /* d */
 	.getattr     = reiser4_getattr,  /* d */
/*	.setxattr    = reiser4_setxattr, */
/* 	.getxattr    = reiser4_getxattr, */
/* 	.listxattr   = reiser4_listxattr, */
/* 	.removexattr = reiser4_removexattr */
};

struct file_operations reiser4_file_operations = {
 	.llseek            = reiser4_llseek, /* d */
	.read              = reiser4_read, /* d */
	.write             = reiser4_write, /* d */
 	.readdir           = reiser4_readdir, /* d */
/* 	.poll              = reiser4_poll, */
/* 	.ioctl             = reiser4_ioctl, */
 	.mmap              = reiser4_mmap, /* d */
/* 	.open              = reiser4_open, */
/* 	.flush             = reiser4_flush, */
 	.release           = reiser4_release, /* d */
/* 	.fsync             = reiser4_fsync, */
/* 	.fasync            = reiser4_fasync, */
/* 	.lock              = reiser4_lock, */
/* 	.readv             = reiser4_readv, */
/* 	.writev            = reiser4_writev, */
/* 	.sendpage          = reiser4_sendpage, */
/* 	.get_unmapped_area = reiser4_get_unmapped_area */
};

struct inode_operations reiser4_symlink_inode_operations = {
 	.readlink    = reiser4_readlink,
 	.follow_link = reiser4_follow_link
};

define_never_ever_op( prepare_write_vfs )
define_never_ever_op( commit_write_vfs )
define_never_ever_op( direct_IO_vfs )

#define V( func ) ( ( void * ) ( func ) )

struct address_space_operations reiser4_as_operations = {
	/** called from write_one_page(). Not sure how this is to be used. */
	.writepage      = reiser4_writepage,
	/** 
	 * called to read page from the storage when page is added into page
	 * cache 
	 */
	.readpage       = reiser4_readpage,
	/**
	 * This is most annoyingly misnomered method. Actually it is called
	 * from wait_on_page_bit() and lock_page() and its purpose is to
	 * actually start io by jabbing device drivers.
	 */
 	.sync_page      = block_sync_page,
	/** called during sync (pdflush) */
	.writepages     = NULL,/*reiser4_writepages,*/
	/** called during memory pressure by kswapd */
	.vm_writeback   = reiser4_vm_writeback,
	.set_page_dirty = __set_page_dirty_nobuffers,
	/** called during read-ahead */
	.readpages      = NULL,
	.prepare_write  = V( never_ever_prepare_write_vfs ),
	.commit_write   = V( never_ever_commit_write_vfs ),
 	.bmap           = reiser4_bmap,
	/**
	 * called just before page is taken out from address space (on
	 * truncate, umount, or similar). 
	 */
	.invalidatepage = reiser4_invalidatepage,
	/**
	 * called when VM is about to take page from address space (due to
	 * memory pressure).
	 */
	.releasepage    = reiser4_releasepage,
	.direct_IO      = V( never_ever_direct_IO_vfs )
};

struct super_operations reiser4_super_operations = {
   	.alloc_inode        = reiser4_alloc_inode, /* d */
	.destroy_inode      = reiser4_destroy_inode, /* d */
	.read_inode         = noop_read_inode, /* d */
/* 	.dirty_inode        = reiser4_dirty_inode, */
/* 	.write_inode        = reiser4_write_inode, */
/* 	.put_inode          = reiser4_put_inode, */
/* 	.delete_inode       = reiser4_delete_inode, */
	.put_super          = NULL,
 	.write_super        = reiser4_write_super,
/* 	.write_super_lockfs = reiser4_write_super_lockfs, */
/* 	.unlockfs           = reiser4_unlockfs, */
 	.statfs             = reiser4_statfs, /* d */
/* 	.remount_fs         = reiser4_remount_fs, */
/* 	.clear_inode        = reiser4_clear_inode, */
/* 	.umount_begin       = reiser4_umount_begin,*/
/* 	.fh_to_dentry       = reiser4_fh_to_dentry, */
/* 	.dentry_to_fh       = reiser4_dentry_to_fh */
	.show_options       = reiser4_show_options /* d */
};

struct dentry_operations reiser4_dentry_operation = {
	.d_revalidate = NULL,
	.d_hash       = NULL,
	.d_compare    = NULL,
	.d_delete     = NULL,
	.d_release    = reiser4_d_release,
	.d_iput       = NULL,
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

