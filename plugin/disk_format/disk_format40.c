/* Copyright 2002 Hans Reiser, licensing governed by reiser4/README */

#include "../../debug.h"
#include "../../dformat.h"
#include "../../key.h"
#include "../node/node.h"
#include "../space/space_allocator.h"
#include "disk_format40.h"
#include "../oid/oid.h"
#include "../plugin.h"
#include "../../txnmgr.h"
#include "../../jnode.h"
#include "../../tree.h"
#include "../../super.h"
#include "../../wander.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/buffer_head.h>

/* reiser 4.0 default disk layout */

/* functions to access fields of format40_disk_super_block */
static __u64
get_format40_block_count(const format40_disk_super_block * sb)
{
	return d64tocpu(&sb->block_count);
}

static __u64
get_format40_free_blocks(const format40_disk_super_block * sb)
{
	return d64tocpu(&sb->free_blocks);
}

static __u64
get_format40_root_block(const format40_disk_super_block * sb)
{
	return d64tocpu(&sb->root_block);
}

static __u16
get_format40_tree_height(const format40_disk_super_block * sb)
{
	return d16tocpu(&sb->tree_height);
}

static __u64
get_format40_file_count(const format40_disk_super_block * sb)
{
	return d64tocpu(&sb->file_count);
}

static __u64
get_format40_oid(const format40_disk_super_block * sb)
{
	return d64tocpu(&sb->oid);
}

static __u16
get_format40_tail_policy(const format40_disk_super_block * sb)
{
	return d16tocpu(&sb->tail_policy);
}

static __u32
get_format40_mkfs_id(const format40_disk_super_block * sb)
{
	return d32tocpu(&sb->mkfs_id);
}

/* find any valid super block of disk_format40 (even if the first
   super block is destroyed), will change block numbers of actual journal header/footer (jf/jh)
   if needed */
static struct buffer_head *
find_a_disk_format40_super_block(struct super_block *s UNUSED_ARG, reiser4_block_nr *jf UNUSED_ARG, reiser4_block_nr *jh UNUSED_ARG)
{
	struct buffer_head *super_bh;
	format40_disk_super_block *disk_sb;
	sector_t rootblock = FORMAT40_OFFSET / s->s_blocksize; /* Default format-specific location */

	assert("umka-487", s != NULL);

#ifdef CONFIG_REISER4_BADBLOCKS
	if ( get_super_private(s)->fixmap_block ) { /* If there is fixmap table, need to read and parse it */
		struct buffer_head *fixmap_bh;
		struct format40_fixmap_block *fixmap;

		fixmap_bh = sb_bread(s, get_super_private(s)->fixmap_block);
		if ( !fixmap_bh )
			return ERR_PTR(RETERR(-EIO));

		fixmap = (struct format40_fixmap_block *) fixmap_bh->b_data;
		/* Compare magic */
		if ( strncmp(fixmap->magic, FORMAT40_FIXMAP_MAGIC, sizeof(FORMAT40_FIXMAP_MAGIC)-1 ) ) {
			/* Wrong magic */
			brelse(fixmap_bh);
			warning("green-2003", "fixmap is specified, but its magic is wrong");
			return ERR_PTR(RETERR(-EINVAL));
		}

		/* Check for alternative superblock location */
		if ( d64tocpu(&fixmap->fm_super) )
			rootblock = d64tocpu(&fixmap->fm_super);

		/* Check for alternative journal header location */
		if ( jh && d64tocpu(&fixmap->fm_journal_header) )
			*jh = d64tocpu(&fixmap->fm_journal_header);
		/* Check for alternative journal footer location */
		if ( jf && d64tocpu(&fixmap->fm_journal_footer) )
			*jf = d64tocpu(&fixmap->fm_journal_footer);
		brelse(fixmap_bh);
	}
#endif

	if (!(super_bh = sb_bread(s, rootblock)))
		return ERR_PTR(RETERR(-EIO));

	disk_sb = (format40_disk_super_block *) super_bh->b_data;
	if (strcmp(disk_sb->magic, FORMAT40_MAGIC)) {
		brelse(super_bh);
		return ERR_PTR(RETERR(-EINVAL));
	}

	reiser4_set_block_count(s, d64tocpu(&disk_sb->block_count));
	reiser4_set_data_blocks(s, d64tocpu(&disk_sb->block_count) - d64tocpu(&disk_sb->free_blocks));
	reiser4_set_free_blocks(s, (d64tocpu(&disk_sb->free_blocks)));

	return super_bh;
}

