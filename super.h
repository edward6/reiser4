/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Super-block functions. See super.c for details. */

#if !defined(__REISER4_SUPER_H__)
#define __REISER4_SUPER_H__

#include <linux/exportfs.h>

#include "tree.h"
#include "entd.h"
#include "wander.h"
#include "fsdata.h"
#include "plugin/object.h"
#include "plugin/space/space_allocator.h"

/*
 * Flush algorithms parameters.
 */
struct flush_params {
	unsigned relocate_threshold;
	unsigned relocate_distance;
	unsigned written_threshold;
	unsigned scan_maxnodes;
};

typedef enum {
	/*
	 * True if this file system doesn't support hard-links (multiple names)
	 * for directories: this is default UNIX behavior.
	 *
	 * If hard-links on directoires are not allowed, file system is Acyclic
	 * Directed Graph (modulo dot, and dotdot, of course).
	 *
	 * This is used by reiser4_link().
	 */
	REISER4_ADG = 0,
	/* if set, bsd gid assignment is supported. */
	REISER4_BSD_GID = 2,
	/* [mac]_time are 32 bit in inode */
	REISER4_32_BIT_TIMES = 3,
	/* load all bitmap blocks at mount time */
	REISER4_DONT_LOAD_BITMAP = 5,
	/* enforce atomicity during write(2) */
	REISER4_ATOMIC_WRITE = 6,
	/* enable issuing of discard requests */
	REISER4_DISCARD = 8,
	/* disable hole punching at flush time */
	REISER4_DONT_PUNCH_HOLES = 9
} reiser4_fs_flag;

typedef enum {
	/* set if all nodes in internal tree have the same
	 * node layout plugin. See znode_guess_plugin() */
	SUBVOL_ONE_NODE_PLUGIN = 0,
	/* set if subvolume lives on a solid state drive */
	SUBVOL_IS_NONROT_DEVICE = 1,
	/* set if subvol is registered */
	SUBVOL_REGISTERED = 2,
	/* set if subvol is activated */
	SUBVOL_ACTIVATED = 3,
	/* set if subvol is a replica */
	SUBVOL_IS_MIRROR = 4
} reiser4_subvol_flag;

/*
 * VFS related operation vectors.
 */
struct object_ops {
	struct super_operations super;
	struct dentry_operations dentry;
	struct export_operations export;
};

/* reiser4-specific part of super block

   Locking

   Fields immutable after mount:

    ->oid*
    ->space*
    ->default_[ug]id
    ->mkfs_id
    ->trace_flags
    ->debug_flags
    ->fs_flags
    ->df_plug
    ->optimal_io_size
    ->plug
    ->flush
    ->u (bad name)
    ->txnmgr
    ->ra_params
    ->journal_header
    ->journal_footer

   Fields protected by per-super block spin lock

    ->block_count
    ->blocks_used
    ->blocks_free
    ->blocks_free_committed
    ->blocks_grabbed
    ->blocks_fake_allocated_unformatted
    ->blocks_fake_allocated
    ->blocks_flush_reserved
    ->eflushed
    ->blocknr_hint_default

   After journal replaying during mount,

    ->last_committed_tx

   is protected by ->tmgr.commit_mutex

   Invariants involving this data-type:

      [sb-block-counts]
      [sb-grabbed]
      [sb-fake-allocated]
*/

/**
 * Per-atom and per-subvolume commit info.
 * This structure is accessed at atom commit time under commit_mutex.
 * See also definition of per-logical-volume struct commit_handle.
 */
struct commit_handle_subvol
{
	struct list_head overwrite_set;
	__u32 overwrite_set_size;
	struct list_head tx_list; /* jnodes for wander record blocks */
	__u32 tx_size; /* number of wander records for this subvolume */
	struct list_head wander_map; /* The atom's wandered_block mapping.
				      * Earlier it was ->wandered_map of struct
				      * txn_atom. Edward moved it here, as
				      * wandered map is always constructed at
				      * commit time under commit_mutex, so
				      * actually there is nothing to do for this
				      * map in the struct txn_atom.
				      */
	reiser4_block_nr nr_bitmap; /* counter of modified bitmaps */
	u64 free_blocks; /*'committed' sb counters are saved here until
			   atom is completely flushed */
};

