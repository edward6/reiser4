/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
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
static int reiser4_symlink (struct inode *,struct dentry *,const char *);
static int reiser4_mkdir (struct inode *,struct dentry *,int);
static int reiser4_rmdir (struct inode *,struct dentry *);
static int reiser4_mknod (struct inode *,struct dentry *,int,int);
static int reiser4_rename (struct inode *, struct dentry *,
		    struct inode *, struct dentry *);
static int reiser4_readlink (struct dentry *, char *,int);
static int reiser4_follow_link (struct dentry *, struct nameidata *);
static void reiser4_truncate (struct inode *);
static int reiser4_permission (struct inode *, int);
static int reiser4_revalidate (struct dentry *);
static int reiser4_setattr (struct dentry *, struct iattr *);
static int reiser4_getattr (struct dentry *, struct iattr *);
static int reiser4_setxattr(struct dentry *, const char *, void *, size_t, int);
static ssize_t reiser4_getxattr(struct dentry *, const char *, void *, size_t);
static ssize_t reiser4_listxattr(struct dentry *, char *, size_t);
static int reiser4_removexattr(struct dentry *, const char *);

/* file operations */

static loff_t reiser4_llseek (struct file *, loff_t, int);
static ssize_t reiser4_read (struct file *, char *, size_t, loff_t *);
static ssize_t reiser4_write (struct file *, char *, size_t, loff_t *);
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


static void reiser4_dirty_inode (struct inode *);
static void reiser4_write_inode (struct inode *, int);
static void reiser4_put_inode (struct inode *);
static void reiser4_delete_inode (struct inode *);
static void reiser4_put_super (struct super_block *);
static void reiser4_write_super (struct super_block *);
static void reiser4_write_super_lockfs (struct super_block *);
static void reiser4_unlockfs (struct super_block *);
static int reiser4_statfs (struct super_block *, struct statfs *);
static int reiser4_remount_fs (struct super_block *, int *, char *);
static void reiser4_clear_inode (struct inode *);
static void reiser4_umount_begin (struct super_block *);
static struct dentry * reiser4_fh_to_dentry(struct super_block *sb, __u32 *fh, 
					    int len, int fhtype, int parent);
static int reiser4_dentry_to_fh(struct dentry *, __u32 *fh, 
				int *lenp, int need_parent);


static int reiser4_writepage(struct page *);
static int reiser4_readpage(struct file *, struct page *);
static int reiser4_sync_page(struct page *);
static int reiser4_prepare_write(struct file *, 
				 struct page *, unsigned, unsigned);
static int reiser4_commit_write(struct file *, 
				struct page *, unsigned, unsigned);
static int reiser4_bmap(struct address_space *, long);
static int reiser4_direct_IO(int, struct inode *, 
			     struct kiobuf *, unsigned long, int);

static struct dentry_operations reiser4_dentry_operation;

static int            create_object  ( struct inode *parent, 
				       struct dentry *dentry, 
				       reiser4_object_create_data *data );
static struct dentry *lookup_object  ( struct inode *parent, 
				       struct dentry *dentry );


static int readdir_actor( reiser4_tree *tree, 
			  tree_coord *coord, reiser4_lock_handle *lh,
			  void *arg );

/**
 * reiser4_lookup() - entry point for ->lookup() method.
 *
 * This is installed in ->lookup() in reiser4_inode_operations.
 */
static struct dentry *reiser4_lookup( struct inode *parent, 
				      struct dentry *dentry )
{
	struct dentry *result;
	REISER4_ENTRY_PTR( parent -> i_sb );

	assert( "nikita-1030", parent != NULL );

	/*
	 * we are trying to do finer grained locking than BKL. Lock
	 * inode in question and release BKL. Hopefully BKL was only
	 * taken once by VFS. 
	 */
	if( reiser4_lock_inode_interruptible( parent ) != 0 )
		return ERR_PTR( -EINTR );
	unlock_kernel();
	result = lookup_object( parent, dentry );
	lock_kernel();
	reiser4_unlock_inode( parent );

