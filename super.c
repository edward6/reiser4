/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Super-block manipulations.
 */

#include "reiser4.h"

const __u32 REISER4_SUPER_MAGIC = 0x52345362; /* (*(__u32 *)"R4Sb"); */

static __u64 reserved_for_gid( const struct super_block *super, gid_t gid );
static __u64 reserved_for_uid( const struct super_block *super, uid_t uid );
static __u64 reserved_for_root( const struct super_block *super );

/** Return reiser4-specific part of super block */
reiser4_super_info_data *
get_super_private_nocheck( const struct super_block *super /* super block
							    * queried */ )
{
	return ( reiser4_super_info_data * )super -> u.generic_sbp;
}

/** Return reiser4-specific part of super block */
reiser4_super_info_data *get_super_private( const struct super_block *super /* super
block queried */ )
{
	assert( "nikita-447", super != NULL );
	assert( "nikita-2245", 	
		( super -> s_op == NULL ) ||
		( super -> s_op == &reiser4_super_operations ) );

	return ( reiser4_super_info_data * )super -> u.generic_sbp;
}

void reiser4_spin_lock_sb (const struct super_block * super)
{
	reiser4_super_info_data * info;

	assert ("zam-464", super != NULL);
	info = get_super_private (super);
	assert ("zam-465", info != NULL);

	spin_lock(& info->guard);
}

void reiser4_spin_unlock_sb (const struct super_block * super)
{
	reiser4_super_info_data * info;

	assert ("zam-466", super != NULL);
	info = get_super_private (super);
	assert ("zam-467", info != NULL);

	spin_unlock (& info->guard);
}

/**
 * Return reiser4 fstype: value that is returned in ->f_type field by statfs()
 */
long statfs_type( const struct super_block *super UNUSED_ARG /* super block
							      * queried */ )
{
	assert( "nikita-448", super != NULL );
	assert( "nikita-449", is_reiser4_super( super ) );
	return ( long ) REISER4_SUPER_MAGIC;
}

/** block size used by file system corresponding to @super */
int reiser4_blksize( const struct super_block *super /* super block queried */ )
{
	assert( "nikita-450", super != NULL );
	assert( "nikita-451", is_reiser4_super( super ) );
	/* FIXME-VS: blocksize has to be 512, 1024, 2048, etc */
	assert ("zam-391", super->s_blocksize > 0);
	return super -> s_blocksize;
}


/* functions to read/modify fields of reiser4_super_info_data */

/** get number of blocks in file system */
__u64 reiser4_block_count( const struct super_block *super /* super block
							      queried */ )
{
	assert( "vs-494", super != NULL );
	assert( "vs-495", is_reiser4_super( super ) );
	return get_super_private( super ) -> block_count;
}

/* set number of block in filesystem */
void reiser4_set_block_count( const struct super_block *super, __u64 nr )
{
	assert( "vs-501", super != NULL );
	assert( "vs-502", is_reiser4_super( super ) );
	get_super_private( super ) -> block_count = nr;
}

/** amount of blocks used (allocated for data) in file system */
__u64 reiser4_data_blocks( const struct super_block *super /* super block
							      queried */ )
{
	assert( "nikita-452", super != NULL );
	assert( "nikita-453", is_reiser4_super( super ) );
	return get_super_private( super ) -> blocks_used;
}

/* set number of block used in filesystem */
void reiser4_set_data_blocks( const struct super_block *super, __u64 nr )
{
	assert( "vs-503", super != NULL );
	assert( "vs-504", is_reiser4_super( super ) );
	get_super_private( super ) -> blocks_used = nr;
}

/** amount of free blocks in file system */
__u64 reiser4_free_blocks( const struct super_block *super /* super block
							      queried */ )
{
	assert( "nikita-454", super != NULL );
	assert( "nikita-455", is_reiser4_super( super ) );
	return get_super_private( super ) -> blocks_free;
}

