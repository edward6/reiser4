/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Inode functions. */

#if !defined( __REISER4_INODE_H__ )
#define __REISER4_INODE_H__

#include "forward.h"
#include "debug.h"
#include "spin_macros.h"
#include "key.h"
#include "kcond.h"
#include "seal.h"
#include "scint.h"
#include "plugin/plugin.h"
#include "plugin/cryptcompress.h"
#include "plugin/plugin_set.h"
#include "plugin/security/perm.h"
#include "plugin/pseudo/pseudo.h"
#include "vfs_ops.h"
#include "jnode.h"

#include <linux/types.h>	/* for __u?? , ino_t */
#include <linux/fs.h>		/* for struct super_block, struct rw_semaphore, etc  */
#include <linux/spinlock.h>
#include <asm/types.h>

/* reiser4-specific inode flags */
/* reiser4-specific inode flags. They are "transient" and are not
   supposed to be stored on a disk. Used to trace "state" of
   inode. Bitmasks for this field are defined in
   reiser4_file_plugin_flags enum. 

   Flags are stored in inode->i_mapping.assoc_mapping field */
typedef enum {
	/* this is light-weight inode, inheriting some state from its
	   parent  */
	REISER4_LIGHT_WEIGHT = 0,
	/* stat data wasn't yet created */
	REISER4_NO_SD = 1,
	/* internal immutable flag. Currently is only used
	    to avoid race condition during file creation.
	    See comment in create_object(). */
	REISER4_IMMUTABLE = 2,
	/* inode was read from storage */
	REISER4_LOADED = 3,
	/* this bit is set for symlinks. inode->u.generic_ip points to target
	   name of symlink */
	REISER4_GENERIC_VP_USED = 4,
/*	REISER4_EXCLUSIVE_USE = 5,*/
	REISER4_SDLEN_KNOWN   = 6,
	/* reiser4_inode->crypt points to the crypto stat */
	REISER4_CRYPTO_STAT_LOADED = 7,
	/* reiser4_inode->cluster_shift makes sense */
	REISER4_CLUSTER_KNOWN = 8,
	/* cryptcompress_inode_data points to the secret key */
	REISER4_SECRET_KEY_INSTALLED = 9,
	REISER4_DEC_UNUSED = 10
} reiser4_file_plugin_flags;

#if BITS_PER_LONG == 64
#define REISER4_INO_IS_OID (1)
typedef struct {;
} oid_hi_t;
#define set_inode_oid(inode, oid) do { inode->i_ino = oid; } while(0)
#define get_inode_oid(inode) (inode->i_ino)
#else
#define REISER4_INO_IS_OID (0)
typedef __u32 oid_hi_t;
#define set_inode_oid(inode, oid) do { \
	assert("nikita-2519", inode != NULL); \
	inode->i_ino = (ino_t)(oid); \
	reiser4_inode_data(inode)->oid_hi = (oid) >> OID_HI_SHIFT; \
	assert("nikita-2521", get_inode_oid(inode) == (oid)); \
	} while (0)
#define get_inode_oid(inode) (((__u64)reiser4_inode_data(inode)->oid_hi << OID_HI_SHIFT) | inode->i_ino)
#endif

#define OID_HI_SHIFT (sizeof(ino_t) * 8)


/* state associated with each inode.
   reiser4 inode.
  
   NOTE-NIKITA In 2.5 kernels it is not necessary that all file-system inodes
   be of the same size. File-system allocates inodes by itself through
   s_op->allocate_inode() method. So, it is possible to adjust size of inode
   at the time of its creation.


   Invariants involving parts of this data-type:

      [inode->eflushed]

*/

typedef struct reiser4_inode reiser4_inode;
/* return pointer to reiser4-specific part of inode */
static inline reiser4_inode *
reiser4_inode_data(const struct inode * inode /* inode queried */);

#include "plugin/file/file.h"

struct reiser4_inode {
	/* */ reiser4_spin_data guard;
	/* object plugins */
	/*   0 */ plugin_set *pset;
	/* high 32 bits of object id */
	/*   4 */ oid_hi_t oid_hi;
	/* seal for stat-data */
	/*   8 */ seal_t sd_seal;
	/* locality id for this file */
	/*  24 */ oid_t locality_id;
	/* coord of stat-data in sealed node */
	/*  28 */ coord_t sd_coord;
	/* truncate, tail2extent and extent2tail use down_write, read, write,
	 * readpage - down_read */
	/* 68 */ /*rw_latch_t latch;*/
	/* 88 */ scint_t extmask;
	/* 92 */ int eflushed;
	/* bitmask of non-default plugins for this inode */
	/* 96 */ __u16 plugin_mask;
	/* 98 */ inter_syscall_rap ra;
	/* 98 */ __u16 padding;
	/* cluster parameter for crypto and compression */
	/* 100 */__u8 cluster_shift;
	/* secret key parameter for crypto */ 
	/* 101 */crypto_stat_t *crypt;
	/* 105 */
	struct list_head  moved_pages;
	readdir_list_head readdir_list;
	unsigned long flags;
	union {
		unix_file_info_t unix_file_info;
		cryptcompress_info_t cryptcompress_info;
		pseudo_info_t pseudo_info;
	} file_plugin_data;

	struct list_head eflushed_jnodes;

	jnode inode_jnode; /* this is to capture inode */
};

