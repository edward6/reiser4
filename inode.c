/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Inode specific operations.
 */

#include "reiser4.h"

/** return pointer to reiser4-specific part of inode */
/* Audited by: green(2002.06.17) */
reiser4_inode_info *reiser4_inode_data( const struct inode *inode /* inode
								   * queried */ )
{
	assert( "nikita-254", inode != NULL );
	return list_entry( inode, reiser4_inode_info, vfs_inode );
}

/** return reiser4 internal tree which inode belongs to */
/* Audited by: green(2002.06.17) */
reiser4_tree *tree_by_inode( const struct inode *inode /* inode queried */ )
{
	assert( "nikita-256", inode != NULL );
	assert( "nikita-257", inode -> i_sb != NULL );
	return get_tree( inode -> i_sb );
}

/** return internal reiser4 inode flags, stored as part of object plugin
    state in reiser4-specific part of inode */
/* Audited by: green(2002.06.17) */
__u32 *reiser4_inode_flags( const struct inode *inode /* inode queried */ )
{
	assert( "nikita-267", inode != NULL );

	return & reiser4_inode_data( inode ) -> flags;
}

/** lock inode. We lock file-system wide spinlock, because we have to lock
 *  inode _before_ we have actually read and initialised it and we cannot rely
 *  on memset() in fs/inode.c to initialise spinlock. Alternative is to grab
 *  i_sem, but it's semaphore rather than spinlock, so it's not clear what
 *  would be more effective.
 *
 *  Taking inode->i_sem is simple and scalable, but taking and releasing
 *  semaphore is much more expensive than taking spin-lock. So, for the time
 *  being, let's just pile a number of possible locking schemes here and
 *  choose best (or leave them as options) after benchmarking.
 *
 *  This is because we dont't have enough empirical evidence about scalability
 *  of each scheme.
 *
 * FIXME-NIKITA ->i_sem is not we actually won't. May be spinlock is better
 * after all.
 */
/* Audited by: green(2002.06.17) */
void reiser4_lock_inode( struct inode *inode /* inode to lock */ )
{
	assert( "nikita-272", inode != NULL );

	down( &inode -> i_sem );
}

/**
 * Same as reiser4_lock_inode(), but signals are allowed to wake current
 * thread up while it is sleeping waiting for the inode lock.
 *
 * This is preferrable approach as it allows user to kill process stuck in "D"
 * state waiting for inode. Unfortunately there are situations when it is
 * inconvenient.
 *
 */
/* Audited by: green(2002.06.17) */
int reiser4_lock_inode_interruptible( struct inode *inode /* inode to lock */ )
{
	assert( "nikita-1270", inode != NULL );

	return down_interruptible( &inode -> i_sem );
}

/**
 * Release lock on inode.
 *
 */
/* Audited by: green(2002.06.17) */
void reiser4_unlock_inode( struct inode *inode /* inode to unlock */ )
{
	assert( "nikita-277", inode != NULL );

	up( &inode -> i_sem );
}

/** check that "inode" is on reiser4 file-system */
/* Audited by: green(2002.06.17) */
int is_reiser4_inode( const struct inode *inode /* inode queried */ )
{
	return( ( inode != NULL ) && 
		( is_reiser4_super( inode -> i_sb ) ||
		  ( inode -> i_op == &reiser4_inode_operations ) ) );
		
}

/**
 * Maximal length of a name that can be stored in directory @inode.
 *
 * This is used in check during file creation and lookup.
 */
/* Audited by: green(2002.06.17) */
int reiser4_max_filename_len( const struct inode *inode /* inode queried */ )
{
	assert( "nikita-287", is_reiser4_inode( inode ) );
	assert( "nikita-1710", inode_dir_item_plugin( inode ) );
	if( inode_dir_item_plugin( inode ) -> s.dir.max_name_len )
		return inode_dir_item_plugin( inode ) -> 
			s.dir.max_name_len( reiser4_blksize( inode -> i_sb ) );
	else
		return 255;
}