/* find the most recent version of super block. This is called after journal is
   replayed */
static struct buffer_head *
read_super_block(struct super_block *s UNUSED_ARG)
{
	/* FIXME-UMKA: Here must be reading of the most recent superblock copy. However, as
	   journal isn't complete, we are using find_any_superblock function. */
	return find_a_disk_format40_super_block(s, NULL, NULL);
}

static int
get_super_jnode(struct super_block *s)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);
	jnode *sb_jnode;
	reiser4_block_nr super_block_nr = FORMAT40_OFFSET / s->s_blocksize;
	int ret;

#ifdef CONFIG_REISER4_BADBLOCKS
	if ( get_super_private(s)->fixmap_block ) { /* If there is fixmap table, need to read and parse it */
		struct buffer_head *fixmap_bh;
		struct format40_fixmap_block *fixmap;

		fixmap_bh = sb_bread(s, get_super_private(s)->fixmap_block);
		if ( !fixmap_bh )
			return RETERR(-EIO);

		fixmap = (struct format40_fixmap_block *) fixmap_bh->b_data;
		/* Compare magic, all of this is redundant sunce we have already checked this
		   when we first read our super block */
		if ( strncmp(fixmap->magic, FORMAT40_FIXMAP_MAGIC, sizeof(FORMAT40_FIXMAP_MAGIC)-1 ) ) {
			/* Wrong magic */
			brelse(fixmap_bh);
			warning("green-2004", "fixmap is specified, but its magic is wrong");
			return RETERR(-EINVAL);
		}

		/* Check for alternative superblock location */
		if ( d64tocpu(&fixmap->fm_super) )
			super_block_nr = d64tocpu(&fixmap->fm_super);

		brelse(fixmap_bh);
	}
#endif

	sb_jnode = alloc_io_head(&super_block_nr);

	ret = jload(sb_jnode);

	if (ret) {
		drop_io_head(sb_jnode);
		return ret;
	}

	pin_jnode_data(sb_jnode);
	jrelse(sb_jnode);

	sbinfo->u.format40.sb_jnode = sb_jnode;

	return 0;
}

static void
done_super_jnode(struct super_block *s)
{
	jnode *sb_jnode = get_super_private(s)->u.format40.sb_jnode;

	if (sb_jnode) {
		unpin_jnode_data(sb_jnode);
		drop_io_head(sb_jnode);
	}
}

