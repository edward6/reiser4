/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Statistical facilities. */

#include "kattr.h"
#include "reiser4.h"
#include "stats.h"
#include "statcnt.h"
#include "super.h"
#include "debug.h"

#include <linux/sysfs.h>
#include <linux/vmalloc.h>


typedef struct reiser4_stats_cnt {
	reiser4_kattr  kattr;
	ptrdiff_t      offset;
	size_t         size;
	const char    *format;
} reiser4_stats_cnt;

#define getptrat(type, ptr, offset) ((type *)(((char *)(ptr)) + (offset)))
#define getat(type, ptr, offset) (*getptrat(type, ptr, offset))

#define DEFINE_STATCNT_0(aname, afield, atype, afmt, ashow, astore)	\
{							\
	.kattr = {					\
		.attr = {				\
			.name = (char *)aname,		\
			.mode = 0666 /* rw-rw-rw- */	\
		},					\
		.cookie = 0,				\
		.show   = ashow,			\
		.store  = astore			\
	},						\
	.format = afmt "\n",				\
	.offset = offsetof(atype, afield),		\
	.size   = sizeof(((atype *)0)->afield)		\
}

#if REISER4_STATS

static ssize_t
show_stat_attr(struct super_block * s, reiser4_kattr * kattr,
	       void * opaque, char * buf)
{
	char *p;
	reiser4_stats_cnt *cnt;
	statcnt_t *val;

	(void)opaque;

	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getptrat(statcnt_t, get_super_private(s)->stats, cnt->offset);
	p = buf;
	KATTR_PRINT(p, buf, cnt->format, statcnt_get(val));
	return (p - buf);
}

static ssize_t
store_stat_attr(struct super_block * s, reiser4_kattr * kattr,
		void *opaq UNUSED_ARG, const char * buf UNUSED_ARG, size_t size)
{
	reiser4_stats_cnt *cnt;
	statcnt_t *val;

	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getptrat(statcnt_t, get_super_private(s)->stats, cnt->offset);
	statcnt_reset(val);
	return size;
}

static ssize_t
show_stat_level_attr(struct super_block * s, reiser4_kattr * kattr,
		     void *da, char * buf)
{
	char *p;
	reiser4_stats_cnt *cnt;
	statcnt_t *val;
	int level;

	level = *(int *)da;
	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getptrat(statcnt_t, &get_super_private(s)->stats->level[level],
		       cnt->offset);
	p = buf;
	KATTR_PRINT(p, buf, cnt->format, statcnt_get(val));
	return (p - buf);
}

static ssize_t
store_stat_level_attr(struct super_block * s, reiser4_kattr * kattr,
		      void *da, const char * buf UNUSED_ARG, size_t size)
{
	reiser4_stats_cnt *cnt;
	statcnt_t *val;
	int level;

	level = *(int *)da;
	cnt = container_of(kattr, reiser4_stats_cnt, kattr);
	val = getptrat(statcnt_t, &get_super_private(s)->stats->level[level],
		       cnt->offset);
	statcnt_reset(val);
	return size;
}

#define DEFINE_STATCNT(field)					\
	DEFINE_STATCNT_0(#field, field, reiser4_stat, "%lu", 	\
			 show_stat_attr, store_stat_attr)