/**
 * Maximal number of hash collisions for this directory.
 */
/* Audited by: green(2002.06.17) */
int max_hash_collisions( const struct inode *dir /* inode queried */ )
{
	assert( "nikita-1711", dir != NULL );
#if REISER4_USE_COLLISION_LIMIT
	return reiser4_inode_data( dir ) -> plugin.max_collisions;
#else
	return ~0;
#endif
}

/** return information about "repetitive access" (ra) patterns,
    accumulated in inode. */
/* Audited by: green(2002.06.17) */
inter_syscall_rap *inter_syscall_ra( const struct inode *inode /* inode
								* queried */ )
{
	assert( "nikita-289", is_reiser4_inode( inode ) );
	return &reiser4_inode_data( inode ) -> ra;
}

/* should be moved into .h */

#if REISER4_DEBUG
/*  has inode been initialized? */
/* Audited by: green(2002.06.17) */
static int is_inode_loaded( const struct inode *inode /* inode queried */ )
{
	assert( "nikita-1120", inode != NULL );
	return reiser4_inode_data( inode ) -> flags & REISER4_LOADED;
}
#endif

/**
 * Install file, inode, and address_space operation on @inode, depending on
 * its mode.
 */
/* Audited by: green(2002.06.17) */
int setup_inode_ops( struct inode *inode /* inode to intialise */ )
{
	switch( inode -> i_mode & S_IFMT ) {
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO: {
		int rdev; /* to keep gcc happy */

		/* ugly hack with rdev */
		rdev = kdev_val (inode -> i_rdev);
		inode -> i_rdev = val_to_kdev( 0 );
		inode -> i_blocks = 0;
		/* I guess that not only i_blocks, but also at least i_size should be zeroed */
		init_special_inode( inode, inode -> i_mode, rdev );
		break;
	}
	case S_IFLNK:
	case S_IFDIR:
	case S_IFREG:
		inode -> i_op = &reiser4_inode_operations;
		inode -> i_fop = &reiser4_file_operations;
		inode -> i_mapping -> a_ops = &reiser4_as_operations;
		break;
	default:
		warning( "nikita-291", "wrong file mode: %o for %lx", 
			 inode -> i_mode, ( long ) inode -> i_ino );
		reiser4_make_bad_inode( inode );
		return -EINVAL;
	}
	return 0;
}

/** initialise inode from disk data. Called with inode locked.
    Return inode locked. */
/* Audited by: green(2002.06.17) */
int init_inode( struct inode *inode /* inode to intialise */, 
		coord_t *coord /* coord of stat data */ )
{
	int result;
	item_plugin    *iplug;
	void           *body;
	int             length;
	reiser4_inode_info *state;

	assert( "nikita-292", coord != NULL );
	assert( "nikita-293", inode != NULL );
	assert( "nikita-1946", inode -> i_state & I_NEW );

	result = zload( coord -> node );
	if( result )
		return result;
	iplug  = item_plugin_by_coord( coord );
	body   = item_body_by_coord  ( coord );
	length = item_length_by_coord( coord );

	assert( "nikita-295", iplug != NULL );
	assert( "nikita-296", body != NULL );
	assert( "nikita-297", length > 0 );

	state = reiser4_inode_data( inode );
	spin_lock_inode( state );
	/* call stat-data plugin method to load sd content into inode */
	result = iplug -> s.sd.init_inode( inode, body, length );
	state -> sd = iplug;
	spin_unlock_inode( state );
	if( result == 0 ) {
		result = setup_inode_ops( inode );
		if( ( result == 0 ) && ( inode -> i_sb -> s_root ) &&
		    ( inode -> i_sb -> s_root -> d_inode ) ) {
			reiser4_inode_info *self;
			reiser4_inode_info *root;

			/* take missing plugins from file-system defaults */
			self = reiser4_inode_data( inode );
			root = reiser4_inode_data
				( inode -> i_sb -> s_root -> d_inode );
			/*
			 * file and directory plugins are already initialised.
			 */
			if( self -> sd == NULL )
				self -> sd = root -> sd;
			if( self -> hash == NULL )
				self -> hash = root -> hash;
			if( self -> tail == NULL )
				self -> tail = root -> tail;
			if( self -> perm == NULL )
				self -> perm = root -> perm;
			if( self -> dir_item == NULL )
				self -> dir_item = root -> dir_item;
		}
	}
	zrelse( coord -> node );
	return result;
}

