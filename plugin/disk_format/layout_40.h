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

/* magic for default reiser4 layout */
#define LAYOUT_40_MAGIC "R4Sb-Default"

/* ondisk super block for layout 40. It is 512 bytes long */
typedef struct layout_40_disk_super_block {	
	/*   0 */ d64 block_count; /* number of block in a filesystem */
	/*   8 */ d64 free_blocks; /* number of free blocks */
	/*  16 */ d64 root_block;  /* filesystem tree root block */
	/*  28 */ d64 oid;	   /* smallest free objectid */
	/*  36 */ d64 file_count;  /* number of files in a filesystem */
	/*  44 */ d64 flushes;  /* number of times super block was
				    * flushed. Needed if layout 40
				    * will have few super blocks */
	/*  52 */ char magic[16]; /* magic string R4Sb-Default */
	
	/*  68 */ d16 journal_plugin_id; /* journal plugin identifier */
	/*  70 */ d16 alloc_plugin_id; /* space allocator plugin identifier */
	/*  24 */ d16 tree_height; /* height of filesystem tree */
	/*  26 */ d16 padd [3];
	/*  72 */ char not_used [440];
} layout_40_disk_super_block;


/* layout 40 specific part of reiser4_super_info_data */
typedef struct layout_40_super_info {
	layout_40_disk_super_block actual_sb;
} layout_40_super_info;


/* declarations of functions implementing methods of layout plugin for
 * layout 40. The functions theirself are in layout_40.c */
int                 layout_40_get_ready    (struct super_block *, void * data);
const reiser4_key * layout_40_root_dir_key (const struct super_block *);