reiser4_stats_cnt reiser4_stat_defs[] = {
	DEFINE_STATCNT(tree.cbk),
	DEFINE_STATCNT(tree.cbk_found),
	DEFINE_STATCNT(tree.cbk_notfound),
	DEFINE_STATCNT(tree.cbk_restart),
	DEFINE_STATCNT(tree.cbk_cache_hit),
	DEFINE_STATCNT(tree.cbk_cache_miss),
	DEFINE_STATCNT(tree.cbk_cache_wrong_node),
	DEFINE_STATCNT(tree.cbk_cache_race),
	DEFINE_STATCNT(tree.object_lookup_novroot),
	DEFINE_STATCNT(tree.object_lookup_moved),
	DEFINE_STATCNT(tree.object_lookup_outside),
	DEFINE_STATCNT(tree.object_lookup_cannotlock),
	DEFINE_STATCNT(tree.object_lookup_restart),
	DEFINE_STATCNT(tree.pos_in_parent_hit),
	DEFINE_STATCNT(tree.pos_in_parent_miss),
	DEFINE_STATCNT(tree.pos_in_parent_set),
	DEFINE_STATCNT(tree.fast_insert),
	DEFINE_STATCNT(tree.fast_paste),
	DEFINE_STATCNT(tree.fast_cut),
	DEFINE_STATCNT(tree.reparenting),
	DEFINE_STATCNT(tree.rd_key_skew),
	DEFINE_STATCNT(tree.check_left_nonuniq),
	DEFINE_STATCNT(tree.left_nonuniq_found),

	DEFINE_STATCNT(vfs_calls.open),
	DEFINE_STATCNT(vfs_calls.lookup),
	DEFINE_STATCNT(vfs_calls.create),
	DEFINE_STATCNT(vfs_calls.mkdir),
	DEFINE_STATCNT(vfs_calls.symlink),
	DEFINE_STATCNT(vfs_calls.mknod),
	DEFINE_STATCNT(vfs_calls.rename),
	DEFINE_STATCNT(vfs_calls.readlink),
	DEFINE_STATCNT(vfs_calls.follow_link),
	DEFINE_STATCNT(vfs_calls.setattr),
	DEFINE_STATCNT(vfs_calls.getattr),
	DEFINE_STATCNT(vfs_calls.read),
	DEFINE_STATCNT(vfs_calls.write),
	DEFINE_STATCNT(vfs_calls.truncate),
	DEFINE_STATCNT(vfs_calls.statfs),
	DEFINE_STATCNT(vfs_calls.bmap),
	DEFINE_STATCNT(vfs_calls.link),
	DEFINE_STATCNT(vfs_calls.llseek),
	DEFINE_STATCNT(vfs_calls.readdir),
	DEFINE_STATCNT(vfs_calls.ioctl),
	DEFINE_STATCNT(vfs_calls.mmap),
	DEFINE_STATCNT(vfs_calls.unlink),
	DEFINE_STATCNT(vfs_calls.rmdir),
	DEFINE_STATCNT(vfs_calls.alloc_inode),
	DEFINE_STATCNT(vfs_calls.destroy_inode),
	DEFINE_STATCNT(vfs_calls.delete_inode),
	DEFINE_STATCNT(vfs_calls.write_super),
	DEFINE_STATCNT(vfs_calls.private_data_alloc),

	DEFINE_STATCNT(dir.readdir.calls),
	DEFINE_STATCNT(dir.readdir.reset),
	DEFINE_STATCNT(dir.readdir.rewind_left),
	DEFINE_STATCNT(dir.readdir.left_non_uniq),
	DEFINE_STATCNT(dir.readdir.left_restart),
	DEFINE_STATCNT(dir.readdir.rewind_right),
	DEFINE_STATCNT(dir.readdir.adjust_pos),
	DEFINE_STATCNT(dir.readdir.adjust_lt),
	DEFINE_STATCNT(dir.readdir.adjust_gt),
	DEFINE_STATCNT(dir.readdir.adjust_eq),

	DEFINE_STATCNT(file.page_ops.readpage_calls),
	DEFINE_STATCNT(file.page_ops.writepage_calls),
	DEFINE_STATCNT(file.tail2extent),
	DEFINE_STATCNT(file.extent2tail),
	DEFINE_STATCNT(file.find_file_item),
	DEFINE_STATCNT(file.find_file_item_via_seal),
	DEFINE_STATCNT(file.find_file_item_via_right_neighbor),
	DEFINE_STATCNT(file.find_file_item_via_cbk),

	DEFINE_STATCNT(extent.unfm_block_reads),
	DEFINE_STATCNT(extent.broken_seals),
	DEFINE_STATCNT(extent.bdp_caused_repeats),

	DEFINE_STATCNT(tail.bdp_caused_repeats),

	DEFINE_STATCNT(txnmgr.slept_in_wait_atom),
	DEFINE_STATCNT(txnmgr.slept_in_wait_event),
	DEFINE_STATCNT(txnmgr.commits),
	DEFINE_STATCNT(txnmgr.post_commit_writes),
	DEFINE_STATCNT(txnmgr.time_spent_in_commits),
	DEFINE_STATCNT(txnmgr.raced_with_truncate),
	DEFINE_STATCNT(txnmgr.empty_bio),
	DEFINE_STATCNT(txnmgr.commit_from_writepage),
	DEFINE_STATCNT(txnmgr.capture_equal),
	DEFINE_STATCNT(txnmgr.capture_both),
	DEFINE_STATCNT(txnmgr.capture_block),
	DEFINE_STATCNT(txnmgr.capture_txnh),
	DEFINE_STATCNT(txnmgr.capture_none),
	DEFINE_STATCNT(txnmgr.restart.atom_begin),
	DEFINE_STATCNT(txnmgr.restart.cannot_commit),
	DEFINE_STATCNT(txnmgr.restart.should_wait),
	DEFINE_STATCNT(txnmgr.restart.flush),
	DEFINE_STATCNT(txnmgr.restart.fuse_lock_owners_fused),
	DEFINE_STATCNT(txnmgr.restart.fuse_lock_owners),
	DEFINE_STATCNT(txnmgr.restart.trylock_throttle),
	DEFINE_STATCNT(txnmgr.restart.assign_block),
	DEFINE_STATCNT(txnmgr.restart.assign_txnh),
	DEFINE_STATCNT(txnmgr.restart.fuse_wait_nonblock),
	DEFINE_STATCNT(txnmgr.restart.fuse_wait_slept),
	DEFINE_STATCNT(txnmgr.restart.init_fusion_atomf),
	DEFINE_STATCNT(txnmgr.restart.init_fusion_atomh),
	DEFINE_STATCNT(txnmgr.restart.init_fusion_fused),

	DEFINE_STATCNT(flush.squeezed_completely),
	DEFINE_STATCNT(flush.flushed_with_unallocated),
	DEFINE_STATCNT(flush.squeezed_leaves),
	DEFINE_STATCNT(flush.squeezed_leaf_items),
	DEFINE_STATCNT(flush.squeezed_leaf_bytes),
	DEFINE_STATCNT(flush.flush),
	DEFINE_STATCNT(flush.left),
	DEFINE_STATCNT(flush.right),
	DEFINE_STATCNT(flush.slept_in_mtflush_sem),

	DEFINE_STATCNT(pool.alloc),
	DEFINE_STATCNT(pool.kmalloc),

	DEFINE_STATCNT(seal.perfect_match),
	DEFINE_STATCNT(seal.out_of_cache),

	DEFINE_STATCNT(hashes.znode.lookup),
	DEFINE_STATCNT(hashes.znode.insert),
	DEFINE_STATCNT(hashes.znode.remove),
	DEFINE_STATCNT(hashes.znode.scanned),
	DEFINE_STATCNT(hashes.zfake.lookup),
	DEFINE_STATCNT(hashes.zfake.insert),
	DEFINE_STATCNT(hashes.zfake.remove),
	DEFINE_STATCNT(hashes.zfake.scanned),
	DEFINE_STATCNT(hashes.jnode.lookup),
	DEFINE_STATCNT(hashes.jnode.insert),
	DEFINE_STATCNT(hashes.jnode.remove),
	DEFINE_STATCNT(hashes.jnode.scanned),
	DEFINE_STATCNT(hashes.lnode.lookup),
	DEFINE_STATCNT(hashes.lnode.insert),
	DEFINE_STATCNT(hashes.lnode.remove),
	DEFINE_STATCNT(hashes.lnode.scanned),
	DEFINE_STATCNT(hashes.eflush.lookup),
	DEFINE_STATCNT(hashes.eflush.insert),
	DEFINE_STATCNT(hashes.eflush.remove),
	DEFINE_STATCNT(hashes.eflush.scanned),

	/* wff - wait for flush */
	DEFINE_STATCNT(wff.asked),
	DEFINE_STATCNT(wff.iteration),
	DEFINE_STATCNT(wff.wait_flush),
	DEFINE_STATCNT(wff.kicked),
	DEFINE_STATCNT(wff.cleaned),
	DEFINE_STATCNT(wff.skipped_ent),
	DEFINE_STATCNT(wff.skipped_last),
	DEFINE_STATCNT(wff.skipped_congested),
	DEFINE_STATCNT(wff.low_priority),
	DEFINE_STATCNT(wff.removed),
	DEFINE_STATCNT(wff.toolong),

	DEFINE_STATCNT(block_alloc.nohint),

	DEFINE_STATCNT(non_uniq),

	/* pcwb - page common write back */
	DEFINE_STATCNT(pcwb.calls),
	DEFINE_STATCNT(pcwb.formatted),
	DEFINE_STATCNT(pcwb.unformatted),
	DEFINE_STATCNT(pcwb.no_jnode),
	DEFINE_STATCNT(pcwb.ented),
	DEFINE_STATCNT(pcwb.not_written),
	DEFINE_STATCNT(pcwb.written),

	/* coc - copy on capture */
	DEFINE_STATCNT(coc.calls),
 	DEFINE_STATCNT(coc.ok_uber), /* copy on capture performed for super block */
 	DEFINE_STATCNT(coc.ok_clean), /* copy on capture performed for clean node */
	DEFINE_STATCNT(coc.ok_ovrwr), /* copy on capture performed for block from overwrite set */
 	DEFINE_STATCNT(coc.ok_reloc), /* copy on capture performed for block from relocate set */
	DEFINE_STATCNT(coc.forbidden),
	DEFINE_STATCNT(coc.reloc_writeback_dirty_fq),
	DEFINE_STATCNT(coc.scan_race),
	DEFINE_STATCNT(coc.atom_changed),
	DEFINE_STATCNT(coc.nopage_eflush)
};