/* plugin->u.layout.get_ready */
int
format40_get_ready(struct super_block *s, void *data UNUSED_ARG)
{
	int result;
	struct buffer_head *super_bh;
	/* UMKA-FIXME-HANS: needs better name */
	reiser4_super_info_data *sbinfo;
	format40_disk_super_block  sb;
	/* FIXME-NIKITA ugly work-around: keep copy of on-disk super-block */
	format40_disk_super_block *sb_copy = &sb;
	reiser4_block_nr root_block;
	tree_level height;
	node_plugin *nplug;

	static reiser4_block_nr jfooter_block = FORMAT40_JOURNAL_FOOTER_BLOCKNR;
	static reiser4_block_nr jheader_block = FORMAT40_JOURNAL_HEADER_BLOCKNR;

	assert("vs-475", s != NULL);
	assert("vs-474", get_super_private(s));

	/* initialize reiser4_super_info_data */
	sbinfo = get_super_private(s);

	super_bh = find_a_disk_format40_super_block(s, &jfooter_block, &jheader_block);
	if (IS_ERR(super_bh))
		return PTR_ERR(super_bh);
	brelse(super_bh);

	/* map jnodes for journal control blocks (header, footer) to disk  */
	result = init_journal_info(s, &jheader_block, &jfooter_block);

	if (result)
		return result;

	result = eflush_init_at(s);
	if (result)
		return result;

	/* ok, we are sure that filesystem format is a format40 format */
	result = reiser4_journal_replay(s);
	if (result)
		return result;

	super_bh = read_super_block(s);
	if (IS_ERR(super_bh))
		return PTR_ERR(super_bh);

	xmemcpy(sb_copy, ((format40_disk_super_block *) super_bh->b_data), sizeof (*sb_copy));
	brelse(super_bh);

	/* init oid allocator */
	sbinfo->oid_plug = oid_allocator_plugin_by_id(OID40_ALLOCATOR_ID);
	result = oid_init_allocator(s, get_format40_file_count(sb_copy), get_format40_oid(sb_copy));
	if (result)
		return result;

	/* initializing tail policy */
	sbinfo->plug.t = tail_plugin_by_id(get_format40_tail_policy(sb_copy));
	assert("umka-751", sbinfo->plug.t);

	/* get things necessary to init reiser4_tree */
	root_block = get_format40_root_block(sb_copy);
	height = get_format40_tree_height(sb_copy);
	nplug = node_plugin_by_id(NODE40_ID);

	sbinfo->tree.super = s;
	/* init reiser4_tree for the filesystem */
	result = init_tree(&sbinfo->tree, &root_block, height, nplug);

	if (result)
		return result;

	/* initialize reiser4_super_info_data */
	sbinfo->default_uid = 0;
	sbinfo->default_gid = 0;

	reiser4_set_mkfs_id(s, get_format40_mkfs_id(sb_copy));
	reiser4_set_block_count(s, get_format40_block_count(sb_copy));
	reiser4_set_free_blocks(s, get_format40_free_blocks(sb_copy));

	sbinfo->fsuid = 0;
	/* FIXME-VS: this is should be taken from mount data? */
	sbinfo->trace_flags = 0;
	sbinfo->fs_flags |= (1 << REISER4_ADG);	/* hard links for directories
							 * are not supported */
	sbinfo->fs_flags |= (1 << REISER4_ONE_NODE_PLUGIN);	/* all nodes in
								 * layout 40 are
								 * of one
								 * plugin */

	/* FIXME-VS: maybe this should be dealt with in common code */
	xmemset(&sbinfo->stats, 0, sizeof (reiser4_stat));
	/* sbinfo->tmgr is initialized already */

	/* recover sb data which were logged separately from sb block */
	reiser4_journal_recover_sb_data(s);

	/* number of used blocks */
	reiser4_set_data_blocks(s, get_format40_block_count(sb_copy) - get_format40_free_blocks(sb_copy));

#if REISER4_DEBUG
	sbinfo->min_blocks_used = 
		16 /* reserved area */ + 
		2 /* super blocks */ + 
		2 /* journal footer and header */;
#endif

	/* layout 40 uses bitmap based space allocator - the one implemented in
	   plugin/space/bitmap.[ch] */
	sbinfo->space_plug = space_allocator_plugin_by_id(BITMAP_SPACE_ALLOCATOR_ID);
	assert("vs-493", (sbinfo->space_plug && sbinfo->space_plug->init_allocator));
	/* init disk space allocator */
	result = sbinfo->space_plug->init_allocator(get_space_allocator(s), s, 0);
	if (result)
		return result;

	result = get_super_jnode(s);

	return result;
}

