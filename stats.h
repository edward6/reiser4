/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Statistics gathering. */
  
#if !defined( __FS_REISER4_STATS_H__ )
#define __FS_REISER4_STATS_H__

#include "forward.h"
#include "reiser4.h"
#include "debug.h"

#ifdef __KERNEL__
/* for __u?? types */
#include <linux/types.h>
/* for struct super_block, etc */
#include <linux/fs.h>
/* for in_interrupt() */
#include <asm/hardirq.h>
#endif

#include <linux/sched.h>
#include <linux/percpu_counter.h>

#if REISER4_STATS

/* following macros update counters from &reiser4_stat below, which
   see */

#define ON_STATS(e) e
/* statistics gathering features. */

#define REISER4_STATS_STRICT (0)

/* type of statistics counters */
typedef struct percpu_counter stat_cnt;

typedef struct reiser4_level_statistics {
	/* carries restarted due to deadlock avoidance algorithm */
	stat_cnt carry_restart;
	/* carries performed */
	stat_cnt carry_done;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it already in a carry set. */
	stat_cnt carry_left_in_carry;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it already in a memory. */
	stat_cnt carry_left_in_cache;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it is not in a memory. */
	stat_cnt carry_left_missed;
	/* how many times carry, trying to find left neighbor of a given node,
	   found that left neighbor either doesn't exist (we are at the left
	   border of the tree already), or that there is extent on the left.
	*/
	stat_cnt carry_left_not_avail;
	/* how many times carry, trying to find left neighbor of a given node,
	   gave this up to avoid deadlock */
	stat_cnt carry_left_refuse;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it already in a carry set. */
	stat_cnt carry_right_in_carry;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it already in a memory. */
	stat_cnt carry_right_in_cache;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it is not in a memory. */
	stat_cnt carry_right_missed;
	/* how many times carry, trying to find right neighbor of a given
	   node, found that right neighbor either doesn't exist (we are at the
	   right border of the tree already), or that there is extent on the
	   right.
	*/
	stat_cnt carry_right_not_avail;
	/* how many times insertion has to look into the left neighbor,
	   searching for the free space. */
	stat_cnt insert_looking_left;
	/* how many times insertion has to look into the right neighbor,
	   searching for the free space. */
	stat_cnt insert_looking_right;
	/* how many times insertion has to allocate new node, searching for
	   the free space. */
	stat_cnt insert_alloc_new;
	/* how many times insertion has to allocate several new nodes in a
	   row, searching for the free space. */
	stat_cnt insert_alloc_many;
	/* how many insertions were performed by carry. */
	stat_cnt insert;
	/* how many deletions were performed by carry. */
	stat_cnt delete;
	/* how many cuts were performed by carry. */
	stat_cnt cut;
	/* how many pastes (insertions into existing items) were performed by
	   carry. */
	stat_cnt paste;
	/* how many extent insertions were done by carry. */
	stat_cnt extent;
	/* how many paste operations were restarted as insert. */
	stat_cnt paste_restarted;
	/* how many updates of delimiting keys were performed by carry. */
	stat_cnt update;
	/* how many times carry notified parent node about updates in its
	   child. */
	stat_cnt modify;
	/* how many times node was found reparented at the time when its
	   parent has to be updated. */
	stat_cnt half_split_race;
	/* how many times new node was inserted into sibling list after
	   concurrent balancing modified right delimiting key if its left
	   neighbor.
	*/
	stat_cnt dk_vs_create_race;
	/* how many times insert or paste ultimately went into node different
	   from original target */
	stat_cnt track_lh;
	/* how many times sibling lookup required getting that high in a
	   tree */
	stat_cnt sibling_search;
	/* key was moved out of node while thread was waiting for the lock */
	stat_cnt cbk_key_moved;
	/* node was moved out of tree while thread was waiting for the lock */
	stat_cnt cbk_met_ghost;
	/* for how many pages on this level ->releasepage() was called. */
	stat_cnt page_try_release;
	/* how many pages were released on this level */
	stat_cnt page_released;
	/* how many times emergency flush was invoked on this level */
	stat_cnt emergency_flush;
	struct {
		/* calls to jload() */
		stat_cnt jload;
		/* calls to jload() that found jnode already loaded */
		stat_cnt jload_already;
		/* calls to jload() that found page already in memory */
		stat_cnt jload_page;
		/* calls to jload() that found jnode with asynchronous io
		 * started */
		stat_cnt jload_async;
		/* calls to jload() that actually had to read data */
		stat_cnt jload_read;
		/* calls to jput() */
		stat_cnt jput;
		/* calls to jput() that released last reference */
		stat_cnt jputlast;
	} jnode;
	struct {
		/* calls to lock_znode() */
		stat_cnt lock;
		/* number of times loop inside lock_znode() was executed */
		stat_cnt lock_iteration;
		/* calls to lock_neighbor() */
		stat_cnt lock_neighbor;
		/* number of times loop inside lock_neighbor() was executed */
		stat_cnt lock_neighbor_iteration;
		stat_cnt lock_read;
		stat_cnt lock_write;
		stat_cnt lock_lopri;
		stat_cnt lock_hipri;
		/* how many requests for znode long term lock couldn't succeed
		 * immediately. */
		stat_cnt lock_contented;
		/* how many requests for znode long term lock managed to
		 * succeed immediately. */
		stat_cnt lock_uncontented;
		stat_cnt unlock;
		stat_cnt wakeup;
		stat_cnt wakeup_found;
		stat_cnt wakeup_found_read;
		stat_cnt wakeup_scan;
		stat_cnt wakeup_convoy;
	} znode;
	struct {
		struct {
			stat_cnt calls;
			stat_cnt items;
			stat_cnt binary;
			stat_cnt seq;
			stat_cnt found;
			stat_cnt pos;
			stat_cnt posrelative;
			stat_cnt samepos;
		} lookup;
	} node;
	stat_cnt total_hits_at_level;
	stat_cnt time_slept;
} reiser4_level_stat;