	REISER4_EXIT_PTR( result );
}

/** 
 * `directory' lookup wrapper. 
 *
 * Called by reiser4_lookup(). Call plugin to perform real lookup. If
 * this fails try lookup_pseudo(). 
 */
static struct dentry *lookup_object( struct inode *parent, 
				     struct dentry *dentry )
{
	dir_plugin          *dplug;
	struct inode        *inode;
	reiser4_key          key;
	reiser4_entry        entry;

	const char          *name;
	int                  len;

	assert( "nikita-403", parent != NULL );
	assert( "nikita-404", dentry != NULL );

	dplug = reiser4_get_dir_plugin( parent );

	/* FIXME-HANS: is this okay? */
	if( dplug == NULL || !dplug -> lookup ) {
		return ERR_PTR( -ENOTDIR );
	}

	/* check permissions */
	if( perm_chk( parent, lookup, parent, dentry ) )
		return ERR_PTR( -EPERM );

	inode = NULL;
	name = dentry -> d_name.name;
	len  = dentry -> d_name.len;

	/*
	 * set up operations on dentry. 
	 *
	 * FIXME-NIKITA this also has to be done for root dentry somewhere?
	 */
	dentry -> d_op = &reiser4_dentry_operation;

	if( dplug -> is_name_acceptable && 
	    !dplug -> is_name_acceptable( parent, name, len ) ) {
		/* some arbitrary error code to return */
		return ERR_PTR( -ENAMETOOLONG );
	}

	memset( &entry, 0, sizeof entry );

	switch( dplug -> lookup( parent, &dentry -> d_name, &key, &entry ) ) {
	default: wrong_return_value( "nikita-407", "->lookup()" );
	case FILE_IO_ERROR:
		return ERR_PTR( -EIO );
	case FILE_OOM:
		return ERR_PTR( -ENOMEM );
	case FILE_NAME_NOTFOUND:
		/*
		 * FIXME-NIKITA here should go lookup of pseudo files. Hans
		 * decided that existing implementation
		 * (lookup_pseudo()) was unsatisfactory.
		 */
		break;
	case FILE_NAME_FOUND: {
		__u32 *flags;

		inode = reiser4_iget( parent -> i_sb, &key );
		if( inode == NULL )
			return ERR_PTR( -EACCES );
		flags = reiser4_inode_flags( inode );
		if( *flags & REISER4_LIGHT_WEIGHT_INODE ) {
			inode -> i_uid = parent -> i_uid;
			inode -> i_gid = parent -> i_gid;
			/* clear light-weight flag. If inode would be
			   read by any other name, [ug]id wouldn't
			   change. */
			*flags &= ~REISER4_LIGHT_WEIGHT_INODE;
		}
		reiser4_unlock_inode( inode );
	}
	}
	d_add( dentry, inode );
	return NULL;
}

/**
 * ->create() VFS method in reiser4 inode_operations
 */
static int reiser4_create( struct inode *parent, 
			   struct dentry *dentry, int mode )
{
	reiser4_object_create_data data;
	
	data.mode = S_IFREG | mode;
	data.id   = REGULAR_FILE_PLUGIN_ID;
	return create_object( parent, dentry, &data );
}

/**
 * ->mkdir() VFS method in reiser4 inode_operations
 */
static int reiser4_mkdir( struct inode *parent, struct dentry *dentry, int mode )
{
	reiser4_object_create_data data;
	
	data.mode = S_IFDIR | mode;
	data.id   = DIRECTORY_FILE_PLUGIN_ID;
	return create_object( parent, dentry, &data );
}

/**
 * ->symlink() VFS method in reiser4 inode_operations
 */
