/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Super-block manipulations.
 */

#include "reiser4.h"

const __u32 REISER4_SUPER_MAGIC = 0x52345362; /* (*(__u32 *)"R4Sb"); */
const char *REISER4_SUPER_MAGIC_STRING = "R4Sb";
const int REISER4_MAGIC_OFFSET = 16 * 4096; /* offset to magic string from the
					     * beginning of device */

static __u64 reserved_for_gid( const struct super_block *super, gid_t gid );
static __u64 reserved_for_uid( const struct super_block *super, uid_t uid );
static __u64 reserved_for_root( const struct super_block *super );

/**
 * Return reiser4-specific part of super block
 */
reiser4_super_info_data *
get_super_private_nocheck( const struct super_block *super )
{
	return ( reiser4_super_info_data * )super -> u.generic_sbp;
}

/**
 * Return reiser4-specific part of super block
 */
reiser4_super_info_data *get_super_private( const struct super_block *super )
{
	assert( "nikita-447", super != NULL );
	return ( reiser4_super_info_data * )super -> u.generic_sbp;
}


layout_plugin *get_layout_plugin()
{
}


/**
 * Return reiser4 fstype: value that is returned in ->f_type field by statfs()
 */
long reiser4_statfs_type( const struct super_block *super UNUSED_ARG )
{
	assert( "nikita-448", super != NULL );
	assert( "nikita-449", is_reiser4_super( super ) );
	return ( long ) REISER4_SUPER_MAGIC;
}

/** 
 * block size used by file system corresponding to @super
 */
int reiser4_blksize( const struct super_block *super )
{
	assert( "nikita-450", super != NULL );
	assert( "nikita-451", is_reiser4_super( super ) );
	return super -> s_blocksize;
}


/**
 * amount of blocks used (allocated for data) in file system
 */
long reiser4_data_blocks( const struct super_block *super )
{
	assert( "nikita-452", super != NULL );
	assert( "nikita-453", is_reiser4_super( super ) );
	return get_super_private( super ) -> blocks_used;
}

/**
 * amount of free blocks in file system
 */
long reiser4_free_blocks( const struct super_block *super )
{
	assert( "nikita-454", super != NULL );
	assert( "nikita-455", is_reiser4_super( super ) );
	return get_super_private( super ) -> blocks_free;
}

/**
 * amount of reserved blocks in file system
 */
long reiser4_reserved_blocks( const struct super_block *super, 
			      uid_t uid, gid_t gid )
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

#if 0
/**
 * objectid allocator used by this file system
 */
reiser4_oid_allocator_t *reiser4_get_oid_allocator( const struct super_block *super )
{
	assert( "nikita-458", super != NULL );
	assert( "nikita-459", is_reiser4_super( super ) );
	return &get_super_private( super ) -> allocator;
}
#endif
/**
 * return fake inode used to bind formatted nodes in the page cache
 */
struct inode *reiser4_get_super_fake( const struct super_block *super )
{
	assert( "nikita-1757", super != NULL );
	return get_super_private( super ) -> fake;
}

/**
 * tree used by this file system
 */
reiser4_tree *reiser4_get_tree( const struct super_block *super )
{
	assert( "nikita-460", super != NULL );
	assert( "nikita-461", is_reiser4_super( super ) );
	return &get_super_private( super ) -> tree;
}

/**
 * True if this file system doesn't support hard-links (multiple names) for
 * directories: this is default UNIX behaviour.
 *
 * If hard-links on directoires are not allowed, file system is Acyclic
 * Directed Graph (modulo dot, and dotdot, of course).
 *
 * This is used by reiser4_link().
 *
 */
int reiser4_adg( const struct super_block *super )
{
	return get_super_private( super ) -> adg;
}

/**
 * Check that @super is (looks like) reiser4 super block. This is mainly for
 * use in assertions.
 */
int is_reiser4_super( const struct super_block *super )
{
	return ( super != NULL ) && 
		( super -> s_op == &reiser4_super_operations );
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
	return reiser4_get_current_context() -> super;
}

/**
 * inode generation to use for the newly created inode
 */
__u32 reiser4_new_inode_generation( const struct super_block *super )
{
	assert( "nikita-464", is_reiser4_super( super ) );
	return get_super_private( super ) -> inode_generation;
}

/**
 * amount of blocks reserved for given group in file system
 */
static __u64 reserved_for_gid( const struct super_block *super UNUSE, 
			       gid_t gid UNUSE )
{
	return 0;
}