/*
 * In-memory subvolume header.
 * It is always associated with a physical or logical (LVM) block device.
 */
struct reiser4_subvol {
	struct list_head list; /* all registered subvolumes are linked */
	u8 uuid[16]; /* uuid of physical, or logical (LMV) drive */
	char *name;
	fmode_t mode;
	struct block_device *bdev;
	u64 id; /* index in the array of subvolumes (internal id) */
	u32 dev_cap; /* capacity of subvolume */
	u64 fiber_len; /* number of segments in the fiber */
	u64 room_for_data; /* number of data block (for meta-data subvolumes) */
	void *fiber; /* array of segments (cpu fiber) */
	jnode **fiber_nodes; /* array of jnodes which represent on-disk fiber */

	unsigned long flags; /* subvolume-wide flags, see subvol_flags enum */
	disk_format_plugin *df_plug; /* disk format of this subvolume */
	union {
		format40_super_info format40;
	} u; /* disk layout specific part */
	reiser4_space_allocator space_allocator; /* space manager plugin */
	reiser4_txmod_id txmod; /* transaction model for this subvolume */
	struct flush_params flush; /* parameters for the flush algorithm */
	reiser4_tree tree; /* internal tree */
	__u32 mkfs_id; /* mkfs identifier generated at mkfs time. */

	__u64 block_count; /* amount of blocks in a subvolume */
	__u64 blocks_free; /* amount of free blocks. This is a "working" version
			      of free blocks counter. It is like "working"
			      bitmap, see block_alloc.c for description */
	__u64 blocks_reserved; /* inviolable reserve */
	__u64 blocks_used; /* amount of blocks used by file system data and
			      meta-data. */
	__u64 blocks_grabbed; /* number of blocks reserved for further
				 allocation, for all threads */
	__u64 blocks_fake_allocated_unformatted;/* number of fake allocated
						   unformatted blocks in tree */
	__u64 blocks_fake_allocated; /* number of fake allocated formatted
					blocks in tree */
	__u64 blocks_flush_reserved; /* number of blocks reserved for flush
					operations */
	__u64 blocks_clustered; /* number of blocks reserved for cluster
				   operations */

	int version; /* On-disk format version. May be upgraded at mount time */
	jnode *journal_header; /* jnode of hournal header */
	jnode *journal_footer; /* jnode of journal footer */
	journal_location jloc;
	__u64 last_committed_tx; /* head block number of last committed
				    transaction */
	__u64 blocknr_hint_default; /* we remember last written location
				       for using as a hint for new block
				       allocation */
	__u64 diskmap; /* diskmap's blocknumber */

	struct repacker *repacker;
	struct page *status_page;
	struct bio *status_bio;
#if REISER4_DEBUG
	__u64 min_blocks_used; /* minimum used blocks value (includes super
				  blocks, bitmap blocks and other fs reserved
				  areas), depends on fs format and fs size. */
#endif
	/*
	 * Per-subvolume fields of commit handle.
	 * Access to them requires to acquire the commit_mutex.
	 */
	__u64 blocks_freed; /* number of blocks freed by the actor
			       apply_dset_to_commit_bmap */
	__u64 blocks_free_committed; /* "commit" version of free
					block counter */
	struct commit_handle_subvol ch;
	struct super_block *super; /* associated super-block */
};

static inline int subvol_is_set(const reiser4_subvol *subv,
				reiser4_subvol_flag f)
{
	return test_bit((int)f, &subv->flags);
}

/*
 * In-memory superblock
 */