static int reiser4_symlink( struct inode *parent, struct dentry *dentry, 
			    const char *linkname )
{
	reiser4_object_create_data data;
	
	data.name = linkname;
	data.id   = SYMLINK_FILE_PLUGIN_ID;
	data.mode = S_IFLNK | S_IRWXUGO;
	return create_object( parent, dentry, &data );
}

/**
 * ->mknod() VFS method in reiser4 inode_operations
 */
static int reiser4_mknod( struct inode *parent, struct dentry *dentry, 
			  int mode, int rdev )
{
	reiser4_object_create_data data;
	
	data.mode = mode;
	data.rdev = rdev;
	data.id   = SPECIAL_FILE_PLUGIN_ID;
	return create_object( parent, dentry, &data );
}

/**
 * ->read() VFS method in reiser4 file_operations
 */
static ssize_t reiser4_read( struct file *file, 
			     char *buf, size_t size, loff_t *off )
{
	file_plugin *fplug;
	ssize_t result;
	
	REISER4_ENTRY( file -> f_dentry -> d_inode -> i_sb );
	fplug = reiser4_get_file_plugin( file -> f_dentry -> d_inode );
	assert( "nikita-417", fplug != NULL );

	if( fplug->read == NULL ) {
		result = -EPERM;
	} else {
		result = fplug -> read ( file, buf, size, off );
	}

	REISER4_EXIT( result );
}

/**
 * ->write() VFS method in reiser4 file_operations
 */
static ssize_t reiser4_write( struct file *file, char *buf, 
			      size_t size, loff_t *off )
{
	file_plugin *fplug;
	ssize_t result;

	REISER4_ENTRY( file -> f_dentry -> d_inode -> i_sb );
	
	assert( "nikita-1421", file != NULL );
	assert( "nikita-1422", buf != NULL );
	assert( "nikita-1424", off != NULL );

	fplug = reiser4_get_file_plugin( file -> f_dentry -> d_inode );

	if( fplug -> write != NULL ) {
		result = fplug -> write( file, buf, size, off );
	} else {
		result = -EPERM;
	}

	REISER4_EXIT( result );
}

/**
 * ->truncate() VFS method in reiser4 inode_operations
 */
static void reiser4_truncate( struct inode *inode )
{
	__REISER4_ENTRY( inode -> i_sb, );
	truncate_object( inode, inode -> i_size );
	/* 
	 * for mysterious reasons ->truncate() VFS call doesn't return
	 * value 
	 */
	__REISER4_EXIT( &__context );
	return;
}

/**
 * ->statfs() VFS method in reiser4 super_operations
 */
static int reiser4_statfs( struct super_block *super, struct statfs *buf )
{
	reiser4_oid_allocator *oidmap;
	REISER4_ENTRY( super );

	assert( "nikita-408", super != NULL );
	assert( "nikita-409", buf != NULL );


	buf -> f_type    = reiser4_statfs_type( super );
	buf -> f_bsize   = reiser4_blksize( super );
	buf -> f_blocks  = reiser4_data_blocks( super );
	buf -> f_bfree   = reiser4_free_blocks( super );
	buf -> f_bavail  = buf -> f_bfree - reiser4_reserved_blocks( super, 0, 0 );
	oidmap = reiser4_get_oid_allocator( super );
	buf -> f_files   = reiser4_oids_used( oidmap );
	buf -> f_ffree   = 
		reiser4_maximal_oid( oidmap ) - 
		reiser4_minimal_oid( oidmap ) - buf -> f_files;
	/* maximal acceptable name length depends on directory plugin. */
	buf -> f_namelen = -1;
	REISER4_EXIT( 0 );
}

/*
 * address space operations
 */

/**
 * ->writepage() VFS method in reiser4 address_space_operations
 */
static int reiser4_writepage( struct page *page UNUSED_ARG )
{
	return -ENOSYS;
}

/**
 * ->readpage() VFS method in reiser4 address_space_operations
 */
