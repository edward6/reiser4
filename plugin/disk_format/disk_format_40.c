/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"

/*
 * reiser 4.0 default disk layout
 */

/* functions to access fields of layout_40_disk_super_block */
static __u64 get_format_40_block_count (const format_40_disk_super_block * sb)
{
	return d64tocpu (&sb->block_count);
}

static __u64 get_format_40_free_blocks (const format_40_disk_super_block * sb)
{
	return d64tocpu (&sb->free_blocks);
}

static __u64 get_format_40_root_block (const format_40_disk_super_block * sb)
{
	return d64tocpu (&sb->root_block);
}

static __u16 get_format_40_tree_height (const format_40_disk_super_block * sb)
{
	return d16tocpu (&sb->tree_height);
}

static __u64 get_format_40_file_count (const format_40_disk_super_block * sb)
{
	return d64tocpu (&sb->file_count);
}

static __u64 get_format_40_oid (const format_40_disk_super_block * sb)
{
	return d64tocpu (&sb->oid);
}

/* find any valid super block of disk_format_40 (even if the first
 * super block is destroyed) */
static struct buffer_head * find_a_disk_format_40_super_block (struct super_block * s
						  UNUSED_ARG)
{
    struct buffer_head *super_bh;
    format_40_disk_super_block *disk_sb;

    assert("umka-487", s != NULL);
    
    if (!(super_bh = sb_bread(s, (int)(FORMAT_40_OFFSET / s->s_blocksize))))
	return ERR_PTR(-EIO);
    
    disk_sb = (format_40_disk_super_block *)super_bh->b_data;
    if (strcmp(disk_sb->magic, FORMAT_40_MAGIC)) {
	brelse(super_bh);
	return ERR_PTR(-EINVAL);
    }
  
    reiser4_set_block_count(s, d64tocpu(&disk_sb->block_count));
    reiser4_set_data_blocks(s, d64tocpu(&disk_sb->block_count) - d64tocpu(&disk_sb->free_blocks));
    reiser4_set_free_blocks(s, (d64tocpu(&disk_sb->free_blocks)));
    reiser4_set_free_committed_blocks (s, reiser4_free_blocks(s));
    
    return super_bh;
}


/* find the most recent version of super block. This is called after journal is
 * replayed */
static struct buffer_head *read_super_block (struct super_block * s
					      UNUSED_ARG)
{
    /* FIXME-UMKA: Here must be reading of the most recent superblock copy. However, as
     * journal isn't complete, we are using find_any_superblock function. */
    return find_a_disk_format_40_super_block(s);
}

static void done_journal_info (struct super_block *s)
{

	reiser4_super_info_data * private = get_super_private (s);

	assert ("zam-476", private != NULL);

	if (private->journal_header != NULL) {
		if (JF_ISSET(private->journal_header, ZNODE_KMAPPED)) 
			jrelse (private->journal_header);
		if (jnode_page(private->journal_footer))
			jnode_detach_page (private->journal_header);
		jfree (private->journal_header);

		private->journal_header = NULL;
	}

	if (private->journal_footer != NULL) {
		if (JF_ISSET(private->journal_footer, ZNODE_KMAPPED))
			jrelse (private->journal_footer);
		if (jnode_page(private->journal_footer))
			jnode_detach_page (private->journal_footer);
		jfree (private->journal_footer);

		private->journal_footer = NULL;
	}
}

