/* Copyright 2002, 2003 Hans Reiser, licensing governed by reiser4/README */

#include "../../debug.h"
#include "../../dformat.h"
#include "../../key.h"
#include "../node/node.h"
#include "../space/space_allocator.h"
#include "test.h"
#include "../plugin.h"
#include "../../txnmgr.h"
#include "../../tree.h"
#include "../../super.h"
#include "../../wander.h"
#include "../../reiser4.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */

#include <linux/buffer_head.h>	/* Ok */

#if REISER4_DEBUG_OUTPUT
static void print_test_disk_sb(const char *, const test_disk_super_block *);
#else
#define print_test_disk_sb(p,s) noop
#endif

/* plugin->u.format.get_ready */
int
get_ready_test_format(struct super_block *s, void *data UNUSED_ARG)
{
	int result;
	reiser4_key *root_key;
	test_disk_super_block *disk_sb;
	struct buffer_head *super_bh;
	reiser4_super_info_data *sbinfo;
	reiser4_block_nr root_block;
	tree_level height;

	sbinfo = get_super_private(s);
	assert("vs-626", sbinfo);

	super_bh = sb_bread(s, (int) (REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh)
		return RETERR(-EIO);

	disk_sb = (test_disk_super_block *) (super_bh->b_data + sizeof (struct reiser4_master_sb));

	if (strcmp(disk_sb->magic, TEST_MAGIC)) {
		brelse(super_bh);
		return RETERR(-EINVAL);
	}

	/* FIXME-VS: remove this debugging info */
	print_test_disk_sb("get_ready:\n", disk_sb);

	/* store key of root directory in format specific part of
	   reiser4 private super data */
	root_key = &sbinfo->u.test_format.root_dir_key;
	key_init(root_key);
	set_key_locality(root_key, d64tocpu(&disk_sb->root_locality));
	set_key_objectid(root_key, d64tocpu(&disk_sb->root_objectid));
	set_key_type(root_key, KEY_SD_MINOR);
	set_key_offset(root_key, (__u64) 0);

	/* initialize fields of reiser4 private part of super block which
	   are common for all disk formats
	   FIXME-VS: shouldn't that initizlization be in common code? */
	reiser4_set_mkfs_id(s, 0);
	reiser4_set_block_count(s, d64tocpu(&disk_sb->block_count));
	/* number of used blocks */
	reiser4_set_data_blocks(s, d64tocpu(&disk_sb->next_free_block));
	reiser4_set_free_blocks(s, (d64tocpu(&disk_sb->block_count) - d64tocpu(&disk_sb->next_free_block)));
	/* set tail policy plugin */
	sbinfo->plug.t = tail_plugin_by_id(d16tocpu(&disk_sb->tail_policy));

	result = oid_init_allocator(s, d64tocpu(&disk_sb->next_free_oid),
				    d64tocpu(&disk_sb->next_free_oid));
	if (result)
		return result;

	/* init space allocator */
	sbinfo->space_plug = space_allocator_plugin_by_id(TEST_SPACE_ALLOCATOR_ID);
	assert("vs-628", (sbinfo->space_plug && sbinfo->space_plug->init_allocator));
	result = sbinfo->space_plug->init_allocator(get_space_allocator(s), s, &disk_sb->next_free_block);
	if (result) {
		brelse(super_bh);
		return result;
	}

	/* init reiser4_tree for the filesystem */
	root_block = d64tocpu(&disk_sb->root_block);
	height = d16tocpu(&disk_sb->tree_height);
	assert("vs-642", d16tocpu(&disk_sb->node_plugin) == NODE40_ID);

	sbinfo->tree.super = s;
	result = init_tree(&sbinfo->tree, &root_block, height, node_plugin_by_id(NODE40_ID));
	if (result) {
		brelse(super_bh);
		return result;
	}

	brelse(super_bh);
	return result;
}

/* plugin->u.format.root_dir_key */
const reiser4_key *
root_dir_key_test_format(const struct super_block *s)
{
	return &(get_super_private(s)->u.test_format.root_dir_key);
}

/* plugin->u.format.release */
int
release_test_format(struct super_block *s)
{
	struct buffer_head *super_bh;
	test_disk_super_block *disk_sb;
	int ret;

	if ((ret = txnmgr_force_commit_all(s, 1))) {
		warning("jmacd-7711", "txn_force failed in umount: %d", ret);
	}

	/* FIXME-VS: txnmgr_force_commit_all and done_tree cound be
	   called by reiser4_kill_super */
	print_fs_info("umount ok", s);

	done_tree(&get_super_private(s)->tree);

	/* temporary fix, until transaction manager/log writer deals with
	   super-block correctly  */

	super_bh = sb_bread(s, (int) (REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh) {
		warning("vs-630", "could not read super block");
		return RETERR(-EIO);
	}
	disk_sb = (test_disk_super_block *) (super_bh->b_data + sizeof (struct reiser4_master_sb));
	if (strcmp(disk_sb->magic, TEST_MAGIC)) {
		warning("vs-631", "no test format found");
		brelse(super_bh);
		return RETERR(-EIO);
	}

	/* update test disk format super block */
	/* root block */
	cputod64(get_super_private(s)->tree.root_block, &disk_sb->root_block);

	/* tree height */
	cputod16(get_super_private(s)->tree.height, &disk_sb->tree_height);

	/* number of next free block */
	cputod64(get_space_allocator(s)->u.test.new_block_nr, &disk_sb->next_free_block);

	/* next free objectid */
	cputod64(oid_next(s), &disk_sb->next_free_oid);

	/* FIXME-VS: remove this debugging info */
	print_test_disk_sb("release:\n", disk_sb);

	mark_buffer_dirty(super_bh);
	ll_rw_block(WRITE, 1, &super_bh);
	wait_on_buffer(super_bh);
	brelse(super_bh);

	return 0;
}

#if REISER4_DEBUG_OUTPUT
static void
print_test_disk_sb(const char *mes, const test_disk_super_block * disk_sb)
{
	printk("%s", mes);
	printk("root %llu, tree height %u,\n"
	       "block count %llu, next free block %llu,\n"
	       "next free oid %llu\n"
	       "root dir [%llu %llu]\n"
	       "tail policy \"%s\"\n"
	       "node format \"%s\"\n",
	       d64tocpu(&disk_sb->root_block),
	       d16tocpu(&disk_sb->tree_height),
	       d64tocpu(&disk_sb->block_count),
	       d64tocpu(&disk_sb->next_free_block),
	       d64tocpu(&disk_sb->next_free_oid),
	       d64tocpu(&disk_sb->root_locality),
	       d64tocpu(&disk_sb->root_objectid),
	       tail_plugin_by_id(d16tocpu(&disk_sb->tail_policy))->h.label,
	       node_plugin_by_id(d16tocpu(&disk_sb->node_plugin))->h.label);
}

#endif

/* plugin->u.format.print_info */
void
print_info_test_format(const struct super_block *s UNUSED_ARG)
{
	/* there is nothing to print */
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