static int reiser4_readpage( struct file *f, struct page *page )
{
	struct inode *inode;
	file_plugin *fplug;

	assert( "vs-318", page -> mapping && page -> mapping -> host );
	assert( "nikita-1352", 
		f -> f_dentry -> d_inode == page -> mapping -> host );

	inode = page -> mapping -> host;
	fplug = reiser4_get_file_plugin( inode );
	if( !fplug -> readpage ) {
		return -EINVAL;
	}
	return fplug -> readpage( f, page );
}


/**
 * ->link() VFS method in reiser4 inode_operations
 *
 * entry point for ->link() method.
 *
 * This is installed as ->link inode operation for reiser4
 * inodes. Delegates all work to object plugin
 */
static int reiser4_link( struct dentry *existing, 
			 struct inode *parent, struct dentry *where )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY( parent -> i_sb );

	assert( "nikita-1031", parent != NULL );
	
	/* is this dead-lock safe? FIXME-NIKITA */
	if( reiser4_lock_inode_interruptible( parent ) != 0 )
		REISER4_EXIT( -EINTR );
	if( reiser4_lock_inode_interruptible( existing -> d_inode ) != 0 ) {
		reiser4_unlock_inode( parent );
		REISER4_EXIT( -EINTR );
	}
	unlock_kernel();
	dplug = reiser4_get_dir_plugin( parent );
	assert( "nikita-1430", dplug != NULL );
	if( dplug -> link != NULL ) {
		result = dplug -> link( parent, existing, where );
	} else {
		result = -EPERM;
	}
	lock_kernel();
	reiser4_unlock_inode( existing -> d_inode );
	reiser4_unlock_inode( parent );
	
	REISER4_EXIT( result );
}

typedef struct readdir_actor_args {
	void        *dirent;
	filldir_t    filldir;
	struct file *dir;
	__u64        offset_hi;
	__u64        offset_lo;
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
static int reiser4_readdir( struct file *f /* directory file being read */, 
			    void *dirent /* opaque data passed to us by VFS */, 
			    filldir_t filldir /* filler function passed to us
					       * by VFS */ )
{
	int           result;
	struct inode *inode;
	reiser4_key   key;
	tree_coord   coord;
	reiser4_lock_handle lh;

	REISER4_ENTRY( f -> f_dentry -> d_inode -> i_sb );

	assert( "nikita-1359", f != NULL );
	inode = f -> f_dentry -> d_inode;
	assert( "nikita-1360", inode != NULL );

	if( ! S_ISDIR( inode -> i_mode ) )
		REISER4_EXIT( -ENOTDIR );

	reiser4_init_coord( &coord );
	reiser4_init_lh( &lh );

	result = build_readdir_key( f, &key );
	if( result == 0 ) {
		result = coord_by_key( tree_by_inode( inode ), &key, &coord, &lh,
				       ZNODE_READ_LOCK, FIND_MAX_NOT_MORE_THAN,
				       LEAF_LEVEL, LEAF_LEVEL );
		if( result == CBK_COORD_FOUND ) {
			readdir_actor_args arg;
			reiser4_file_fsdata *fsdata;

			arg.dirent   = dirent;
			arg.filldir  = filldir;
			arg.dir      = f;

			result = reiser4_iterate_tree
				( tree_by_inode( inode ), &coord, &lh, 
				  readdir_actor, &arg, ZNODE_READ_LOCK, 1 );
			/*
			 * if end of the tree or extent was reached
			 * during scanning. That's fine.
			 */
			if( result == -ENAVAIL )
				result = 0;

			f -> f_version = inode -> i_version;
			f -> f_pos = arg.offset_hi;
			fsdata = reiser4_get_file_fsdata( f );
			if( ! IS_ERR( fsdata ) )
				fsdata -> readdir_offset = arg.offset_lo + 1;
			else
				result = PTR_ERR( fsdata );
		}
	}

	UPDATE_ATIME( inode );
	reiser4_done_lh( &lh );
	reiser4_done_coord( &coord );

	REISER4_EXIT( result );
}

/** 
 * ->unlink() VFS method in reiser4 inode_operations
 *
 * remove link from @parent directory to @victim object: delegate work
 * to object plugin
 */
static int reiser4_unlink( struct inode *parent, struct dentry *victim )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY( parent -> i_sb );
 
	assert( "nikita-1435", parent != NULL );
	assert( "nikita-1436", victim != NULL );

	/* is this dead-lock safe? FIXME-NIKITA */
	if( reiser4_lock_inode_interruptible( parent ) != 0 ) {
		REISER4_EXIT( -EINTR );
	}
	if( reiser4_lock_inode_interruptible( victim -> d_inode ) != 0 ) {
		reiser4_unlock_inode( parent );
		REISER4_EXIT( -EINTR );
	}
	unlock_kernel();
	dplug = reiser4_get_dir_plugin( parent );
	assert( "nikita-1429", dplug != NULL );
	if( dplug -> unlink != NULL ) {
		result = dplug -> unlink( parent, victim );
	} else {
		result = -EPERM;
	}
	lock_kernel();
	/* 
	 * @victim can be already removed from the disk by this
	 * time. Inode is then marked so that iput() wouldn't try to
	 * remove stat data. But inode itself is still there. 
	 */
	reiser4_unlock_inode( victim -> d_inode );
	reiser4_unlock_inode( parent );
       
	REISER4_EXIT( result );
}

