/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Super-block functions.
 */

#if !defined( __REISER4_SUPER_H__ )
#define __REISER4_SUPER_H__

/** reiser4-specific part of super block */
typedef struct reiser4_super_info_data {
	/**
	 * allocator used to allocate new object ids for objects in the file
	 * system. Current default implementation of object id allocator is
	 * just counter and
	 */
	reiser4_oid_allocator allocator;

	/**
	 * reiser4 internal tree
	 */
	reiser4_tree       tree;

	/**
	 * default user id used for light-weight files without their own
	 * stat-data.
	 */
	uid_t              default_uid;

	/**
	 * default group id used for light-weight files without their own
	 * stat-data.
	 */
	gid_t              default_gid;

	/**
	 * amount of blocks used by file system data and meta-data.
	 */
	__u32    blocks_used;

	/**
	 * amount of free blocks.
	 */
	__u32    blocks_free;

	/**
	 * current inode generation.
	 *
	 * FIXME-NIKITA not sure this is really needed now when we have 64-bit
	 * inode numbers.
	 *
	 */
	__u32    inode_generation;

	/** unique file-system identifier */
	/* does this conform to Andreas Dilger UUID stuff? */
	__u32    fsuid;

	/**
	 * per-fs tracing flags. Use reiser4_trace_flags enum to set
	 * bits in it.
	 */
	__u32    trace_flags;

	/* super block flags */

	/**
	 * see reiser4_adg() for description.
	 */
	__u32    adg                :1;

	/**
	 * set if all nodes in internal tree have the same node layout plugin.
	 * If so, znode_guess_plugin() will return tree->node_plugin in stead
	 * of guessing plugin by plugin id stored in the node.
	 */
	__u32    one_node_plugin    :1;

	/**
	 * Statistical counters. reiser4_stat is empty data-type unless
	 * REISER4_STATS is set.
	 */
	reiser4_stat     stats;

	/** transaction manager */
	txn_mgr              tmgr;

#if REISER4_DEBUG
	/**
	 * amount of space allocated by kmalloc. For debugging.
	 */
	int                  kmalloc_allocated;
#endif

} reiser4_super_info_data;

extern reiser4_super_info_data *reiser4_get_super_private_nocheck( const struct super_block *super );

extern reiser4_super_info_data *reiser4_get_super_private( const struct super_block *super );

extern reiser4_super_info_data *reiser4_get_current_super_data( void );

extern const __u32 REISER4_SUPER_MAGIC;

extern long reiser4_statfs_type( const struct super_block *super );
extern int  reiser4_blksize( const struct super_block *super );
extern long reiser4_data_blocks( const struct super_block *super );
extern long reiser4_free_blocks( const struct super_block *super );
extern long reiser4_reserved_blocks( const struct super_block *super, 
			      uid_t uid, gid_t gid );
extern reiser4_oid_allocator *reiser4_get_oid_allocator( const struct super_block *super );
extern reiser4_tree *reiser4_get_tree( const struct super_block *super );
extern __u32 reiser4_new_inode_generation( const struct super_block *super );
extern int  reiser4_adg( const struct super_block *super );
extern int  is_reiser4_super( const struct super_block *super );

extern struct super_block *reiser4_get_current_sb( void );

/* __REISER4_SUPER_H__ */
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