#define DEFINE_STAT_LEVEL_CNT(field)					\
	DEFINE_STATCNT_0(#field, field,					\
			 reiser4_level_stat, "%lu", 			\
			 show_stat_level_attr, store_stat_level_attr)

reiser4_stats_cnt reiser4_stat_level_defs[] = {
	DEFINE_STAT_LEVEL_CNT(carry_restart),
	DEFINE_STAT_LEVEL_CNT(carry_done),
	DEFINE_STAT_LEVEL_CNT(carry_left_in_carry),
	DEFINE_STAT_LEVEL_CNT(carry_left_in_cache),
	DEFINE_STAT_LEVEL_CNT(carry_left_missed),
	DEFINE_STAT_LEVEL_CNT(carry_left_not_avail),
	DEFINE_STAT_LEVEL_CNT(carry_left_refuse),
	DEFINE_STAT_LEVEL_CNT(carry_right_in_carry),
	DEFINE_STAT_LEVEL_CNT(carry_right_in_cache),
	DEFINE_STAT_LEVEL_CNT(carry_right_missed),
	DEFINE_STAT_LEVEL_CNT(carry_right_not_avail),
	DEFINE_STAT_LEVEL_CNT(insert_looking_left),
	DEFINE_STAT_LEVEL_CNT(insert_looking_right),
	DEFINE_STAT_LEVEL_CNT(insert_alloc_new),
	DEFINE_STAT_LEVEL_CNT(insert_alloc_many),
	DEFINE_STAT_LEVEL_CNT(insert),
	DEFINE_STAT_LEVEL_CNT(delete),
	DEFINE_STAT_LEVEL_CNT(cut),
	DEFINE_STAT_LEVEL_CNT(paste),
	DEFINE_STAT_LEVEL_CNT(extent),
	DEFINE_STAT_LEVEL_CNT(paste_restarted),
	DEFINE_STAT_LEVEL_CNT(update),
	DEFINE_STAT_LEVEL_CNT(modify),
	DEFINE_STAT_LEVEL_CNT(half_split_race),
	DEFINE_STAT_LEVEL_CNT(dk_vs_create_race),
	DEFINE_STAT_LEVEL_CNT(track_lh),
	DEFINE_STAT_LEVEL_CNT(sibling_search),
	DEFINE_STAT_LEVEL_CNT(cbk_key_moved),
	DEFINE_STAT_LEVEL_CNT(cbk_met_ghost),
	DEFINE_STAT_LEVEL_CNT(page_try_release),
	DEFINE_STAT_LEVEL_CNT(page_released),
	DEFINE_STAT_LEVEL_CNT(emergency_flush),
	DEFINE_STAT_LEVEL_CNT(object_lookup_start),

	DEFINE_STAT_LEVEL_CNT(jnode.jload),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_already),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_page),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_async),
	DEFINE_STAT_LEVEL_CNT(jnode.jload_read),
	DEFINE_STAT_LEVEL_CNT(jnode.jput),
	DEFINE_STAT_LEVEL_CNT(jnode.jputlast),

	DEFINE_STAT_LEVEL_CNT(znode.lock),
	DEFINE_STAT_LEVEL_CNT(znode.lock_iteration),
	DEFINE_STAT_LEVEL_CNT(znode.lock_neighbor),
	DEFINE_STAT_LEVEL_CNT(znode.lock_neighbor_iteration),
	DEFINE_STAT_LEVEL_CNT(znode.lock_read),
	DEFINE_STAT_LEVEL_CNT(znode.lock_write),
	DEFINE_STAT_LEVEL_CNT(znode.lock_lopri),
	DEFINE_STAT_LEVEL_CNT(znode.lock_hipri),
	DEFINE_STAT_LEVEL_CNT(znode.lock_contented),
	DEFINE_STAT_LEVEL_CNT(znode.lock_uncontented),
	DEFINE_STAT_LEVEL_CNT(znode.lock_dying),
	DEFINE_STAT_LEVEL_CNT(znode.lock_cannot_lock),
	DEFINE_STAT_LEVEL_CNT(znode.lock_can_lock),
	DEFINE_STAT_LEVEL_CNT(znode.lock_no_capture),
	DEFINE_STAT_LEVEL_CNT(znode.unlock),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_found),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_found_read),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_scan),
	DEFINE_STAT_LEVEL_CNT(znode.wakeup_convoy),
	DEFINE_STAT_LEVEL_CNT(node.lookup.calls),
	DEFINE_STAT_LEVEL_CNT(node.lookup.items),
	DEFINE_STAT_LEVEL_CNT(node.lookup.binary),
	DEFINE_STAT_LEVEL_CNT(node.lookup.seq),
	DEFINE_STAT_LEVEL_CNT(node.lookup.found),
	DEFINE_STAT_LEVEL_CNT(node.lookup.pos),
	DEFINE_STAT_LEVEL_CNT(node.lookup.posrelative),
	DEFINE_STAT_LEVEL_CNT(node.lookup.samepos),
	DEFINE_STAT_LEVEL_CNT(node.lookup.nextpos),

	DEFINE_STAT_LEVEL_CNT(time_slept),
	DEFINE_STAT_LEVEL_CNT(total_hits_at_level)
};

