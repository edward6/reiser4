/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Super-block functions.
 */

#if !defined( __REISER4_SUPER_H__ )
#define __REISER4_SUPER_H__

/** reiser4-specific part of inode */
typedef struct reiser4_super_info_data {
	/**
	 * allocator used to allocate new object ids for objects in the file
	 * system. Current default implementation of object id allocator 
	 */
	reiser4_oid_allocator allocator;
	/**
	 *
	 */
	reiser4_tree       tree;
	/**
	 *
	 */
	uid_t              default_uid;
	/**
	 *
	 */
	gid_t              default_gid;

	/**
	 *
	 */
	__u32    blocks_used;
	/**
	 *
	 */
	__u32    blocks_free;
	/**
	 *
	 */
	__u32    inode_generation;
	/** unique file-system identifier */
	__u32    fsuid;

	/**
	 * per-fs tracing flags. Use reiser4_trace_flags enum to set
	 * bits in it.
	 */
	__u32    trace_flags;

	/* super block flags */

	/**
	 *
	 */
	__u32    adg                :1;
	/**
	 *
	 */
	__u32    one_node_plugin    :1;

	/**
	 *
	 */
	reiser4_stat     stats;

	/** transaction manager */
	txn_mgr              tmgr;

	/**
	 *
	 */
	int                  allocated;

} reiser4_super_info_data;

extern reiser4_super_info_data *reiser4_get_super_data_nocheck( const struct super_block *super );

extern reiser4_super_info_data *reiser4_get_super_data( const struct super_block *super );

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