/**
 * ->permission() method in reiser4_inode_operations.
 */
static int reiser4_permission( struct inode *inode, int mask )
{
	assert( "nikita-1687", inode != NULL );

	return perm_chk( inode, mask, inode, mask ) ? -EACCES : 0;
}

/**
 * helper function: increase inode nlink count and call plugin method to save
 * updated stat-data.
 *
 * Used by link/create and during creation of dot and dotdot in mkdir
 */
int reiser4_add_nlink( struct inode *object )
{
	file_plugin *fplug;

	assert( "nikita-1351", object != NULL );

	fplug = reiser4_get_file_plugin( object );
	assert( "nikita-1445", fplug != NULL );

	/* ask plugin whether it can add yet another link to this
	   object */
	if( !fplug -> can_add_link( object ) ) {
		return -EMLINK;
	}

	if( fplug -> add_link != NULL ) {
		/* call plugin to do actual addition of link */
		return fplug -> add_link( object );
	} else {
		reiser4_plugin *plugin = file_plugin_to_plugin (fplug);
		/* do reasonable default stuff */
		++ object -> i_nlink;
		object -> i_ctime = CURRENT_TIME;
		return plugin -> h.pops->save( object, plugin, NULL /* FIXME-HANS what's this? */ );
	}
}

/**
 * helper function: decrease inode nlink count and call plugin method to save
 * updated stat-data.
 *
 * Used by unlink/create
 */
int reiser4_del_nlink( struct inode *object )
{
	file_plugin *fplug;

	assert( "nikita-1349", object != NULL );

	fplug = reiser4_get_file_plugin( object );
	assert( "nikita-1350", fplug != NULL );

	assert( "nikita-1446", object -> i_nlink > 0 );

	if( fplug -> rem_link != NULL ) {
		/* call plugin to do actual addition of link */
		return fplug -> rem_link( object );
	} else {
		reiser4_plugin *plugin = file_plugin_to_plugin (fplug);
		/* do reasonable default stuff */
		-- object -> i_nlink;
		object -> i_ctime = CURRENT_TIME;
		return plugin -> h.pops->save( object, plugin, NULL /* FIXME-HANS what's this? */ );
	}
}

/**
 * helper function: call object plugin to truncate file to @size
 */