struct reiser4_super_info_data {
	spinlock_t guard; /* protects fields blocks_free,
			     blocks_free_committed, etc  */
	oid_t next_to_use;/* next oid that will be returned by oid_allocate() */
	oid_t oids_in_use; /* total number of used oids */
	__u32 default_uid; /* default user id used for light-weight files
			      without their own stat-data */
	__u32 default_gid; /* default group id used for light-weight files
			      without their own stat-data */
	unsigned long fs_flags; /* file-system wide flags. See reiser4_fs_flag
				   enum */
	txn_mgr tmgr; 	/* transaction manager */
	entd_context entd; /* ent thread */
	struct inode *fake; /* fake inode used to bind formatted nodes */
	/* inode used to bind bitmaps (and journal heads) */
	struct inode *bitmap; /* fake inode used to bind bitmaps (and journal
				 heads) */
	struct inode *cc; /* fake inode used to bind copied on capture nodes */
	unsigned long optimal_io_size; /* value we return in st_blksize on
					  stat(2) */
	__u64 nr_files_committed; /* committed number of files (oid allocator
				     state variable ) */
	__u64 vol_block_count; /* amount of blocks in a (logical) volume */
	struct formatted_ra_params ra_params;
	int onerror; /* What to do in case of IO error. Specified by a mount
			option */
	struct object_ops ops; /* operations for objects on this volume */
	struct d_cursor_info d_info; /* structure to maintain d_cursors.
					See plugin/file_ops_readdir.c for more
					details */
	struct crypto_shash *csum_tfm;
	struct mutex delete_mutex;/* a mutex for serializing cut tree operation
				     if out-of-free-space: the only one cut_tree
				     thread is allowed to grab space from
				     reserved area (it is 5% of disk space) */
	struct task_struct *delete_mutex_owner; /* task owning ->delete_mutex */
#ifdef CONFIG_REISER4_BADBLOCKS
	unsigned long altsuper; /* Alternative master superblock offset
				   (in bytes). Specified by a mount option */
#endif
	struct dentry *debugfs_root;
#if REISER4_DEBUG
	/*
	 * when debugging is on, all jnodes (including znodes, bitmaps, etc.)
	 * are kept on a list anchored at sbinfo->all_jnodes. This list is
	 * protected by sbinfo->all_guard spin lock. This lock should be taken
	 * with _irq modifier, because it is also modified from interrupt
	 * contexts (by RCU).
	 */
	spinlock_t all_guard;
	struct list_head all_jnodes; /* list of all jnodes */
#endif
	struct reiser4_volume *vol; /* accociated volume header */
	reiser4_context *ctx;
};

/*
 * In-memory header of compound (logical) volume.
 */
struct reiser4_volume {
	struct list_head list;
	u8 uuid[16]; /* volume id */
	int num_sgs_bits; /* logarithm of number of hash space segments */
	int stripe_size_bits; /* logarithm of stripe size */
	int num_meta_subvols;
	int num_mixed_subvols;
	distribution_plugin *dist_plug;
	volume_plugin *vol_plug;
	reiser4_aib *aib; /* storage array descriptor */
	u64 num_subvols; /* number of subvolumes in the logical volume */
	u64 num_mirrors; /* number of replicas (excluding the original) */
	struct list_head subvols_list;  /* list of registered subvolumes */
	struct reiser4_subvol **subvols; /* activated subvolumes */
	struct reiser4_super_info_data *info; /* accociated super-block */
};

extern reiser4_super_info_data *get_super_private_nocheck(const struct
							  super_block * super);

/* Return reiser4-specific part of super block */
static inline reiser4_super_info_data *get_super_private(const struct
							 super_block * super)
{
	assert("nikita-447", super != NULL);

	return (reiser4_super_info_data *) super->s_fs_info;
}

static inline reiser4_volume *get_super_volume(struct super_block *super)
{
	return get_super_private(super)->vol;
}

