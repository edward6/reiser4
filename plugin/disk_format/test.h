/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#define TEST_MAGIC "TestLay"

/* ondisk super block for test layout */
typedef struct test_disk_super_block {
	char magic [8];
	d64 root_block;    /* root of tree */
	d16 tree_height;

	d16 root_dir_plugin; /* not used */
	d16 root_hash_plugin;/* not used */
	d16 node_plugin;
	d16 tail_policy;
	d16 not_used [3];

	d64 block_count;      /* number of blocks on device */
	d64 next_free_block;  /* smallest free block */

	d64 next_free_oid;    /* smallest oid to allocated */

	d64 root_locality;    /* key of root directory */
	d64 root_objectid;
	
} test_disk_super_block;


/* test layout specific part of reiser4_super_info_data */
typedef struct {
	reiser4_key root_dir_key;
	reiser4_block_nr new_blocknr;
} test_layout_super_info;


/* declarations of functions implementing methods of layout plugin for
 * test layout. The functions theirself are in
 * plugin/disk_format/test.c */
int                 test_layout_get_ready    (struct super_block *, void * data);
const reiser4_key * test_layout_root_dir_key (const struct super_block *);
int                 test_layout_release      (struct super_block *);
void                test_layout_print_info   (struct super_block *);