/**
 * amount of blocks reserved for given user in file system
 */
static __u64 reserved_for_uid( const struct super_block *super UNUSE, 
			       uid_t uid UNUSE )
{
	return 0;
}

/**
 * amount of blocks reserved for super user in file system
 */
static __u64 reserved_for_root( const struct super_block *super UNUSE )
{
	return 0;
}



int reiser4_init_tree( reiser4_tree *tree /* pointer to structure being
					   * initialised */, 
		       const reiser4_block_nr *root_block /* address of a
							    * root block on a
							    * disk */,
		       tree_level height /* height of a tree */, 
		       node_plugin *nplug, node_read_actor read_node )
{
	assert( "nikita-306", tree != NULL );
	assert( "nikita-307", root_block != NULL );
	assert( "nikita-308", height > 0 );
	assert( "nikita-309", nplug != NULL );
	assert( "nikita-1099", read_node != NULL );

	xmemset( tree, 0, sizeof *tree );
	tree -> root_block = *root_block;
	tree -> height = height;
	tree -> nplug = nplug;
	tree -> read_node = read_node;
	tree -> cbk_cache = reiser4_kmalloc( sizeof( cbk_cache ), GFP_KERNEL );
	if( tree -> cbk_cache == NULL )
		return -ENOMEM;
	cbk_cache_init( tree -> cbk_cache );

	return znodes_tree_init( tree );
}


/* get layout plugin by plugin id if master_sb != 0, call guess method for all
 * layout plugins available otherwise */
static layout_plugin * find_layout_plugin (struct super_block * s UNUSED_ARG,
					   struct reiser4_master_sb * master_sb,
					   struct buffer_head ** super_bh UNUSED_ARG)
{
	__u16 plugin_id;

	if (!master_sb) {
		/* FIXME-VS: call guess method for all available layout plugins */
		return ERR_PTR (-EINVAL);
	} else {
		plugin_id = d16tocpu (&master_sb->disk_plugin_id);
		/* only one plugin is available for now */
		assert ("vs-476", plugin_id == LAYOUT_40_ID);
	}

	return layout_plugin_by_id (plugin_id);
}


static int reiser4_fill_super (struct super_block * s, void * data UNUSED_ARG,
			       int silent UNUSED_ARG)
{
	struct buffer_head * super_bh;
	struct reiser4_master_sb * master_sb;
	layout_plugin * lplug;
	struct inode * inode;
	int result;


 read_super_block:
	/* look for reiser4 magic at hardcoded place */
	super_bh = sb_bread (s, (int)(REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh)
		return -EIO;
	
	master_sb = (struct reiser4_master_sb *)super_bh->b_data;
	/* check reiser4 magic string */
	if (!strncmp (master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4)) {
		/* reset block size if it is not a right one */
		if (d16tocpu (&master_sb->blocksize) != s->s_blocksize) {
			brelse (super_bh);
			if (!sb_set_blocksize (s, d16tocpu (&master_sb->blocksize)))
				return -EINVAL;
			goto read_super_block;
		}
	} else {
		/* no standard reiser4 super block found */
		brelse (super_bh);
		master_sb = 0;
		super_bh = 0;
	}

	lplug = find_layout_plugin (s, master_sb, &super_bh);
	if (IS_ERR (lplug)) {
		brelse (super_bh);
		return -EINVAL;
	}

	s->s_op = &reiser4_super_operations;

	/* this is common for every disk layout. It has a pointer where layout
	 * specific part of info can be attached to, though */
	s->u.generic_sbp = kmalloc (sizeof (reiser4_super_info_data),
				    GFP_KERNEL);
	if (!s->u.generic_sbp) {
		brelse (super_bh);
		return -ENOMEM;
	}
	memset (s->u.generic_sbp, 0, sizeof (reiser4_super_info_data));

	((reiser4_super_info_data *)(s->u.generic_sbp))->lplug = lplug;
 
	/* call disk format plugin method to do all the preparations like
	 * journal replay, super_info_data initialization, etc */
	result = lplug->get_ready (s, get_super_private (s), super_bh);
	if (result)
		return result;

	inode = reiser4_iget (s, lplug->root_dir_key ());
	/* allocate dentry for root inode, It works with inode == 0 */
	s->s_root = d_alloc_root (inode);
	if (!s->s_root) {
		return -EINVAL;
	}

	return 0;
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