int truncate_object( struct inode *inode, loff_t size )
{
	file_plugin *fplug;

	assert( "nikita-1026", inode != NULL );
	assert( "nikita-1027", is_reiser4_inode( inode ) );
	assert( "nikita-1028", inode -> i_sb != NULL );

	fplug = reiser4_get_file_plugin( inode );
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
reiser4_dentry_fsdata *reiser4_get_dentry_fsdata( struct dentry *dentry )
{
	assert( "nikita-1365", dentry != NULL );

	if( dentry -> d_fsdata == NULL ) {
		reiser4_stat_file_add( fsdata_alloc );
		/* FIXME-NIKITA use slab in stead */
		dentry -> d_fsdata = reiser4_kmalloc( sizeof( reiser4_dentry_fsdata ),
					      GFP_KERNEL );
		if( dentry -> d_fsdata == NULL )
			return ERR_PTR( -ENOMEM );
		memset( dentry -> d_fsdata, 0, sizeof( reiser4_dentry_fsdata ) );
	}
	return dentry -> d_fsdata;
}

/**
 * Release reiser4 dentry. This is d_op->d_delease() method.
 */
static void reiser4_d_release( struct dentry *dentry )
{
	assert( "nikita-1366", dentry != NULL );
	if( dentry -> d_fsdata != NULL )
		reiser4_kfree( dentry -> d_fsdata, 
			       sizeof( reiser4_dentry_fsdata ) );
}

/**
 * Return and lazily allocate if necessary per-file data that we attach
 * to each struct file.
 */
reiser4_file_fsdata *reiser4_get_file_fsdata( struct file *f )
{
	assert( "nikita-1603", f != NULL );

	if( f -> private_data == NULL ) {
		reiser4_stat_file_add( private_data_alloc );
		/* FIXME-NIKITA use slab in stead */
		f -> private_data = reiser4_kmalloc( sizeof( reiser4_file_fsdata ),
					     GFP_KERNEL );
		if( f -> private_data == NULL )
			return ERR_PTR( -ENOMEM );
		memset( f -> private_data, 0, sizeof( reiser4_file_fsdata ) );
	}
	return f -> private_data;
}

/**
 * Release reiser4 file. This is f_op->release() method.
 */
static int reiser4_release( struct inode *i UNUSED_ARG, struct file *f )
{
	assert( "nikita-1447", f != NULL );

	if( f -> private_data != NULL )
		reiser4_kfree( f -> private_data, 
			       sizeof( reiser4_file_fsdata ) );
	return 0;
}

/**
 * Function that is called by reiser4_readdir() on each directory item
 * while doing readdir.
 */
static int readdir_actor( reiser4_tree *tree UNUSED_ARG,
			  tree_coord *coord,
			  reiser4_lock_handle *lh UNUSED_ARG, void *arg )
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
	
	if( item_type_by_coord( coord ) != DIR_ENTRY_ITEM_TYPE ) {
		return 0;
	}

	args = arg;
	inode = args -> dir -> f_dentry -> d_inode;
	assert( "nikita-1370", inode != NULL );
	fplug = reiser4_get_file_plugin( inode );
	if( ! fplug -> owns_item( inode, coord ) ) {
		return 0;
	}

	iplug = item_plugin_by_coord( coord );
	name = iplug -> s.dir.extract_name( coord );
	assert( "nikita-1371", name != NULL );
	if( iplug -> s.dir.extract_key( coord, &sd_key ) != 0 ) {
		return -EIO;
	}
	unit_key_by_coord( coord, &de_key );
	if( args -> filldir( args -> dirent, name, ( int ) strlen( name ),
			     /* FIXME-NIKITA into kassign.c */
			     ( loff_t ) get_key_objectid( &de_key ), 
			     /* FIXME-NIKITA into kassign.c */
			     ( ino_t ) get_key_objectid( &sd_key ), 
			     DT_UNKNOWN ) < 0 ) {
		return 0;
	}
	args -> offset_hi = get_key_objectid( &de_key );
	args -> offset_lo = get_key_offset( &de_key );
	return 1;
}

/*
 * VFS object creation function (not used by reiser4 assignments)
 *
 */
