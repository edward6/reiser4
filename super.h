/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Super-block functions. */

#if !defined( __REISER4_SUPER_H__ )
#define __REISER4_SUPER_H__

#include "forward.h"
#include "debug.h"
#include "tree.h"
#include "trace.h"
#include "lnode.h"
#include "plugin/plugin.h"

#include "plugin/space/space_allocator.h"
#include "plugin/oid/oid.h"

#include "plugin/disk_format/test.h"
#include "plugin/disk_format/disk_format40.h"
#include "plugin/security/perm.h"

#include "emergency_flush.h"

#include <linux/spinlock.h>
#include <linux/types.h>	/* for __u??, etc.  */
#include <linux/fs.h>		/* for struct super_block, etc.  */
#include <linux/list.h>		/* for struct list_head */

typedef struct {
	unsigned relocate_threshold;
	unsigned relocate_distance;
	unsigned queue_size;
	unsigned written_threshold;
	unsigned scan_maxnodes;
} flush_params;

typedef enum {
	/* True if this file system doesn't support hard-links (multiple
	   names) for directories: this is default UNIX behavior.
	  
	   If hard-links on directoires are not allowed, file system is
	   Acyclic Directed Graph (modulo dot, and dotdot, of course).
	  
	   This is used by reiser4_link().
	*/
	REISER4_ADG = 0,
	/* set if all nodes in internal tree have the same node layout plugin.
	   If so, znode_guess_plugin() will return tree->node_plugin in stead
	   of guessing plugin by plugin id stored in the node.
	*/
	REISER4_ONE_NODE_PLUGIN = 1,
#if REISER4_BSD_PORT
	/* if set, bsd gid assignment is supported. */
	REISER4_BSD_GID = 2,
#endif
} reiser4_fs_flag;

/* reiser4-specific part of super block */
struct reiser4_super_info_data {
	/* guard spinlock which protects reiser4 super 
	   block fields (currently blocks_free, 
	   blocks_free_committed)
	*/
	spinlock_t guard;

	/* allocator used to allocate new object ids for objects in the file
	   system. Current default implementation of object id allocator is
	   just counter and
	   used by reiser 4.0 default oid manager
	*/
	oid_allocator_plugin *oid_plug;
	reiser4_oid_allocator oid_allocator;

	/* space manager plugin */
	space_allocator_plugin *space_plug;
	reiser4_space_allocator space_allocator;

	/* reiser4 internal tree */
	reiser4_tree tree;

	/* default user id used for light-weight files without their own
	   stat-data. */
	uid_t default_uid;

	/* default group id used for light-weight files without their own
	   stat-data. */
	gid_t default_gid;

	/* mkfs identifier generated at mkfs time. */
	__u32 mkfs_id;
	/* amount of blocks in a file system */
	__u64 block_count;

	/* amount of blocks used by file system data and meta-data. */
	__u64 blocks_used;

#if REISER4_DEBUG
	/* minimum used blocks value (includes super blocks, bitmap blocks and
	 * other fs reserved areas), depends on fs format and fs size. */
	__u64 min_blocks_used;
#endif
	/* amount of free blocks. This is "working" free blocks counter. It is
	   like "working" bitmap, please see block_alloc.c for description. */
	__u64 blocks_free;

	/* free block count for fs committed state. This is "commit" version
	   of free block counter. */
	__u64 blocks_free_committed;

	/* number of blocks reserved for further allocation, for all threads. */
	__u64 blocks_grabbed;

	/* number of fake allocated unformatted blocks in tree. */
	__u64 blocks_fake_allocated_unformatted;

	/* number of fake allocated formatted blocks in tree. */
	__u64 blocks_fake_allocated;

	/* number of blocks reserved for flush operations. */
	__u64     blocks_flush_reserved;
	/* current inode generation.
	  
	   FIXME-NIKITA not sure this is really needed now when we have 64-bit
	   inode numbers.
	  
	*/
	__u32 inode_generation;

	/* unique file-system identifier */
	/* does this conform to Andreas Dilger UUID stuff? */
	__u32 fsuid;

	/* per-fs tracing flags. Use reiser4_trace_flags enum to set
	   bits in it. */
	__u32 trace_flags;

	/* file where tracing goes (if enabled). */
	reiser4_trace_file trace_file;

	/* per-fs debugging flags. This is bitmask populated from 
	   reiser4_debug_flags enum. */
	__u32 debug_flags;

	/* super block flags */

	/* file-system wide flags. See reiser4_fs_flag enum */
	unsigned long fs_flags;

	/* Statistical counters. reiser4_stat is empty data-type unless
	   REISER4_STATS is set. */
	reiser4_stat stats;

	/* transaction manager */
	txn_mgr tmgr;

	/* fake inode used to bind formatted nodes */
	struct inode *fake;

	ln_hash_table lnode_htable;
	spinlock_t lnode_htable_guard;
#if REISER4_DEBUG
	/* amount of space allocated by kmalloc. For debugging. */
	int kmalloc_allocated;
	/* list of all jnodes */
	struct list_head all_jnodes;
#endif

	/* disk layout plugin */
	disk_format_plugin *df_plug;

	/* disk layout specific part of reiser4 super info data */
	union {
		format40_super_info format40;
		test_format_super_info test_format;
	} u;

	unsigned long optimal_io_size;