/* set number of blocks free in filesystem */
void reiser4_set_free_blocks( const struct super_block *super, __u64 nr )
{
	assert( "vs-505", super != NULL );
	assert( "vs-506", is_reiser4_super( super ) );
	get_super_private( super ) -> blocks_free = nr;
}

/** increment reiser4_super_info_data's counter of free blocks */
void reiser4_inc_free_blocks( const struct super_block *super )
{
	assert( "vs-496",
		reiser4_free_blocks( super ) < reiser4_block_count( super ));
	get_super_private( super ) -> blocks_free ++;
}

/**
 * amount of free blocks in file system
 */
__u64 reiser4_free_committed_blocks( const struct super_block *super )
{
	assert( "vs-497", super != NULL );
	assert( "vs-498", is_reiser4_super( super ) );
	return get_super_private( super ) -> blocks_free_committed;
}

/* this is only used once on mount time to number of free blocks in
 * filesystem */
void reiser4_set_free_committed_blocks( const struct super_block *super,
					__u64 nr )
{
	assert( "vs-507", super != NULL );
	assert( "vs-508", is_reiser4_super( super ) );
	get_super_private( super ) -> blocks_free_committed = nr;
}

/* increment reiser4_super_info_data's counter of free committed blocks */
void reiser4_inc_free_committed_blocks( const struct super_block *super )
{
	assert( "vs-499",
		reiser4_free_committed_blocks( super ) <
		reiser4_block_count( super ));
	get_super_private( super ) -> blocks_free_committed ++;
}

/* decrement reiser4_super_info_data's counter of free committed blocks */
void reiser4_dec_free_committed_blocks( const struct super_block *super )
{
	assert( "vs-500",
		reiser4_free_committed_blocks( super ) > 0);
	get_super_private( super ) -> blocks_free_committed --;
}

/** amount of blocks in the file system reserved for @uid and @gid */
long reiser4_reserved_blocks( const struct super_block *super /* super block
								 queried */, 
			      uid_t uid /* user id */, gid_t gid /* group id */ )
{
	long reserved;

	assert( "nikita-456", super != NULL );
	assert( "nikita-457", is_reiser4_super( super ) );

	reserved = 0;
	if( REISER4_SUPPORT_GID_SPACE_RESERVATION )
		reserved += reserved_for_gid( super, gid );
	if( REISER4_SUPPORT_UID_SPACE_RESERVATION )
		reserved += reserved_for_uid( super, uid );
	if( REISER4_SUPPORT_ROOT_SPACE_RESERVATION && ( uid == 0 ) )
		reserved += reserved_for_root( super );
	return reserved;
}

/**
 * get/set value of/to grabbed blocks counter
 */
__u64 reiser4_grabbed_blocks (const struct super_block *super)
{
	assert ("zam-512", super != NULL);
	assert ("zam-513", is_reiser4_super (super));

	return get_super_private (super) -> blocks_grabbed;
}

void reiser4_set_grabbed_blocks (const struct super_block *super, __u64 nr)
{
	assert ("zam-514", super != NULL);
	assert ("zam-515", is_reiser4_super (super));

	get_super_private (super) -> blocks_grabbed = nr;
}

/**
 * get/set value of/to counter of fake allocated formatted blocks
 */
__u64 reiser4_fake_allocated (const struct super_block *super)
{
	assert ("zam-516", super != NULL);
	assert ("zam-517", is_reiser4_super (super));

	return get_super_private (super) -> blocks_fake_allocated;
}

void reiser4_set_fake_allocated (const struct super_block *super, __u64 nr)
{
	assert ("zam-518", super != NULL);
	assert ("zam-519", is_reiser4_super (super));

	get_super_private (super) -> blocks_fake_allocated = nr;
}

/**
 * get/set value of/to counter of fake allocated unformatted blocks
 */