static int create_object( struct inode *parent, struct dentry *dentry, 
			  reiser4_object_create_data *data )
{
	int result;
	file_plugin *fplug;
	REISER4_ENTRY( parent -> i_sb );

	assert( "nikita-426", parent != NULL );
	assert( "nikita-427", dentry != NULL );
	assert( "nikita-428", data != NULL );

	fplug = reiser4_get_file_plugin( parent );
	assert( "nikita-429", fplug != NULL );
	if( fplug -> create != NULL ) {
		result = fplug -> create( parent, dentry, data );
	} else {
		result = -EPERM;
	}
	REISER4_EXIT( result );
}

/** our ->read_inode() is no-op. Reiser4 inodes should be loaded
    through fs/reiser4/inode.c:reiser4_iget() */
static void noop_read_inode( struct inode *inode UNUSED_ARG )
{}

struct inode_operations reiser4_inode_operations = {
	.create      = reiser4_create, /* d */
	.lookup      = reiser4_lookup, /* d */
	.link        = reiser4_link, /* d */
 	.unlink      = reiser4_unlink, /* d */
	.symlink     = reiser4_symlink, /* d */
	.mkdir       = reiser4_mkdir, /* d */
/* 	.rmdir       = reiser4_rmdir, */
	.mknod       = reiser4_mknod, /* d */
/* 	.rename      = reiser4_rename, */
/* 	.readlink    = reiser4_readlink, */
/* 	.follow_link = reiser4_follow_link, */
 	.truncate    = reiser4_truncate, /* d */
 	.permission  = reiser4_permission, /* d */
/* 	.revalidate  = reiser4_revalidate, */
/* 	.setattr     = reiser4_setattr, */
/* 	.getattr     = reiser4_getattr, */
/* 	.getxattr    = reiser4_getxattr, */
/* 	.listxattr   = reiser4_listxattr, */
/* 	.removexattr = reiser4_removexattr */
};

struct file_operations reiser4_file_operations = {
/* 	.llseek            = reiser4_llseek, */
	.read              = reiser4_read, /* d */
	.write             = reiser4_write, /* d */
 	.readdir           = reiser4_readdir, /* d */
/* 	.poll              = reiser4_poll, */
/* 	.ioctl             = reiser4_ioctl, */
/* 	.mmap              = reiser4_mmap, */
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

struct address_space_operations reiser4_as_operations = {
 	.writepage      = reiser4_writepage, 
 	.readpage       = reiser4_readpage, /* d */
/* 	.sync_page      = reiser4_sync_page, */
/* 	.prepare_write  = reiser4_prepare_write, */
/* 	.commit_write   = reiser4_commit_write, */
/* 	.bmap           = reiser4_bmap, */
/* 	.direct_IO      = reiser4_direct_IO */
};

struct super_operations reiser4_super_operations = {
	.read_inode         = noop_read_inode, /* d */
	.read_inode2        = NULL, /* d */
/* 	.dirty_inode        = reiser4_dirty_inode, */
/* 	.write_inode        = reiser4_write_inode, */
/* 	.put_inode          = reiser4_put_inode, */
/* 	.delete_inode       = reiser4_delete_inode, */
/* 	.put_super          = reiser4_put_super, */
/* 	.write_super        = reiser4_write_super, */
/* 	.write_super_lockfs = reiser4_write_super_lockfs, */
/* 	.unlockfs           = reiser4_unlockfs, */
 	.statfs             = reiser4_statfs, /* d */
/* 	.remount_fs         = reiser4_remount_fs, */
/* 	.clear_inode        = reiser4_clear_inode, */
/* 	.umount_begin       = reiser4_umount_begin, */
/* 	.fh_to_dentry       = reiser4_fh_to_dentry, */
/* 	.dentry_to_fh       = reiser4_dentry_to_fh */
};

static struct dentry_operations reiser4_dentry_operation = {
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

