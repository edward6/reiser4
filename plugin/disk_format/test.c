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

	/* init oid allocator */		  
	private->oid_plug = oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID);
	assert ("vs-627", (private->oid_plug &&
			   private->oid_plug->init_oid_allocator));
	result = private->oid_plug->init_oid_allocator (get_oid_allocator (s),
							d64tocpu (&disk_sb->next_to_use),
							d64tocpu (&disk_sb->next_to_use));

	/* init space allocator */
	private->space_plug = space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID);
	assert ("vs-628", (private->space_plug &&
			   private->space_plug->init_allocator));
	result = private->space_plug->init_allocator (get_space_allocator (s), s,
						      &disk_sb->new_block_nr);
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
	if (result)
		return result;

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

	cputod64 (get_super_private (s)->tree.root_block, &disk_sb->root_block);
	cputod16 (get_super_private (s)->tree.height, &disk_sb->tree_height);
	/* oid allocator is a oid_40 one */
	assert ("vs-633",
		get_super_private (s)->oid_plug ==
		oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID));
	{
		oid_t oid;

		get_super_private (s)->oid_plug->allocate_oid (get_oid_allocator (s),
							       &oid);
		cputod64 (oid, &disk_sb->next_to_use);
	}
	/* block allocator is a test one */
	assert ("vs-632",
		get_super_private (s)->space_plug ==
		space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID));
	cputod64 (get_space_allocator (s)->u.test.new_block_nr, &disk_sb->new_block_nr);

#if 0
	/* FIXME-GREEN: generic_shutdown_super clears s->s_root */
	/* root directory key */
	cputod64 (reiser4_inode_data (s->s_root->d_inode)->locality_id,
		  &disk_sb->root_locality);
	cputod64 ((__u64)(s->s_root->d_inode->i_ino), &disk_sb->root_objectid);
#endif
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
	      "next oid %llu, next free block %llu,\n"
	      "root dir [%llu %llu]\n", d64tocpu (&disk_sb->root_block),
	      d16tocpu (&disk_sb->tree_height), d64tocpu (&disk_sb->next_to_use),
	      d64tocpu (&disk_sb->new_block_nr), d64tocpu (&disk_sb->root_locality),
	      d64tocpu (&disk_sb->root_objectid));
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