/* read `inode' from the disk. This is what was previously in 
   reiserfs_read_inode2().

   Must be called with inode locked. Return inode still locked.
*/
/* Audited by: green(2002.06.17) */
static void read_inode( struct inode * inode /* inode to read from disk */,
			const reiser4_key *key /* key of stat data */ )
{
	int                 result;
	lock_handle         lh;
	reiser4_inode_info *info;
	coord_t          coord;

	assert( "nikita-298", inode != NULL );
	assert( "nikita-1945", !is_inode_loaded( inode ) );

	info = reiser4_inode_data( inode );
	assert( "nikita-300", info -> locality_id != 0 );

	coord_init_zero( &coord );
	init_lh( &lh );
	/* locate stat-data in a tree and return znode locked */
	result = lookup_sd_by_key( tree_by_inode( inode ), 
				   ZNODE_READ_LOCK, &coord, &lh, key );
	assert( "nikita-301", !is_inode_loaded( inode ) );
	if( result == 0 ) {
		*reiser4_inode_flags( inode ) |= REISER4_LOADED;
		/* use stat-data plugin to load sd into inode. */
		result = init_inode( inode, &coord );
		if( result == 0 ) {
			/* initialise stat-data seal */
			spin_lock_inode( info );
			seal_init( &info -> sd_seal, &coord, key );
			info -> sd_coord = coord;
			spin_unlock_inode( info );
		}
	}
	/* lookup_sd() doesn't release coord because we want znode
	   stay read-locked while stat-data fields are accessed in
	   init_inode() */
	done_lh( &lh );

	if( result != 0 ) {
		reiser4_make_bad_inode( inode );
		unlock_new_inode( inode );
	}
}

/**
 * initialise new reiser4 inode being inserted into hash table.
 */
/* Audited by: green(2002.06.17) */
static int init_locked_inode( struct inode *inode /* new inode */, 
			      void *opaque /* key of stat data passed to the
					    * iget5_locked as cookie */ )
{
	reiser4_key *key;

	assert( "nikita-1995", inode != NULL );
	assert( "nikita-1996", opaque != NULL );
	key = opaque;
	inode -> i_ino = get_key_objectid( key );
	reiser4_inode_data( inode ) -> locality_id = get_key_locality( key );
	return 0;
}


/**
 * reiser4_inode_find_actor() - "find actor" supplied by reiser4 to iget5_locked().
 *
 * This function is called by iget5_locked() to distinguish reiserfs inodes
 * having the same inode numbers. Such inodes can only exist due to some error
 * condition. One of them should be bad. Inodes with identical inode numbers
 * (objectids) are distinguished by their packing locality.
 *
 */
/* Audited by: green(2002.06.17) */
int reiser4_inode_find_actor( struct inode *inode /* inode from hash table to
						   * check */,
			      void *opaque /* "cookie" passed to
					    * iget5_locked(). This is stat data
					    * key */ )
{
	reiser4_key *key;
	
	key = opaque;
	return 
		( inode -> i_ino == get_key_objectid( key ) ) &&
		( reiser4_inode_data( inode ) -> locality_id == get_key_locality( key ) );
}