static void
print_cnt(reiser4_stats_cnt * cnt, const char * prefix, void * base)
{
	printk("%s%s:\t ", prefix, cnt->kattr.attr.name);
	printk(cnt->format,
	       statcnt_get(getptrat(statcnt_t, base, cnt->offset)));
}

/* Print statistical data accumulated so far. */
void
reiser4_print_stats()
{
	reiser4_stat *s;
	int i;

	s = get_current_super_private()->stats;
	for(i = 0 ; i < sizeof_array(reiser4_stat_defs) ; ++ i)
		print_cnt(&reiser4_stat_defs[i], "", s);

	for (i = 0; i < REAL_MAX_ZTREE_HEIGHT; ++i) {
		int j;

		if (statcnt_get(&s->level[i].total_hits_at_level) <= 0)
			continue;
		printk("tree: at level: %i\n", i +  LEAF_LEVEL);
		for(j = 0 ; j < sizeof_array(reiser4_stat_level_defs) ; ++ j)
			print_cnt(&reiser4_stat_level_defs[j], "\t", &s->level[i]);
	}
}

int
reiser4_populate_kattr_level_dir(struct kobject * kobj)
{
	int result;
	int i;

	result = 0;
	for(i = 0 ; i < sizeof_array(reiser4_stat_level_defs) && !result ; ++ i)
		result = sysfs_create_file(kobj,
					   &reiser4_stat_level_defs[i].kattr.attr);
	if (result != 0)
		warning("nikita-2921", "Failed to add sysfs level attr: %i, %i",
			result, i);
	return result;
}