static inline reiser4_subvol *sbinfo_subvol(reiser4_super_info_data *info,
					    __u32 subv_id)
{
	assert("edward-xxx", info != NULL);
	assert("edward-xxx", info->vol != NULL);
	assert("edward-xxx", info->vol->subvols[subv_id] != NULL);
	assert("edward-xxx", info->vol->subvols[subv_id]->id == subv_id);

	return info->vol->subvols[subv_id];
}

static inline __u32 sbinfo_num_mirrors(reiser4_super_info_data *info)
{
	assert("edward-xxx", info != NULL);
	assert("edward-xxx", info->vol != NULL);

	return info->vol->num_mirrors;
}

static inline __u32 sbinfo_num_notmirr(reiser4_super_info_data *info)
{
	assert("edward-xxx", info != NULL);
	assert("edward-xxx", info->vol != NULL);

	return info->vol->num_subvols - info->vol->num_mirrors;
}

static inline reiser4_subvol *super_subvol(const struct super_block *super,
					   __u32 subv_id)
{
	return sbinfo_subvol(get_super_private(super), subv_id);
}

/**
 * get original subvolume
 * FIXME-EDWARD: This should go to format specifications
 */
static inline reiser4_subvol *super_origin(const struct super_block *super)
{
	return sbinfo_subvol(get_super_private(super),
			     sbinfo_num_mirrors(get_super_private(super)));
}

/* get ent context for the @super */
static inline entd_context *get_entd_context(struct super_block *super)
{
	return &get_super_private(super)->entd;
}

/**
 * Get the super block used during current system call.
 * Reference to this super block is stored in reiser4_context
 */
static inline struct super_block *reiser4_get_current_sb(void)
{
	return get_current_context()->super;
}

/**
 * Reiser4-specific part of "current" super-block: main super block used
 * during current system call. Reference to this super block is stored in
 * reiser4_context
 */
static inline reiser4_super_info_data *get_current_super_private(void)
{
	return get_super_private(reiser4_get_current_sb());
}

static inline struct formatted_ra_params *get_current_super_ra_params(void)
{
	return &(get_current_super_private()->ra_params);
}

static inline struct distribution_plugin *private_dist_plug(reiser4_super_info_data *info)
{
	return info->vol->dist_plug;
}

static inline struct distribution_plugin *super_dist_plug(struct super_block *s)
{
	return get_super_private(s)->vol->dist_plug;
}

static inline struct distribution_plugin *current_dist_plug(void)
{
	return get_current_super_private()->vol->dist_plug;
}

static inline int current_stripe_size_bits(void)
{
	return current_volume->stripe_size_bits;
}

static inline __u64 current_stripe_size(void)
{
	return 1 << current_stripe_size_bits();
}

static inline struct reiser4_subvol *current_subvol(__u32 subv_id)
{
	return sbinfo_subvol(get_current_super_private(), subv_id);
}

static inline __u32 current_num_mirrors(void)
{
	return get_current_context()->ctx_num_mirrors;
}

static inline int have_mirrors(void)
{
	return current_num_mirrors();
}

static inline __u32 current_num_notmirr(void)
{
	return get_current_context()->ctx_num_notmirr;
}

static inline int current_is_last_commit(void)
{
	return get_current_context()->exit_mount_session;
}

#define for_each_notmirr(_subv_id)					\
	for (_subv_id = current_num_mirrors();				\
	     _subv_id < current_num_mirrors() + current_num_notmirr();	\
	     _subv_id ++)

#define for_each_mirror(_subv_id)					\
	for (_subv_id = 0;						\
	     _subv_id < current_num_mirrors(); _subv_id ++)

#define for_each_subvol(_subv_id)					\
	for (_subv_id = 0;						\
	     _subv_id < current_num_mirrors() + current_num_notmirr();	\
	     _subv_id ++)