/** this is our helper function a la iget().
    Probably we also need function taking locality_id as the second argument. ???
    This will be called by reiser4_lookup() and reiser4_read_super().
    Return inode locked or error encountered. 
*/
/* Audited by: green(2002.06.17) */
struct inode *reiser4_iget( struct super_block *super /* super block  */, 
			    const reiser4_key *key /* key of inode's
						    * stat-data */ )
{
	struct inode *inode;

	assert( "nikita-302", super != NULL );
	assert( "nikita-303", key != NULL );

	/** call iget(). Our ->read_inode() is dummy, so this will either
	    find inode in cache or return uninitialised inode */
	/*
	 * FIXME-NIKITA it is supposed that 2.5 kernels with use 64 bit inode
	 * numbers. If they will not at the time of reiser4 inclusion,
	 * find_actor has to be used to distinguish inodes by (lowest?) 32
	 * bits of objectid.
	 */
	inode = iget5_locked( super, ( unsigned long ) get_key_objectid( key ), 
			      reiser4_inode_find_actor, init_locked_inode, 
			      ( reiser4_key * ) key );
	if( inode == NULL ) 
		return ERR_PTR( -ENOMEM );
	else if( is_bad_inode( inode ) ) {
		warning( "nikita-304", "Stat data not found" );
		print_key( "key", key );
	} else if( inode -> i_state & I_NEW ) {
		/* locking: iget5_locked returns locked inode */
		assert( "nikita-1941", ! is_inode_loaded( inode ) );
		assert( "nikita-1949", reiser4_inode_find_actor( inode, 
						   ( reiser4_key * ) key ) );
		/* now, inode has objectid as -> i_ino and locality in
		   reiser4-specific part. This data is enough for read_inode()
		   to read stat data from the disk */
		read_inode( inode, key );
	}
	if( is_bad_inode( inode ) ) {
		iput( inode );
		inode = ERR_PTR( -EIO );
	} else if( REISER4_DEBUG ) {
		reiser4_key found_key;
				
		build_sd_key( inode, &found_key );
		if( !keyeq( &found_key, key ) ) {
			warning( "nikita-305", "Wrong key in sd" );
			print_key( "sought for", key );
			print_key( "found", &found_key );
		}
	}
	return inode;
}

/* Audited by: green(2002.06.17) */
void reiser4_make_bad_inode( struct inode *inode )
{
	assert( "nikita-1934", inode != NULL );
	
	/* clear LOADED bit */
	reiser4_inode_data( inode ) -> flags &= ~REISER4_LOADED;
	make_bad_inode( inode );
	return;
}

/* Audited by: green(2002.06.17) */
file_plugin *inode_file_plugin( const struct inode *inode )
{
	assert( "nikita-1997", inode != NULL );
	return reiser4_inode_data( inode ) -> file;
}

/* Audited by: green(2002.06.17) */
dir_plugin  *inode_dir_plugin ( const struct inode *inode )
{
	assert( "nikita-1998", inode != NULL );
	return reiser4_inode_data( inode ) -> dir;
}

/* Audited by: green(2002.06.17) */
perm_plugin *inode_perm_plugin( const struct inode *inode )
{
	assert( "nikita-1999", inode != NULL );
	return reiser4_inode_data( inode ) -> perm;
}

/* Audited by: green(2002.06.17) */
tail_plugin *inode_tail_plugin( const struct inode *inode )
{
	assert( "nikita-2000", inode != NULL );
	return reiser4_inode_data( inode ) -> tail;
}

/* Audited by: green(2002.06.17) */
hash_plugin *inode_hash_plugin( const struct inode *inode )
{
	assert( "nikita-2001", inode != NULL );
	return reiser4_inode_data( inode ) -> hash;
}

/* Audited by: green(2002.06.17) */
item_plugin *inode_sd_plugin( const struct inode *inode )
{
	assert( "vs-534", inode != NULL );
	return reiser4_inode_data( inode ) -> sd;
}

/* Audited by: green(2002.06.17) */
item_plugin *inode_dir_item_plugin( const struct inode *inode )
{
	assert( "vs-534", inode != NULL );
	return reiser4_inode_data( inode ) -> dir_item;
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