typedef struct reiser4_inode_object {
	/* private part */
	reiser4_inode p;
	/* generic fields not specific to reiser4, but used by VFS */
	struct inode vfs_inode;
} reiser4_inode_object;

static inline reiser4_inode *
reiser4_inode_data(const struct inode * inode /* inode queried */)
{
	assert("nikita-254", inode != NULL);
	return &container_of(inode, reiser4_inode_object, vfs_inode)->p;
}

static inline struct inode *
inode_by_reiser4_inode(const reiser4_inode *r4_inode /* inode queried */)
{
       return &container_of(r4_inode, reiser4_inode_object, p)->vfs_inode;
}

/* ordering predicate for inode spin lock: only jnode lock can be held */
#define spin_ordering_pred_inode_object(inode)			\
	( lock_counters() -> rw_locked_dk == 0 ) &&		\
	( lock_counters() -> rw_locked_tree == 0 ) &&		\
	( lock_counters() -> spin_locked_txnh == 0 ) &&		\
	( lock_counters() -> spin_locked_zlock == 0 ) &&	\
	( lock_counters() -> spin_locked_jnode == 0 ) &&	\
	( lock_counters() -> spin_locked_atom == 0 ) &&		\
	( lock_counters() -> spin_locked_ktxnmgrd == 0 ) &&	\
	( lock_counters() -> spin_locked_txnmgr == 0 )

SPIN_LOCK_FUNCTIONS(inode_object, reiser4_inode, guard);

extern ino_t oid_to_ino(oid_t oid) __attribute__ ((const));
extern ino_t oid_to_uino(oid_t oid) __attribute__ ((const));

extern reiser4_tree *tree_by_inode(const struct inode *inode);

#if REISER4_DEBUG
extern void inode_invariant(const struct inode *inode);
#else
#define inode_invariant(inode) noop
#endif

#define spin_lock_inode(inode)			\
({						\
	LOCK_INODE(reiser4_inode_data(inode));	\
	inode_invariant(inode);			\
})

#define spin_unlock_inode(inode)			\
({							\
	inode_invariant(inode);				\
	UNLOCK_INODE(reiser4_inode_data(inode));	\
})

extern int reiser4_max_filename_len(const struct inode *inode);
extern int max_hash_collisions(const struct inode *dir);
extern inter_syscall_rap *inter_syscall_ra(const struct inode *inode);
extern void reiser4_unlock_inode(struct inode *inode);
extern int is_reiser4_inode(const struct inode *inode);
extern int setup_inode_ops(struct inode *inode, reiser4_object_create_data *);
extern int init_inode(struct inode *inode, coord_t * coord);
extern struct inode *reiser4_iget(struct super_block *super, const reiser4_key * key);
extern int reiser4_inode_find_actor(struct inode *inode, void *opaque);

extern void inode_set_flag(struct inode *inode, reiser4_file_plugin_flags f);
extern void inode_clr_flag(struct inode *inode, reiser4_file_plugin_flags f);
extern int inode_get_flag(const struct inode *inode, reiser4_file_plugin_flags f);

extern file_plugin *inode_file_plugin(const struct inode *inode);
extern dir_plugin *inode_dir_plugin(const struct inode *inode);
extern perm_plugin *inode_perm_plugin(const struct inode *inode);
extern tail_plugin *inode_tail_plugin(const struct inode *inode);
extern hash_plugin *inode_hash_plugin(const struct inode *inode);
extern crypto_plugin *inode_crypto_plugin(const struct inode *inode);
extern digest_plugin *inode_digest_plugin(const struct inode *inode);
extern compression_plugin *inode_compression_plugin(const struct inode *inode);
extern item_plugin *inode_sd_plugin(const struct inode *inode);
extern item_plugin *inode_dir_item_plugin(const struct inode *inode);

extern void inode_set_plugin(struct inode *inode, reiser4_plugin * plug);
extern void reiser4_make_bad_inode(struct inode *inode);

extern void inode_set_extension(struct inode *inode, sd_ext_bits ext);
extern void inode_check_scale(struct inode *inode, __u64 old, __u64 new);

#define INODE_SET_FIELD(i, field, value)		\
({							\
	struct inode *__i;				\
	typeof(value) __v;				\
							\
	__i = (i);					\
	__v = (value);					\
	inode_check_scale(__i, __i->field, __v);	\
	__i->field = __v;				\
})

#define INODE_INC_FIELD(i, field)				\
({								\
	struct inode *__i;					\
								\
	__i = (i);						\
	inode_check_scale(__i, __i->field, __i->field + 1);	\
	++ __i->field;						\
})

#define INODE_DEC_FIELD(i, field)				\
({								\
	struct inode *__i;					\
								\
	__i = (i);						\
	inode_check_scale(__i, __i->field, __i->field - 1);	\
	-- __i->field;						\
})

static inline readdir_list_head *
get_readdir_list(const struct inode *inode)
{
	return &reiser4_inode_data(inode)->readdir_list;
}

extern void init_inode_ordering(struct inode *inode, 
				reiser4_object_create_data *crd, int create);

#if REISER4_DEBUG_OUTPUT
extern void print_inode(const char *prefix, const struct inode *i);
#else
#define print_inode(p, i) noop
#endif

/* __REISER4_INODE_H__ */
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