static inline int is_mirror(struct reiser4_subvol *subv)
{
	assert("edward-xxx", subv != NULL);
	assert("edward-xxx", subv->super != NULL);
	assert("edward-xxx", get_super_volume(subv->super) != NULL);
	assert("edward-xxx",
	       ergo(subvol_is_set(subv, SUBVOL_ACTIVATED) &&
		    subvol_is_set(subv, SUBVOL_IS_MIRROR),
		    subv->id < get_super_volume(subv->super)->num_mirrors));

	return subvol_is_set(subv, SUBVOL_IS_MIRROR);
}

static inline int is_mirror_id(u32 subv_id)
{
	assert("edward-xxx",
	       subvol_is_set(current_subvol(subv_id), SUBVOL_ACTIVATED));

	return subv_id < current_num_mirrors();
}

/*
 * true, if file system on @super is read-only
 */
static inline int rofs_super(struct super_block *super)
{
	return super->s_flags & MS_RDONLY;
}

/*
 * true, if file system where @inode lives on, is read-only
 */
static inline int rofs_inode(struct inode *inode)
{
	return rofs_super(inode->i_sb);
}

/*
 * true, if file system where @node lives on, is read-only
 */
static inline int rofs_jnode(jnode *node)
{
	return rofs_super(node->subvol->super);
}

extern void build_object_ops(struct super_block *super, struct object_ops *ops);

#define REISER4_SUPER_MAGIC 0x52345362	/* (*(__u32 *)"R4Sb"); */

static inline void spin_lock_reiser4_super(reiser4_super_info_data *sbinfo)
{
	spin_lock(&(sbinfo->guard));
}

static inline void spin_unlock_reiser4_super(reiser4_super_info_data *sbinfo)
{
	assert_spin_locked(&(sbinfo->guard));
	spin_unlock(&(sbinfo->guard));
}

/* FIXME-EDWARD: Below is implementation of trivial volume plugin */

static inline reiser4_subvol *sbinfo_subvol_for_system(reiser4_super_info_data *sbinfo)
{
	return sbinfo_subvol(sbinfo, sbinfo->vol->num_mirrors);
}

/* subvolume for system records: status records, blackbox items, etc */
static inline reiser4_subvol *subvol_for_system(void)
{
	reiser4_subvol *subv;
	subv = current_volume->subvols[current_num_mirrors()];
	assert("edward-xxx", !is_mirror(subv));

	return subv;
}

/**
 * @offset: offset of the data stripe in the file.
 */
static inline reiser4_subvol *subvol_for_data(const struct inode *inode,
					      loff_t offset)
{
	return current_volume->subvols[current_num_mirrors()];
}

static inline reiser4_subvol *__subvol_for_meta(oid_t oid)
{
	return current_volume->subvols[current_num_mirrors()];
}

static inline reiser4_subvol *subvol_for_meta(const struct inode *inode)
{
	return current_volume->subvols[current_num_mirrors()];
}

static inline reiser4_subvol *subvol_by_coord(const coord_t *coord)
{
	return ZJNODE(coord->node)->subvol;
}

static inline reiser4_tree *tree_by_coord(const coord_t *coord)
{
	return &subvol_by_coord(coord)->tree;
}

static inline void __init_ch_sub(struct commit_handle_subvol *ch_sub)
{
	memset(ch_sub, 0, sizeof(*ch_sub));
	INIT_LIST_HEAD(&ch_sub->overwrite_set);
	INIT_LIST_HEAD(&ch_sub->tx_list);
	INIT_LIST_HEAD(&ch_sub->wander_map);
}

