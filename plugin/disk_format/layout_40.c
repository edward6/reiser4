/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"

/*
 * reiser 4.0 default disk layout
 */

/* functions to access fields of layout_40_disk_super_block */
static __u64 get_layout_40_block_count (const layout_40_disk_super_block * sb)
{
	return d64tocpu (&sb->block_count);
}

static __u64 get_layout_40_free_blocks (const layout_40_disk_super_block * sb)
{
	return d64tocpu (&sb->free_blocks);
}

static __u64 get_layout_40_root_block (const layout_40_disk_super_block * sb)
{
	return d64tocpu (&sb->root_block);
}

static __u16 get_layout_40_tree_height (const layout_40_disk_super_block * sb)
{
	return d16tocpu (&sb->tree_height);
}

static __u64 get_layout_40_file_count (const layout_40_disk_super_block * sb)
{
	return d64tocpu (&sb->file_count);
}

static __u64 get_layout_40_oid (const layout_40_disk_super_block * sb)
{
	return d64tocpu (&sb->oid);
}

/* FIXME-JMACD: is part of disk layout plugin's do_on_mount method right place
 * for it */
static int replay_journal (struct super_block * s UNUSED_ARG)
{
	return 0;
}


/* find any super block of layout 40 */
static struct buffer_head * find_any_super_block (struct super_block * s
						  UNUSED_ARG)
{
	return ERR_PTR (-ENOSYS);
}


/* find the most recent version of super block. This is called after journal is
 * replayed */
static struct buffer_head * read_super_block (struct super_block * s
					      UNUSED_ARG)
{
	return ERR_PTR (-ENOSYS);
}


/* plugin->u.layout.get_ready */
int layout_40_get_ready (struct super_block * s, void * data UNUSED_ARG)
{
	int result;
	struct buffer_head * super_bh;
	reiser4_super_info_data * private;
	layout_40_disk_super_block * sb_copy;
	reiser4_block_nr root_block;
	tree_level height;
	node_plugin * nplug;


	assert ("vs-475", s != NULL);
	assert ("vs-474", get_super_private (s));


	super_bh = find_any_super_block (s);
	if (IS_ERR (super_bh))
		return PTR_ERR (super_bh);
	brelse (super_bh);

	/* ok, we are sure that filesystem format is a layout 40 format */
	result = replay_journal (s);
	if (result)
		return result;

	super_bh = read_super_block (s);
	if (IS_ERR (super_bh))
		return PTR_ERR (super_bh);

	private = get_super_private (s);

	/* initialize part of reiser4_super_info_data specific to layout 40 */
	sb_copy = &private->u.layout_40.actual_sb;
	memcpy (sb_copy, ((layout_40_disk_super_block *)super_bh->b_data),
		sizeof (*sb_copy));
	brelse (super_bh);

	/* FIXME-VS: shouldn't this be in reiser4_fil_super */
	spin_lock_init (&private->guard);

	/* layout 40 uses oid_40 oid allocator - the one implemented in
	 * plugin/oid/oid_40.[ch] */
	private->oid_plug = oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID);
	assert ("vs-492", (private->oid_plug &&
			   private->oid_plug->init_oid_allocator));
	/* init oid allocator */
	result = private->oid_plug->init_oid_allocator (get_oid_allocator (s),
							get_layout_40_file_count (sb_copy),
							get_layout_40_oid (sb_copy));
	if (result)
		return result;

	/* layout 40 uses bitmap based space allocator - the one implemented in
	 * plugin/space/bitmap.[ch] */
	private->space_plug =
		space_allocator_plugin_by_id (BITMAP_SPACE_ALLOCATOR_ID);
	assert ("vs-493", (private->space_plug &&
			   private->space_plug->init_allocator));
	/* init disk space allocator */
	result = private->space_plug->init_allocator (get_space_allocator (s), s, 0);
	if (result)
		return result;

	/* get things necessary to init reiser4_tree */
	root_block = get_layout_40_root_block (sb_copy);
	height = get_layout_40_tree_height (sb_copy);
	nplug = node_plugin_by_id (NODE40_ID);

	/* init reiser4_tree for the filesystem */
	result = init_tree (&private->tree, &root_block, height, nplug,
			    default_read_node, default_allocate_node, default_unread_node);
	if (result)
		return result;

	/* initialize reiser4_super_info_data */
	private->default_uid = 0;
	private->default_gid = 0;

	reiser4_set_block_count (s, get_layout_40_block_count (sb_copy));
	/* number of used blocks */
	reiser4_set_data_blocks (s, get_layout_40_block_count (sb_copy));
	reiser4_set_free_blocks (s, get_layout_40_free_blocks (sb_copy));
	reiser4_set_free_committed_blocks (s, reiser4_free_blocks (s));

	private->inode_generation = get_layout_40_oid (sb_copy);
	private->fsuid = 0;
	/* FIXME-VS: this is should be taken from mount data? */
	private->trace_flags = 0;
	private->adg = 1; /* hard links for directories are not supported */
	private->one_node_plugin = 1; /* all nodes in layout 40 are of one
				       * plugin */

	/* FIXME-VS: maybe this should be dealt with in common code */
	xmemset (&private->stats, 0, sizeof (reiser4_stat));
	/* private->tmgr is initialized already */

	/* init fake inode which will be used to read all the formatted nodes
	 * into page cache */
	init_formatted_fake (s);

#if REISER4_DEBUG
	private->kmalloc_allocated = 0;
#endif
	return 0;
}

/* plugin->u.layout.root_dir_key */
const reiser4_key * layout_40_root_dir_key (
	const struct super_block * super UNUSED_ARG)
{
	static const reiser4_key LAYOUT_40_ROOT_DIR_KEY = {
		.el = { { ( 2 << 4 ) | KEY_SD_MINOR }, { 0x10002ull }, { 0ull } }
	};

	return &LAYOUT_40_ROOT_DIR_KEY;
}