int reiser4_stat_init(reiser4_stat ** storehere)
{
	reiser4_stat *stats;
	statcnt_t *cnt;
	int num, i;

	cassert((sizeof *stats) / (sizeof *cnt) * (sizeof *cnt) == (sizeof *stats));

	stats = vmalloc_32(sizeof *stats);
	if (stats == NULL)
		return -ENOMEM;

	*storehere = stats;

	num = (sizeof *stats) / (sizeof *cnt);
	cnt = (statcnt_t *)stats;
	for (i = 0; i < num ; ++i, ++cnt)
		statcnt_init(cnt);
	return 0;
}

void reiser4_stat_done(reiser4_stat ** stats)
{
	vfree(*stats);
	*stats = NULL;
}

#else
void
reiser4_print_stats()
{
}
#endif

int
reiser4_populate_kattr_dir(struct kobject * kobj UNUSED_ARG)
{
	int result;
	int i;

	result = 0;
#if REISER4_STATS
	for(i = 0 ; i < sizeof_array(reiser4_stat_defs) && !result ; ++ i)
		result = sysfs_create_file(kobj,
					  &reiser4_stat_defs[i].kattr.attr);

#endif
	if (result != 0)
		warning("nikita-2920", "Failed to add sysfs attr: %i, %i",
			result, i);
	return result;
}


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