typedef struct tshash_stat {
	stat_cnt lookup;
	stat_cnt insert;
	stat_cnt remove;
	stat_cnt scanned;
} tshash_stat;

#define TSHASH_LOOKUP(stat) ({ if(stat) percpu_counter_inc(&stat->lookup); })
#define TSHASH_INSERT(stat) ({ if(stat) percpu_counter_inc(&stat->insert); })
#define TSHASH_REMOVE(stat) ({ if(stat) percpu_counter_inc(&stat->remove); })
#define TSHASH_SCANNED(stat) ({ if(stat) percpu_counter_inc(&stat->scanned); })

/* set of statistics counter. This is embedded into super-block when
   REISER4_STATS is on. */
typedef struct reiser4_statistics {
	struct {
		/* calls to coord_by_key */
		stat_cnt cbk;
		/* calls to coord_by_key that found requested key */
		stat_cnt cbk_found;
		/* calls to coord_by_key that didn't find requested key */
		stat_cnt cbk_notfound;
		/* number of times calls to coord_by_key restarted */
		stat_cnt cbk_restart;
		/* calls to coord_by_key that found key in coord cache */
		stat_cnt cbk_cache_hit;
		/* calls to coord_by_key that didn't find key in coord
		   cache */
		stat_cnt cbk_cache_miss;
		/* cbk cache search found wrong node */
		stat_cnt cbk_cache_wrong_node;
		/* search for key in coord cache raced against parallel
		   balancing and lose. This should be rare. If not,
		   update cbk_cache_search() according to comment
		   therewithin.
		*/
		stat_cnt cbk_cache_race;
		/* number of times coord of child in its parent, cached
		   in a former, was reused. */
		stat_cnt pos_in_parent_hit;
		/* number of time binary search for child position in
		   its parent had to be redone. */
		stat_cnt pos_in_parent_miss;
		/* number of times position of child in its parent was
		   cached in the former */
		stat_cnt pos_in_parent_set;
		/* how many times carry() was skipped by doing "fast
		   insertion path". See
		   fs/reiser4/plugin/node/node.h:->fast_insert() method.
		*/
		stat_cnt fast_insert;
		/* how many times carry() was skipped by doing "fast
		   paste path". See
		   fs/reiser4/plugin/node/node.h:->fast_paste() method.
		*/
		stat_cnt fast_paste;
		/* how many times carry() was skipped by doing "fast
		   cut path". See
		   fs/reiser4/plugin/node/node.h:->cut_insert() method.
		*/
		stat_cnt fast_cut;
		/* children reparented due to shifts at the parent level */
		stat_cnt reparenting;
		/* right delimiting key is not exact */
		stat_cnt rd_key_skew;
		/* how many times lookup_multikey() has to restart from the
		   beginning because of the broken seal. */
		stat_cnt multikey_restart;
		stat_cnt check_left_nonuniq;
		stat_cnt left_nonuniq_found;
	} tree;
	reiser4_level_stat level[REAL_MAX_ZTREE_HEIGHT];
	struct {
		stat_cnt lookup;
		stat_cnt create;
		stat_cnt mkdir;
		stat_cnt symlink;
		stat_cnt mknod;
		stat_cnt rename;
		stat_cnt readlink;
		stat_cnt follow_link;
		stat_cnt setattr;
		stat_cnt getattr;
		stat_cnt read;
		stat_cnt write;
		stat_cnt truncate;
		stat_cnt statfs;
		stat_cnt bmap;
		stat_cnt link;
		stat_cnt llseek;
		stat_cnt readdir;
		stat_cnt ioctl;
		stat_cnt mmap;
		stat_cnt unlink;
		stat_cnt rmdir;
		stat_cnt alloc_inode;
		stat_cnt destroy_inode;
		stat_cnt delete_inode;
		stat_cnt write_super;
		stat_cnt private_data_alloc; /* allocations of either per struct dentry or per struct file data */
	} vfs_calls;
	struct {
		struct {
			stat_cnt calls;
			stat_cnt reset;
			stat_cnt rewind_left;
			stat_cnt left_non_uniq;
			stat_cnt left_restart;
			stat_cnt rewind_right;
			stat_cnt adjust_pos;
			stat_cnt adjust_lt;
			stat_cnt adjust_gt;
			stat_cnt adjust_eq;
		} readdir;
	} dir;

	/* statistics of unix file plugin */
	struct {
		
		struct {
			stat_cnt readpage_calls;
			stat_cnt writepage_calls;
		} page_ops;

		/* number of tail conversions */
		stat_cnt tail2extent;
		stat_cnt extent2tail;

		/* find_next_item statistic */
		stat_cnt find_file_item;
		stat_cnt find_file_item_via_seal;
		stat_cnt find_file_item_via_right_neighbor;
		stat_cnt find_file_item_via_cbk;
		
	} file;
	struct {
		/* how many unformatted nodes were read */
		stat_cnt unfm_block_reads;

		/* extent_write seals and unlock znode before locking/capturing
		   page which is to be modified. After page is locked/captured
		   it validates a seal. Number of found broken seals is stored
		   here
		*/
		stat_cnt broken_seals;

		/* extent_write calls balance_dirty_pages after it modifies every page. Before that it seals node it
		   currently holds and uses seal_validate to lock it again. This field stores how many times
		   balance_dirty_pages broke that seal and caused to repease search tree traversal
		*/
		stat_cnt bdp_caused_repeats;
		/* how many times extent_write could not write a coord and had to ask for research */
		stat_cnt repeats;
	} extent;
	struct { /* stats on tail items */		
		/* tail_write calls balance_dirty_pages after every call to insert_flow. Before that it seals node it
		   currently holds and uses seal_validate to lock it again. This field stores how many times
		   balance_dirty_pages broke that seal and caused to repease search tree traversal
		*/
		stat_cnt bdp_caused_repeats;
	} tail;
	struct {
		/* jiffies, spent in atom_wait_event() */
		stat_cnt slept_in_wait_event;
		/* jiffies, spent in capture_fuse_wait (wait for atom state change) */
		stat_cnt slept_in_wait_atom;
		/* number of commits */
		stat_cnt commits;
		/*number of post commit writes */
		stat_cnt post_commit_writes;
		/* jiffies, spent in commits and post commit writes */
		stat_cnt time_spent_in_commits;
		stat_cnt raced_with_truncate;
		stat_cnt empty_bio;
		stat_cnt commit_from_writepage;

		stat_cnt capture_equal;
		stat_cnt capture_both;
		stat_cnt capture_block;
		stat_cnt capture_txnh;
		stat_cnt capture_none;
	} txnmgr;
	struct {
		/* how many nodes were squeezed to left neighbor completely */
		stat_cnt squeezed_completely;
		/* how many times nodes with unallocated children are written */
		stat_cnt flushed_with_unallocated;
		/* how many leaves were squeezed to left */
		stat_cnt squeezed_leaves;
		/* how many items were squeezed on leaf level */
		stat_cnt squeezed_leaf_items;
		/* how mnay bytes were squeezed on leaf level */
		stat_cnt squeezed_leaf_bytes;
		/* how many times jnode_flush was called */
		stat_cnt flush;
		/* how many nodes were scanned by scan_left() */
		stat_cnt left;
		/* how many nodes were scanned by scan_right() */
		stat_cnt right;
		/* an overhead of MTFLUSH semaphore */
		stat_cnt slept_in_mtflush_sem;
	} flush;
	struct {
		/* how many carry objects were allocated */
		stat_cnt alloc;
		/* how many "extra" carry objects were allocated by
		   kmalloc. */
		stat_cnt kmalloc;
	} pool;
	struct {
		/* seals that were found pristine */
		stat_cnt perfect_match;
		/* how many times key drifted from sealed node */
		stat_cnt key_drift;
		/* how many times node under seal was out of cache */
		stat_cnt out_of_cache;
		/* how many times wrong node was found under seal */
		stat_cnt wrong_node;
		/* how many times coord was found in exactly the same position
		   under seal */
		stat_cnt didnt_move;
		/* how many times key was actually found under seal */
		stat_cnt found;
	} seal;
	struct {
		tshash_stat znode;
		tshash_stat zfake;
		tshash_stat jnode;
		tshash_stat lnode;
		tshash_stat eflush;
	} hashes;
	struct {
		stat_cnt asked;
		stat_cnt iteration;
		stat_cnt wait_flush;
		stat_cnt wait_congested;
		stat_cnt kicked;
		stat_cnt cleaned;
		stat_cnt skipped_ent;
		stat_cnt skipped_last;
		stat_cnt skipped_congested;
		stat_cnt low_priority;
		stat_cnt removed;
		stat_cnt toolong;
	} wff;
	/* how many non-unique keys were scanned into tree */
	stat_cnt non_uniq;

	/* page_common_writeback stats */
	stat_cnt pcwb_calls;
	stat_cnt pcwb_formatted;
	stat_cnt pcwb_unformatted;
	stat_cnt pcwb_no_jnode;
	stat_cnt pcwb_ented;
	stat_cnt pcwb_written;
	stat_cnt pcwb_not_written;

	stat_cnt pages_dirty;
	stat_cnt pages_clean;
} reiser4_stat;

