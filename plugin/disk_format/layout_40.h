/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * this file contains:
 * - definition of ondisk super block of standart disk layout for
 *   reiser 4.0 (layout 40)
 * - definition of layout 40 specific portion of in-core super block
 * - declarations of functions implementing methods of layout plugin
 *   for layout 40
 * - declarations of functions used to get/set fields in layout 40 super block
 */

/* ondisk super block for layout 40. It is 512 bytes long */
typedef struct layout_40_disk_super_block {	
	/*   0 */ d64 block_count; /* number of block in a filesystem */
	/*   8 */ d64 free_blocks; /* number of free blocks */
	/*  12 */ d64 root_block;  /* filesystem tree root block */
	/*  20 */ d16 tree_height; /* height of filesystem tree */
	/*  22 */ d16 padd [3];
	/*  28 */ d64 oid;	   /* smallest free objectid */
	/*  32 */ d64 file_count;  /* number of files in a filesystem */
	/*  48 */ d64 flushes;  /* number of times super block was
				    * flushed. Needed if layout 40
				    * will have few super blocks */
	/*  56 */ char magic[16]; /* magic string R4Sb-Default */
	/*  56 */ char not_used [408]; /* 88 */
} layout_40_disk_super_block;


/* layout 40 specific part of reiser4_super_info_data */
typedef struct layout_40_super_info {
	layout_40_disk_super_block actual_sb;
} layout_40_super_info;


/* declarations of functions implementing methods of layout plugin for
 * layout 40. The functions theirself are in layout_40.c */
int                 layout_40_get_ready    (struct super_block *, void * data);
const reiser4_key * layout_40_root_dir_key (const struct super_block *);