	struct {
		tail_plugin *t;
		item_plugin *sd;
		item_plugin *dir_item;
		perm_plugin *p;
		file_plugin *f;
		dir_plugin *d;
		hash_plugin *h;
	} plug;

	struct {
		unsigned atom_max_size;
		unsigned atom_max_age;
		unsigned low_memory;
	} txnmgr;

	flush_params flush;

	int           eflushed;
	ef_hash_table efhash_table;

	/* pointers to jnodes for journal header and footer */
	jnode *journal_header;
	jnode *journal_footer;

	/* head block number of last committed transaction */
	__u64 last_committed_tx;

	/* we remember last written location for using as a hint for
	   new block allocation */
	__u64 blocknr_hint_default;

	/* committed number of files (oid allocator state variable ) */
	__u64 nr_files_committed;
};

extern reiser4_super_info_data *get_super_private_nocheck(const struct
							  super_block *super);

extern struct super_operations reiser4_super_operations;

/* Return reiser4-specific part of super block */
static inline reiser4_super_info_data *
get_super_private(const struct super_block * super)
{
	assert("nikita-447", super != NULL);
	assert("nikita-2245", 
	       (super->s_op == NULL) || 
	       (super->s_op == &reiser4_super_operations));

	return (reiser4_super_info_data *) super->s_fs_info;
}

/* "Current" super-block: main super block used during current system
   call. Reference to this super block is stored in reiser4_context. */
static inline struct super_block *
reiser4_get_current_sb(void)
{
	return get_current_context()->super;
}

/* Reiser4-specific part of "current" super-block: main super block used
   during current system call. Reference to this super block is stored in
   reiser4_context. */
static inline reiser4_super_info_data *
get_current_super_private(void)
{
	return get_super_private(reiser4_get_current_sb());
}

extern __u64 reiser4_current_block_count(void);

extern const __u32 REISER4_SUPER_MAGIC;

#define spin_ordering_pred_reiser4_super_info_data(private) (1)
SPIN_LOCK_FUNCTIONS(reiser4_super_info_data, reiser4_super_info_data, guard);

static inline void reiser4_spin_lock_sb(const struct super_block * s)
{
	reiser4_super_info_data * private = get_super_private (s);
	spin_lock_reiser4_super_info_data(private);
}

static inline void reiser4_spin_unlock_sb(const struct super_block * s)
{
	reiser4_super_info_data * private = get_super_private (s);
	spin_unlock_reiser4_super_info_data(private);
}

extern __u64 reiser4_flush_reserved        ( const struct super_block*);
extern void  reiser4_set_flush_reserved    ( const struct super_block*, __u64 nr );
extern int reiser4_is_set(const struct super_block *super, reiser4_fs_flag f);
extern long statfs_type(const struct super_block *super);
extern int reiser4_blksize(const struct super_block *super);
extern __u64 reiser4_block_count(const struct super_block *super);
extern void reiser4_set_block_count(const struct super_block *super, __u64 nr);
extern __u64 reiser4_data_blocks(const struct super_block *super);
extern void reiser4_set_data_blocks(const struct super_block *super, __u64 nr);
extern __u64 reiser4_free_blocks(const struct super_block *super);
extern void reiser4_set_free_blocks(const struct super_block *super, __u64 nr);
extern void reiser4_inc_free_blocks(const struct super_block *super);
extern __u32 reiser4_mkfs_id(const struct super_block *super);
extern void reiser4_set_mkfs_id(const struct super_block *super, __u32 id);

extern __u64 reiser4_free_committed_blocks(const struct super_block *super);
extern void reiser4_set_free_committed_blocks(const struct super_block *super, __u64 nr);

extern __u64 reiser4_grabbed_blocks(const struct super_block *);
extern void reiser4_set_grabbed_blocks(const struct super_block *, __u64 nr);
extern __u64 reiser4_fake_allocated(const struct super_block *);
extern void reiser4_set_fake_allocated(const struct super_block *, __u64 nr);
extern __u64 reiser4_fake_allocated_unformatted(const struct super_block *);
extern void reiser4_set_fake_allocated_unformatted(const struct super_block *, __u64 nr);

extern long reiser4_reserved_blocks(const struct super_block *super, uid_t uid, gid_t gid);

extern reiser4_space_allocator *get_space_allocator(const struct super_block
						    *super);
extern reiser4_oid_allocator *get_oid_allocator(const struct super_block
						*super);
extern struct inode *get_super_fake(const struct super_block *super);
extern reiser4_tree *get_tree(const struct super_block *super);
extern __u32 new_inode_generation(const struct super_block *super);
extern int is_reiser4_super(const struct super_block *super);

file_plugin *default_file_plugin(const struct super_block *super);
dir_plugin *default_dir_plugin(const struct super_block *super);
hash_plugin *default_hash_plugin(const struct super_block *super);
perm_plugin *default_perm_plugin(const struct super_block *super);
tail_plugin *default_tail_plugin(const struct super_block *super);
item_plugin *default_sd_plugin(const struct super_block *super);
item_plugin *default_dir_item_plugin(const struct super_block *super);
#if REISER4_DEBUG_OUTPUT
void print_fs_info(const char *prefix, const struct super_block *);
#else
#define print_fs_info(p,s) noop
#endif

/* __REISER4_SUPER_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