#define get_current_stat() 					\
	(get_super_private_nocheck(reiser4_get_current_sb())->stats)

/* Macros to gather statistical data. If REISER4_STATS is disabled, they
   are preprocessed to nothing.
*/

#define	reiser4_stat(sb, cnt) (&get_super_private_nocheck(sb)->stats->cnt)

#define	reiser4_stat_inc_at(sb, counter)					\
	percpu_counter_inc(&get_super_private_nocheck(sb)->stats->counter)

#define	reiser4_stat_inc(counter)				\
	percpu_counter_inc(&get_current_stat()->counter)

#define reiser4_stat_add(counter, delta) 			\
	percpu_counter_mod(&get_current_stat()->counter, delta)

#define	reiser4_stat_inc_at_level(lev, stat)					\
({										\
	int __level;								\
										\
	__level = (lev) - LEAF_LEVEL;						\
	if (__level >= 0) {							\
		if(__level < REAL_MAX_ZTREE_HEIGHT) {				\
			reiser4_stat_inc(level[__level]. stat);			\
			reiser4_stat_inc(level[__level]. total_hits_at_level);	\
		}								\
	}									\
})

#define	reiser4_stat_add_at_level(lev, stat, value)				\
({										\
	int level;								\
										\
	level = (lev) - LEAF_LEVEL;						\
	if (level >= 0) {							\
		if(level < REAL_MAX_ZTREE_HEIGHT) {				\
			reiser4_stat_add(level[level]. stat , value );		\
			reiser4_stat_inc(level[level]. total_hits_at_level);	\
		}								\
	}									\
})