__u64 reiser4_fake_allocated_unformatted (const struct super_block *super)
{
	assert ("zam-516", super != NULL);
	assert ("zam-517", is_reiser4_super (super));

	return get_super_private (super) -> blocks_fake_allocated_unformatted;
}

void reiser4_set_fake_allocated_unformatted (const struct super_block *super, __u64 nr)
{
	assert ("zam-518", super != NULL);
	assert ("zam-519", is_reiser4_super (super));

	get_super_private (super) -> blocks_fake_allocated_unformatted = nr;
}

void reiser4_update_last_written_location( const struct super_block *s, const reiser4_block_nr *block)
{
	reiser4_super_info_data * private = get_super_private(s);

	reiser4_spin_lock_sb(s);
	if (*block < private->block_count) {
		private->last_written_location = *block;
	} else {
		warning ("zam-676", 
			 "block number %llu is too large to be used in a blocknr hint\n",
			 (unsigned long long)*block);
	}
	reiser4_spin_unlock_sb(s);
}

/**
 * objectid allocator used by this file system
 */
reiser4_oid_allocator *get_oid_allocator( const struct super_block *super /* super
									     block
									     queried */ )
{
	assert( "nikita-458", super != NULL );
	assert( "nikita-459", is_reiser4_super( super ) );
	return &get_super_private( super ) -> oid_allocator;
}

/**
 * space allocator used by this file system
 */
reiser4_space_allocator *get_space_allocator( const struct super_block *super )
{
	assert( "nikita-1965", super != NULL );
	assert( "nikita-1966", is_reiser4_super( super ) );
	return &get_super_private( super ) -> space_allocator;
}

/**
 * return fake inode used to bind formatted nodes in the page cache
 */
struct inode *get_super_fake( const struct super_block *super /* super block
								 queried */ )
{
	assert( "nikita-1757", super != NULL );
	return get_super_private( super ) -> fake;
}

/**
 * tree used by this file system
 */
reiser4_tree *get_tree( const struct super_block *super /* super block
							 * queried */ )
{
	assert( "nikita-460", super != NULL );
	assert( "nikita-461", is_reiser4_super( super ) );
	return &get_super_private( super ) -> tree;
}

/**
 * Check that @super is (looks like) reiser4 super block. This is mainly for
 * use in assertions.
 */
int is_reiser4_super( const struct super_block *super /* super block
						       * queried */ )
{
	return ( super != NULL ) && 
		( super -> s_op == &reiser4_super_operations );
}

int reiser4_is_set( const struct super_block *super, reiser4_fs_flag f )
{
	return test_bit( ( int ) f, &get_super_private( super ) -> fs_flags );
}

/**
 * Reiser4-specific part of "current" super-block: main super block used
 * during current system call. Reference to this super block is stored in
 * reiser4_context.
 */
reiser4_super_info_data *get_current_super_private( void )
{
	return get_super_private( reiser4_get_current_sb() );
}

/**
 * "Current" super-block: main super block used during current system
 * call. Reference to this super block is stored in reiser4_context.
 */
struct super_block *reiser4_get_current_sb()
{
	return get_current_context() -> super;
}

/**
 * inode generation to use for the newly created inode
 */
__u32 new_inode_generation( const struct super_block *super /* super block
							     * queried */ )
{
	assert( "nikita-464", is_reiser4_super( super ) );
	return get_super_private( super ) -> inode_generation;
}

/**
 * amount of blocks reserved for given group in file system
 */
static __u64 reserved_for_gid( const struct super_block *super UNUSED_ARG /* super
									   * block
									   * queried */,
			       gid_t gid UNUSED_ARG /* group id */ )
{
	return 0;
}

/**
 * amount of blocks reserved for given user in file system
 */
static __u64 reserved_for_uid( const struct super_block *super UNUSED_ARG /* super
									     block
									     queried */, 
			       uid_t uid UNUSED_ARG /* user id */ )
{
	return 0;
}