extern __u64 reiser4_flush_reserved(const reiser4_subvol *);
extern int reiser4_is_set(const struct super_block *super, reiser4_fs_flag f);
extern long reiser4_statfs_type(const struct super_block *super);
extern __u64 reiser4_subvol_block_count(const reiser4_subvol *);
extern __u64 reiser4_volume_block_count(const struct super_block *);
extern void reiser4_subvol_set_block_count(reiser4_subvol *subv, __u64 nr);
extern __u64 reiser4_subvol_blocks_reserved(const reiser4_subvol *subv);
extern __u64 reiser4_volume_blocks_reserved(const struct super_block *super);
extern __u64 reiser4_subvol_used_blocks(const reiser4_subvol *);
extern void reiser4_subvol_set_used_blocks(reiser4_subvol *, __u64 nr);
extern __u64 reiser4_subvol_free_blocks(const reiser4_subvol *);
extern void reiser4_subvol_set_free_blocks(reiser4_subvol *, __u64 nr);
extern __u64 reiser4_subvol_fiber_len(reiser4_subvol *);
extern void reiser4_subvol_set_fiber_len(reiser4_subvol *, __u64 len);

extern __u64 reiser4_volume_free_blocks(const struct super_block *super);
extern __u32 reiser4_mkfs_id(const struct super_block *super, __u32 subv_id);
extern __u64 reiser4_subvol_free_committed_blocks(const reiser4_subvol *);
extern __u64 reiser4_grabbed_blocks(const reiser4_subvol *);
extern __u64 reiser4_fake_allocated(const reiser4_subvol *);
extern __u64 reiser4_fake_allocated_unformatted(const reiser4_subvol *);
extern __u64 reiser4_clustered_blocks(const reiser4_subvol *);

extern long reiser4_subvol_reserved4user(const reiser4_subvol *,
					 uid_t uid, gid_t gid);
extern long reiser4_volume_reserved4user(const struct super_block *,
					 uid_t uid, gid_t gid);
extern reiser4_space_allocator * reiser4_get_space_allocator(reiser4_subvol *);
extern reiser4_oid_allocator *
reiser4_get_oid_allocator(const struct super_block *super);
extern struct inode *reiser4_get_super_fake(const struct super_block *super);
extern struct inode *reiser4_get_cc_fake(const struct super_block *super);
extern struct inode *reiser4_get_bitmap_fake(const struct super_block *super);
extern int is_reiser4_super(const struct super_block *super);

extern int reiser4_blocknr_is_sane(const reiser4_subvol *subv,
				   const reiser4_block_nr * blk);
extern int reiser4_blocknr_is_sane_for(const reiser4_subvol *subv,
				       const reiser4_block_nr *blk);
extern int reiser4_done_super(struct super_block *s);
extern int reiser4_scan_device(const char *path, fmode_t flags, void *holder);

/* step of fill super */
extern int reiser4_init_fs_info(struct super_block *);
extern void reiser4_done_fs_info(struct super_block *);
extern int reiser4_init_super_data(struct super_block *, char *opt_string);
extern int reiser4_activate_volume(struct super_block *, u8 *vol_uuid);
extern void reiser4_deactivate_volume(struct super_block *);
extern void reiser4_unregister_volumes(void);
extern struct reiser4_volume *reiser4_search_volume(u8 *vol_uuid);
extern int reiser4_read_master(struct super_block *, int silent, u8 *vol_uuid);
extern int reiser4_init_root_inode(struct super_block *);
extern reiser4_plugin *get_default_plugin(pset_member memb);

#define INVALID_OID ((oid_t)0)
/* Maximal possible object id. */
#define  ABSOLUTE_MAX_OID ((oid_t)~0)

#define OIDS_RESERVED  (1 << 16)
int oid_init_allocator(struct super_block *, oid_t nr_files, oid_t next);
oid_t oid_allocate(struct super_block *);
int oid_release(struct super_block *, oid_t);
oid_t oid_next(const struct super_block *);
void oid_count_allocated(void);
void oid_count_released(void);
long oids_used(const struct super_block *);

#if REISER4_DEBUG
void print_fs_info(const char *prefix, const struct super_block *);
#endif

extern void destroy_reiser4_cache(struct kmem_cache **);

extern struct super_operations reiser4_super_operations;
extern struct export_operations reiser4_export_operations;
extern struct dentry_operations reiser4_dentry_operations;

/* __REISER4_SUPER_H__ */
#endif

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