static void
pack_format40_super(const struct super_block *s, char *data)
{
	format40_disk_super_block *super_data = (format40_disk_super_block *) data;
	reiser4_super_info_data *sbinfo = get_super_private(s);

	assert("zam-591", data != NULL);
	assert("zam-598", sbinfo->oid_plug != NULL);
	assert("zam-599", sbinfo->oid_plug->oids_used != NULL);
	assert("zam-600", sbinfo->oid_plug->next_oid != NULL);

	cputod64(reiser4_free_committed_blocks(s), &super_data->free_blocks);
	cputod64(sbinfo->tree.root_block, &super_data->root_block);

	cputod64(sbinfo->oid_plug->next_oid(&sbinfo->oid_allocator), &super_data->oid);
	cputod64(sbinfo->oid_plug->oids_used(&sbinfo->oid_allocator), &super_data->file_count);

	cputod16(sbinfo->tree.height, &super_data->tree_height);
}

/* return a jnode which should be added to transaction when the super block
   gets logged */
jnode *
format40_log_super(struct super_block *s)
{
	jnode *sb_jnode;

	sb_jnode = get_super_private(s)->u.format40.sb_jnode;

	jload(sb_jnode);

	pack_format40_super(s, jdata(sb_jnode));

	jrelse(sb_jnode);

	return sb_jnode;
}

int
format40_release(struct super_block *s)
{
	int ret;
	reiser4_super_info_data *sbinfo;

	sbinfo = get_super_private(s);
	assert("zam-579", sbinfo != NULL);

	/* FIXME-UMKA: Should we tell block transaction manager to commit all if 
	 * we will have no space left? */
	if (reiser4_grab_space(1, BA_RESERVED, "format40_release"))
		return RETERR(-ENOSPC);
	    
	if ((ret = capture_super_block(s))) {
		warning("vs-898", "capture_super_block failed in umount: %d", ret);
	}

	if ((ret = txnmgr_force_commit_all(s))) {
		warning("jmacd-74438", "txn_force failed in umount: %d", ret);
	}

	if (reiser4_is_debugged(s, REISER4_STATS_ON_UMOUNT))
		print_fs_info("umount ok", s);

	/*done_tree(&sbinfo->tree);*/

	assert("zam-580", sbinfo->space_plug != NULL);

	if (sbinfo->space_plug->destroy_allocator != NULL)
		sbinfo->space_plug->destroy_allocator(&sbinfo->space_allocator, s);

	done_journal_info(s);

	eflush_done_at(s);
	done_super_jnode(s);

	return 0;
}

#define FORMAT40_ROOT_LOCALITY 41
#define FORMAT40_ROOT_OBJECTID 42

/* plugin->u.layout.root_dir_key */
const reiser4_key *
format40_root_dir_key(const struct super_block *super UNUSED_ARG)
{
	static const reiser4_key FORMAT40_ROOT_DIR_KEY = {
		.el = {{(FORMAT40_ROOT_LOCALITY << 4) | KEY_SD_MINOR},
		       {FORMAT40_ROOT_OBJECTID}, {0ull}}
	};

	return &FORMAT40_ROOT_DIR_KEY;
}

/* plugin->u.layout.print_info */
void
format40_print_info(const struct super_block *s)
{
#if 0
	format40_disk_super_block *sb_copy;

	sb_copy = &get_super_private(s)->u.format40.actual_sb;

	printk("\tblock count %llu\n"
	       "\tfree blocks %llu\n"
	       "\troot_block %llu\n"
	       "\ttail policy %s\n"
	       "\tmin free oid %llu\n"
	       "\tfile count %llu\n"
	       "\ttree height %d\n",
	       get_format40_block_count(sb_copy),
	       get_format40_free_blocks(sb_copy),
	       get_format40_root_block(sb_copy),
	       tail_plugin_by_id(get_format40_tail_policy(sb_copy))->h.label,
	       get_format40_oid(sb_copy), get_format40_file_count(sb_copy), get_format40_tree_height(sb_copy));
#endif
}

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
