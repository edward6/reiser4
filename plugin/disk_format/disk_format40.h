/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/*
 * Objects of Standard Disk Layout for simple volumes (i.e. volumes
 * associated with a single physical, or logical (RAID, LVM) device.
 */

#ifndef __DISK_FORMAT40_H__
#define __DISK_FORMAT40_H__

/* magic for default reiser4 layout */
#define FORMAT40_MAGIC "ReIsEr40FoRmAt"
#define FORMAT40_OFFSET (REISER4_MASTER_OFFSET + PAGE_SIZE)

#include "../../dformat.h"
#include <linux/fs.h>

typedef enum {
	FORMAT40_LARGE_KEYS,
	FORMAT40_UNBALANCED_VOLUME,
	FORMAT40_HAS_DATA_ROOM,
	FORMAT40_TO_BE_REMOVED,
	FORMAT40_PLANB_KEY_ALLOC,
} format40_flags;

/* ondisk super block for format 40. It is 512 bytes long */
typedef struct format40_disk_super_block {
	/*   0 */ d64 block_count;
	/* number of block in a filesystem */
	/*   8 */ d64 free_blocks;
	/* number of free blocks */
	/*  16 */ d64 root_block;
	/* filesystem tree root block */
	/*  24 */ d64 oid;
	/* smallest free objectid */
	/*  32 */ d64 file_count;
	/* number of files in a filesystem */
	/*  40 */ d64 flushes;
	/* number of times super block was
	   flushed. Needed if format 40
	   will have few super blocks */
	/*  48 */ d32 mkfs_id;
	/* unique identifier of fs */
	/*  52 */ char magic[16];
	/* magic string ReIsEr40FoRmAt */
	/*  68 */ d16 tree_height;
	/* height of filesystem tree */
	/*  70 */ d16 formatting_policy;
	/* not used anymore */
	/*  72 */ d64 flags;
	/*  80 */ d32 version;
	/* on-disk format version number
	   initially assigned by mkfs as the greatest format40
	   version number supported by reiser4progs and updated
	   in mount time in accordance with the greatest format40
	   version number supported by kernel.
	   Is used by fsck to catch possible corruption and
	   for various compatibility issues */
	/*  84 */ d32 node_pid; /* formatted node plugin id */

	/* Reiser5 fields */
	/*  88 */ d64 origin_id;    /* internal ID of the subvolume. It gets assigned
				       once and never changes */
	/*  96 */ d64 nr_origins;   /* total number of original subvolumes in LV */
	/* 104 */ d64 data_capacity;/* weight of the brick in data storage array */
	/* 112 */ d64 volinfo_loc;  /* location of the first block of system LV info */
	/* 120 */ d8  num_sgs_bits; /* logarithm of total number of the hash-space
				       segments */
	/* 121 */ d64 nr_mslots;    /* number of mslots (== maximal brick ID + 1) */
	/* 129 */ d64 min_occup;    /* mimimal possible number of occupied blocks on
				     * the partition (reserved area at the beginning
				     * of the partition + 2 super-blocks + 1 journal
				     * footer + 1 journal header + backup blocks that
				     * kernel is not aware of, etc). This is set by
				     * mkfs.reiser4 utility and never gets changed */
	char not_used[375];
} __attribute__((packed)) format40_disk_super_block;

/* Defines for journal header and footer respectively. */
#define FORMAT40_JOURNAL_HEADER_BLOCKNR			\
	((REISER4_MASTER_OFFSET / PAGE_SIZE) + 3)

#define FORMAT40_JOURNAL_FOOTER_BLOCKNR			\
	((REISER4_MASTER_OFFSET / PAGE_SIZE) + 4)

#define FORMAT40_STATUS_BLOCKNR				\
	((REISER4_MASTER_OFFSET / PAGE_SIZE) + 5)

/* Diskmap declarations */
#define FORMAT40_PLUGIN_DISKMAP_ID ((REISER4_FORMAT_PLUGIN_TYPE<<16) | (FORMAT40_ID))
#define FORMAT40_SUPER 1
#define FORMAT40_JH 2
#define FORMAT40_JF 3

/*
 * declarations of functions implementing methods of layout plugin
 * for format40. The functions theirself are in disk_format40.c
 */
extern int extract_subvol_id_format40(struct block_device *bdev, u64 *subv_id);
extern int extract_subvol_id_format41(struct block_device *bdev, u64 *subv_id);
extern int init_format_format40(struct super_block *, reiser4_subvol *);
extern int init_format_format41(struct super_block *, reiser4_subvol *);
extern const reiser4_key *root_dir_key_format40(const struct super_block *);
extern int release_format40(struct super_block *s, reiser4_subvol *);
extern jnode *log_super_format40(struct super_block *s, reiser4_subvol *);
extern int check_open_format40(const struct inode *object);
extern int version_update_format40(struct super_block *super, reiser4_subvol *);

/* __DISK_FORMAT40_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
