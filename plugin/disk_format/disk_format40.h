/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* this file contains:
   - definition of ondisk super block of standart disk layout for
     reiser 4.0 (layout 40)
   - definition of layout 40 specific portion of in-core super block
   - declarations of functions implementing methods of layout plugin
     for layout 40
   - declarations of functions used to get/set fields in layout 40 super block
*/

#ifndef __DISK_FORMAT40_H__
#define __DISK_FORMAT40_H__

/* magic for default reiser4 layout */
#define FORMAT40_MAGIC "R4Sb-Default"
#define FORMAT40_OFFSET (65536 + 4096)

#include "../../dformat.h"

#include <linux/fs.h>		/* for struct super_block  */

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
	/* magic string R4Sb-Default */
	/*  68 */ d16 tree_height;
	/* height of filesystem tree */
	/*  70 */ d16 tail_policy;
	/*  72 */ char not_used[440];
} format40_disk_super_block;

/* format 40 specific part of reiser4_super_info_data */
typedef struct format40_super_info {
/*	format40_disk_super_block actual_sb; */
	jnode *sb_jnode;
} format40_super_info;

#define FORMAT40_FIXMAP_MAGIC "R4FiXMaPv1.0"
/* ondisk fixmap table for format 40, it's length is up to block size */
typedef struct format40_fixmap_block {
	/* magic */
	/*   0 */ char magic[16];
	/* if not zero - actual location of format-specific superblock */
	/*  16 */ d64 fm_super;
	/* if not zero - actual location of format-secific journal header */
	/*  24 */ d64 fm_journal_header;
	/* if not zero - actual location of format-secific journal footer */
	/*  32 */ d64 fm_journal_footer;
	/* fixmap table for bitmaps. */
	/* This one is just a list of bitmaps that happen to hit badblocks, and
	   blocknumbers where they are stored now. If the table will grow so big
	   that it won't fit on single block, the last entry in this array should
	   have bmap_nr equal to -1 and then the blocknumber will be threated as
	   the block where this table is continued. Entries in the table should be
	   sorted by bmap_nr. Last entry is represented with {0,0} */
	/*  40 */
	struct {
		/* Bitmap number */
		d64 bmap_nr;
		/* blocknumber where this bitmap lives */
		d64 new_block;
	} fm_bitmap_table[1];
} format40_fixmap_info;

#define FORMAT40_JOURNAL_HEADER_BLOCKNR 19
#define FORMAT40_JOURNAL_FOOTER_BLOCKNR 20

/* declarations of functions implementing methods of layout plugin for
   format 40. The functions theirself are in disk_format40.c */
int format40_get_ready(struct super_block *, void *data);
const reiser4_key *format40_root_dir_key(const struct super_block *);
int format40_release(struct super_block *s);
jnode *format40_log_super(struct super_block *s);
void format40_print_info(const struct super_block *s);

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
