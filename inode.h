/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Inode functions. */

#if !defined( __REISER4_INODE_H__ )
#define __REISER4_INODE_H__

#include "forward.h"
#include "debug.h"
#include "spin_macros.h"
#include "key.h"
#include "seal.h"
#include "scint.h"
#include "plugin/plugin.h"
#include "plugin/security/perm.h"
#include "vfs_ops.h"

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
	/* this is set when we know for sure state of file: for default
	   reiser4 ordinary files it means that we know whether file is built
	   of extents or of tail items only */
	REISER4_FILE_STATE_KNOWN = 4,
	/* this is set to 1 when not all file data are stored as unformatted
	   node, 0 - otherwise. Note, that this bit can be only checked if
	   REISER4_FILE_STATE_KNOWN is set */
	REISER4_BUILT_OF_TAILS = 5,
	/* this bit is set for symlinks. inode->u.generic_ip points to target
	   name of symlink */
	REISER4_GENERIC_VP_USED = 6,
	REISER4_EXCLUSIVE_USE = 7,
	REISER4_SDLEN_KNOWN   = 8,
	/* reiser4_inode->keyid points to the identification word */
	REISER4_KEYID_LOADED = 9,
	/* reiser4_inode->expkey points to the secret key */
	REISER4_SECRET_KEY_INSTALLED = 10
} reiser4_file_plugin_flags;

#if BITS_PER_LONG == 64
#define REISER4_INO_IS_OID (1)
typedef struct {;
} oid_hi_t;
#else
#define REISER4_INO_IS_OID (0)
typedef __u32 oid_hi_t;
#endif

#define OID_HI_SHIFT (sizeof(ino_t) * 8)

/* state associated with each inode.
   reiser4 inode.
  
   NOTE-NIKITA In 2.5 kernels it is not necessary that all file-system inodes
   be of the same size. File-system allocates inodes by itself through
   s_op->allocate_inode() method. So, it is possible to adjust size of inode
   at the time of its creation.
  
*/
typedef struct reiser4_inode {
	/* plugin of file */
	/*  0 */ file_plugin *file;
	/* plugin of dir */
	/*  4 */ dir_plugin *dir;
	/* perm plugin for this file */
	/*  8 */ perm_plugin *perm;
	/* tail policy plugin. Only meaningful for regular files */
	/* 12 */ tail_plugin *tail;
	/* hash plugin. Only meaningful for directories. */
	/* 16 */ hash_plugin *hash;
	/* plugin of stat-data */
	/* 20 */ item_plugin *sd;
	/* plugin of items a directory is built of */
	/* 24 */ item_plugin *dir_item;
	/* 28 */ struct inode *parent;
	/* seal for stat-data */
	/* 32 */ seal_t sd_seal;
	/* locality id for this file */
	/* 48 */ oid_t locality_id;
	/* coord of stat-data in sealed node */
	/* 56 */ coord_t sd_coord;
	/* truncate, tail2extent and extent2tail use down_write, read, write,
	 * readpage - down_read */
	/* 68 */ struct rw_semaphore sem;
	/* high 32 bits of object id */
	/* 84 */ oid_hi_t oid_hi;
	/* 88 */ scint_t extmask;
	/* 92 */ int eflushed;
	/* bitmask of non-default plugins for this inode */
	/* 96 */ __u16 plugin_mask;
	/* 98 */ inter_syscall_rap ra;
	/* 98 */ __u16 padding;
	/* secret key and its id. The last supposed to be stored on disk.
	   Only meaningful for crypto-files */
	/* 100 */__u32 *expkey;
	/* 104 */__u8 *keyid;
	/* crypto plugin */
	/* 108 */crypto_plugin *crypto;
	/* compression plugin */
	/* 112 */compression_plugin *compression;
	/* 116 */
	struct list_head moved_pages;
#if REISER4_DEBUG
	/* pointer to task struct of thread owning exclusive access to file */
	void *ea_owner;
#endif
} reiser4_inode;

typedef struct reiser4_inode_object {
	/* private part */
	reiser4_inode p;
	/* generic fields not specific to reiser4, but used by VFS */
	struct inode vfs_inode;
} reiser4_inode_object;

#define spin_ordering_pred_inode(inode)   (1)
SPIN_LOCK_FUNCTIONS(inode, struct inode, i_mapping->private_lock);

extern oid_t get_inode_oid(const struct inode *inode);
extern void set_inode_oid(struct inode *inode, oid_t oid);
extern ino_t oid_to_ino(oid_t oid) __attribute__ ((const));
extern ino_t oid_to_uino(oid_t oid) __attribute__ ((const));

extern reiser4_tree *tree_by_inode(const struct inode *inode);
extern reiser4_inode *reiser4_inode_data(const struct inode *inode);
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
	return (readdir_list_head *)&inode->i_mapping->private_list;
}

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