static int init_journal_info (struct super_block * s)
{
	reiser4_super_info_data * private = get_super_private(s);

	int ret;

	ret = -ENOMEM;

	if ((private->journal_header = jnew()) == NULL) return ret;

	if ((private->journal_footer = jnew()) == NULL) goto fail;

	private->journal_header->blocknr = FORMAT_40_JOURNAL_HEADER_BLOCKNR;
	private->journal_footer->blocknr = FORMAT_40_JOURNAL_FOOTER_BLOCKNR;

	if ((ret = jload (private->journal_header)) < 0) goto fail;
	if ((ret = jload (private->journal_footer)) < 0) goto fail;

	return 0;
 fail:
	done_journal_info(s);
	return ret;
}
/* plugin->u.layout.get_ready */
int format_40_get_ready (struct super_block * s, void * data UNUSED_ARG)
{
	int result;
	struct buffer_head * super_bh;
        /* UMKA-FIXME-HANS: needs better name */
	reiser4_super_info_data * private;
	format_40_disk_super_block * sb_copy;
	reiser4_block_nr root_block;
	tree_level height;
	node_plugin * nplug;


	assert ("vs-475", s != NULL);
	assert ("vs-474", get_super_private (s));

	super_bh = find_a_disk_format_40_super_block (s);
	if (IS_ERR (super_bh))
		return PTR_ERR (super_bh);
	brelse (super_bh);

	/* ok, we are sure that filesystem format is a format_40 format */
	result = reiser4_replay_journal (s);
	if (result)
		return result;

	super_bh = read_super_block (s);
	if (IS_ERR (super_bh))
		return PTR_ERR (super_bh);

	/* initialize reiser4_super_info_data */
	private = get_super_private (s);

	/* initialize part of reiser4_super_info_data specific to layout 40 */
	sb_copy = &private->u.format_40.actual_sb;
	memcpy (sb_copy, ((format_40_disk_super_block *)super_bh->b_data),
		sizeof (*sb_copy));
	brelse (super_bh);

	/* layout 40 uses oid_40 oid allocator - the one implemented in
	 * plugin/oid/oid_40.[ch] */
	private->oid_plug = oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID);
	assert ("vs-492", (private->oid_plug &&
			   private->oid_plug->init_oid_allocator));
	/* init oid allocator */
	result = private->oid_plug->init_oid_allocator (get_oid_allocator (s),
							get_format_40_file_count (sb_copy),
							get_format_40_oid (sb_copy));
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
	root_block = get_format_40_root_block (sb_copy);
	height = get_format_40_tree_height (sb_copy);
	nplug = node_plugin_by_id (NODE40_ID);

	/* init reiser4_tree for the filesystem */
	result = init_tree (&private->tree, s, &root_block, height, nplug,
			    &page_cache_tops);
	if (result)
		return result;

	/* initialize reiser4_super_info_data */
	private->default_uid = 0;
	private->default_gid = 0;

	reiser4_set_block_count (s, get_format_40_block_count (sb_copy));
	/* number of used blocks */
	reiser4_set_data_blocks (s, get_format_40_block_count (sb_copy));
	reiser4_set_free_blocks (s, get_format_40_free_blocks (sb_copy));
	reiser4_set_free_committed_blocks (s, reiser4_free_blocks (s));

	private->inode_generation = get_format_40_oid (sb_copy);
	private->fsuid = 0;
	/* FIXME-VS: this is should be taken from mount data? */
	private->trace_flags = 0;
	private->adg = 1; /* hard links for directories are not supported */
	private->one_node_plugin = 1; /* all nodes in layout 40 are of one
				       * plugin */

	result = init_journal_info (s); /* map jnodes for journal control
					    * blocks (header, footer) to
					    * disk  */

	if (result)
		return result;
	
	/* FIXME-VS: maybe this should be dealt with in common code */
	xmemset(&private->stats, 0, sizeof (reiser4_stat));
	/* private->tmgr is initialized already */


#if REISER4_DEBUG
	/*
	 * FIXME-VS: init_tree worked already
	 */
	/*private->kmalloc_allocated = 0;*/
#endif
	return 0;
}

int format_40_release (struct super_block * s)
{
	int ret;

	if ((ret = txn_mgr_force_commit (s))) {
		warning ("jmacd-77114429378", 
			 "txn_force failed in umount: %d", ret);
	}

	done_tree (&get_super_private (s)->tree);

	assert ("zam-579", get_super_private(s) != NULL);
	assert ("zam-580", get_super_private(s)->space_plug != NULL);

	if (get_super_private(s)->space_plug->destroy_allocator != NULL) 
		get_super_private(s)->space_plug->destroy_allocator(&get_super_private(s)->space_allocator, s);

	done_journal_info(s);

	return 0;
}
	


#define FORMAT40_ROOT_LOCALITY 41
#define FORMAT40_ROOT_OBJECTID 42

/* plugin->u.layout.root_dir_key */
const reiser4_key * format_40_root_dir_key (
	const struct super_block * super UNUSED_ARG)
{
	static const reiser4_key FORMAT_40_ROOT_DIR_KEY = {
		.el = { { (FORMAT40_ROOT_LOCALITY  << 4 ) | KEY_SD_MINOR }, 
			{ FORMAT40_ROOT_OBJECTID }, { 0ull } }
	};
	
	return &FORMAT_40_ROOT_DIR_KEY;
}
