/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"

static void print_test_disk_sb (const char *, const test_disk_super_block *);

/* plugin->u.layout.get_ready */
int test_layout_get_ready (struct super_block * s, void * data UNUSED_ARG)
{
	int result;
	reiser4_key * root_key;
	test_disk_super_block * disk_sb;
	struct buffer_head * super_bh;
	reiser4_super_info_data * private;
	reiser4_block_nr root_block;
	tree_level height;


	private = get_super_private (s);
	assert ("vs-626", private);
	
	super_bh = sb_bread (s, (int)(REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh)
		return -EIO;
	
	disk_sb = (test_disk_super_block *)(super_bh->b_data + 
					    sizeof (struct reiser4_master_sb));

	if (strcmp (disk_sb->magic, TEST_MAGIC)) {
		brelse (super_bh);
		return -EINVAL;
	}

	/* FIXME-VS: remove this debugging info */
	print_test_disk_sb ("get_ready:\n", disk_sb);

	/* store key of root directory in layout specific part of
	 * reiser4 private super data */
	root_key = &private->u.test_layout.root_dir_key;
	key_init (root_key);
	set_key_locality (root_key, d64tocpu (&disk_sb->root_locality));
	set_key_objectid (root_key, d64tocpu (&disk_sb->root_objectid));
	set_key_type (root_key, KEY_SD_MINOR);
	set_key_offset (root_key, (__u64)0);

	/* initialize fields of reiser4 private part of super block which
	 * are common for all disk formats
	 * FIXME-VS: shouldn't that initizlization be in common code? */
	reiser4_set_block_count (s, d64tocpu (&disk_sb->block_count));
	/* number of used blocks */
	reiser4_set_data_blocks (s, d64tocpu (&disk_sb->next_free_block));
	reiser4_set_free_blocks (s, (d64tocpu (&disk_sb->block_count) -
				     d64tocpu (&disk_sb->next_free_block)));
	/* set tail policy plugin */
	get_super_private (s)->tplug =
		tail_plugin_by_id (d16tocpu (&disk_sb->tail_policy));

	/* init oid allocator */		  
	private->oid_plug = oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID);
	assert ("vs-627", (private->oid_plug &&
			   private->oid_plug->init_oid_allocator));
	result = private->oid_plug->init_oid_allocator (get_oid_allocator (s),
							d64tocpu (&disk_sb->next_free_oid),
							d64tocpu (&disk_sb->next_free_oid));

	/* init space allocator */
	private->space_plug = space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID);
	assert ("vs-628", (private->space_plug &&
			   private->space_plug->init_allocator));
	result = private->space_plug->init_allocator (get_space_allocator (s), s,
						      &disk_sb->next_free_block);
	if (result) {
		brelse (super_bh);
		return result;
	}

	/* init reiser4_tree for the filesystem */
	root_block = d64tocpu (&disk_sb->root_block);
	height = d16tocpu (&disk_sb->tree_height);
	assert ("vs-642", d16tocpu (&disk_sb->node_plugin) == NODE40_ID);
	result = init_tree (&private->tree, s, &root_block, height, 
			    node_plugin_by_id (NODE40_ID),
			    &page_cache_tops);
	if (result) {
		brelse (super_bh);
		return result;
	}

	/* FIXME-VS: move up to reiser4_fill_super? */
	result = init_formatted_fake (s);

	brelse (super_bh);
	return result;
}


/* plugin->u.layout.root_dir_key */
const reiser4_key * test_layout_root_dir_key (const struct super_block * s)
{
	return &(get_super_private (s)->u.test_layout.root_dir_key);
}


/* plugin->u.layout.release */
void test_layout_release (struct super_block * s)
{
	struct buffer_head * super_bh;
	test_disk_super_block * disk_sb;

	done_tree (&get_super_private (s)->tree);

	super_bh = sb_bread (s, (int)(REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh) {
		warning ("vs-630", "could not read super block");
		return;
	}
	disk_sb = (test_disk_super_block *)(super_bh->b_data + 
					    sizeof (struct reiser4_master_sb));
	if (strcmp (disk_sb->magic, TEST_MAGIC)) {
		warning ("vs-631", "no test layout found");
		brelse (super_bh);
		return;
	}

	/*
	 * update test disk format super block
	 */
	/* root block */
	cputod64 (get_super_private (s)->tree.root_block, &disk_sb->root_block);

	/* tree height */
	cputod16 (get_super_private (s)->tree.height, &disk_sb->tree_height);

	/* number of next free block */
	cputod64 (get_space_allocator (s)->u.test.new_block_nr,
		  &disk_sb->next_free_block);

	/* next free objectid */
	cputod64 (get_oid_allocator (s)->u.oid_40.next_to_use, &disk_sb->next_free_oid);

	/* */
	

	/* FIXME-VS: remove this debugging info */
	print_test_disk_sb ("release:\n", disk_sb);

	mark_buffer_dirty (super_bh);
	ll_rw_block (WRITE, 1, &super_bh);
	wait_on_buffer (super_bh);
	brelse (super_bh);

	return;
}

static void print_test_disk_sb (const char * mes,
				const test_disk_super_block * disk_sb)
{
	info ("%s", mes);
	info ("root %llu, tree height %u,\n"
	      "block count %llu, next free block %llu,\n"
	      "next free oid %llu\n"
	      "root dir [%llu %llu]\n"
	      "tail policy \"%s\"\n"
	      "node format \"%s\"\n",
	      d64tocpu (&disk_sb->root_block),
	      d16tocpu (&disk_sb->tree_height),
	      d64tocpu (&disk_sb->block_count), d64tocpu (&disk_sb->next_free_block), 
	      d64tocpu (&disk_sb->next_free_oid),
	      d64tocpu (&disk_sb->root_locality), d64tocpu (&disk_sb->root_objectid),
	      tail_plugin_by_id (d16tocpu (&disk_sb->tail_policy))->h.label,
	      node_plugin_by_id (d16tocpu (&disk_sb->node_plugin))->h.label);
}


/* plugin->u.layout.print_info */
void test_layout_print_info (struct super_block * s)
{
	/* print filesystem information common to all layouts */
	print_fs_info (s);

	/* print some info from test layout specific part of reiser4
	 * private super block */
	info ("test layout super info:\n");
	print_key ("root_key", test_layout_root_dir_key (s));
}