/**
 * amount of blocks reserved for super user in file system
 */
static __u64 reserved_for_root( const struct super_block *super UNUSED_ARG /* super
									      block
									      queried */ )
{
	return 0;
}

/** 
 * default file plugin used by this file system.
 * This should actually look at the mount options or something
 */
file_plugin *default_file_plugin( const struct super_block *super UNUSED_ARG /* super
									      * block
									      * to
									      * query */ )
{
	assert( "nikita-1950", super != NULL );

	return file_plugin_by_id( REISER4_FILE_PLUGIN );
}

/** 
 * default dir plugin used by this file system.
 * This should actually look at the mount options or something
 */
dir_plugin *default_dir_plugin( const struct super_block *super UNUSED_ARG /*  super
									    *  block to
									    *  query */ )
{
	assert( "nikita-1967", super != NULL );
	return get_super_private( super ) -> plug.d;
}

/** 
 * default hash plugin used by this file system.
 * This should actually look at the mount options or something
 */
hash_plugin *default_hash_plugin( const struct super_block *super UNUSED_ARG /*  super
									      *  block to
									      *  query */ )
{
	assert( "nikita-1968", super != NULL );
	return get_super_private( super ) -> plug.h;
}

/** 
 * default perm plugin used by this file system.
 * This should actually look at the mount options or something
 */
perm_plugin *default_perm_plugin( const struct super_block *super UNUSED_ARG /*  super
									      *  block to
									      *  query */ )
{
	assert( "nikita-1969", super != NULL );
	return get_super_private( super ) -> plug.p;
}

/** 
 * default tail policy plugin used by this file system.
 * This should actually look at the mount options or something
 */
tail_plugin *default_tail_plugin( const struct super_block *super /*  super
								   *  block to
								   *  query */ )
{
	assert( "nikita-1971", super != NULL );
	return get_super_private( super ) -> plug.t;
}

/** 
 * default sd plugin used by this file system.
 * This should actually look at the mount options or something
 */
item_plugin *default_sd_plugin( const struct super_block *super UNUSED_ARG /*  super
									    *  block to
									    *  query */ )
{
	assert( "nikita-1972", super != NULL );
	return get_super_private( super ) -> plug.sd;
}

/** 
 * default dir item plugin used by this file system.
 * This should actually look at the mount options or something
 */
item_plugin *default_dir_item_plugin( const struct super_block *super UNUSED_ARG /*  super
										  *  block
										  *  to
										  *  *
										  *  query */ )
{
	assert( "nikita-1973", super != NULL );
	return get_super_private( super ) -> plug.dir_item;
}

#if REISER4_DEBUG_OUTPUT
void print_fs_info (const char *prefix, const struct super_block * s)
{
	reiser4_super_info_data * private;

	private = get_super_private (s);

	info ("================ fs info (%s) =================\n", prefix);
	info ("root block: %lli\ntree height: %i\n",
	      private->tree.root_block, private->tree.height);
	if (private->space_plug->print_info)
		private->space_plug->print_info ("", get_space_allocator (s));

	oid_print_allocator (private->oid_plug->h.label, s);
	info ("Block counters:\n\tblock count\t%llu\n\tfree blocks\t%llu\n"
	      "\tused blocks\t%llu\n\tgrabbed\t%llu\n\tfake allocated formatted\t%llu\n"
	      "\tfake allocated unformatted\t%llu\n",
	      reiser4_block_count (s), reiser4_free_blocks (s), reiser4_data_blocks (s),
	      reiser4_grabbed_blocks (s), reiser4_fake_allocated (s),
	      reiser4_fake_allocated_unformatted (s));
	print_key ("Root directory key", private->df_plug->root_dir_key (s));

	if (private->df_plug->print_info) {
		info ("=========== disk format info (%s) =============\n",
		      private->df_plug->h.label);
		private->df_plug->print_info (s);
	}
	
}
#endif

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