#define	reiser4_stat_level_inc(l, stat)			\
	reiser4_stat_inc_at_level((l)->level_no, stat)


struct kobject;
extern int reiser4_populate_kattr_level_dir(struct kobject * kobj);
extern int reiser4_stat_init(reiser4_stat ** stats);
extern void reiser4_stat_done(reiser4_stat ** stats);

/* REISER4_STATS */
#else

#define ON_STATS(e) noop

#define	reiser4_stat(sb, cnt) ((void *)NULL)
#define	reiser4_stat_inc(counter)  noop
#define reiser4_stat_add(counter, delta) noop

#define	reiser4_stat_inc_at(sb, counter) noop
#define	reiser4_stat_inc_at_level(lev, stat) noop
#define reiser4_stat_add_at_level(lev, stat, cnt) noop
#define	reiser4_stat_level_inc(l, stat) noop
#define reiser4_stat_stack_check_max(gap) noop

typedef struct {
} reiser4_stat;

typedef struct tshash_stat {
} tshash_stat;

#define TSHASH_LOOKUP(stat) noop
#define TSHASH_INSERT(stat) noop
#define TSHASH_REMOVE(stat) noop
#define TSHASH_SCANNED(stat) noop

#define reiser4_populate_kattr_level_dir(kobj, i) (0)
#define reiser4_stat_init(s) (0)
#define reiser4_stat_done(s) noop

#endif

extern int reiser4_populate_kattr_dir(struct kobject * kobj);


/* __FS_REISER4_STATS_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
