/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "forward.h"
#include "tree.h"
#include "tree_walk.h"
#include "super.h"
#include "inode.h"
#include "key.h"
#include "znode.h"

/* for nr_free_pagecache_pages(), totalram_pages */
#include <linux/swap.h>

reiser4_internal void init_ra_info(ra_info_t * rai)
{
	rai->key_to_stop = *min_key();
}

/* global formatted node readahead parameter. It can be set by mount option -o readahead:NUM:1 */
static inline int ra_adjacent_only(int flags)
{
	return flags & RA_ADJACENT_ONLY;
}

/* this is used by formatted_readahead to decide whether read for right neighbor of node is to be issued. It returns 1
   if right neighbor's first key is less or equal to readahead's stop key */
static int
should_readahead_neighbor(znode *node, ra_info_t *info)
{
	return (UNDER_RW(dk, ZJNODE(node)->tree, read,
			 keyle(znode_get_rd_key(node), &info->key_to_stop)));
}

#define LOW_MEM_PERCENTAGE (5)

static int
low_on_memory(void)
{
	unsigned int freepages;

	freepages = nr_free_pagecache_pages();
	return freepages < (totalram_pages * LOW_MEM_PERCENTAGE / 100);
}

/* start read for @node and for a few of its right neighbors */
reiser4_internal void
formatted_readahead(znode *node, ra_info_t *info)
{
	ra_params_t *ra_params;
	znode *cur;
	int i;
	int grn_flags;
	lock_handle next_lh;

	/* do nothing if node block number has not been assigned to node (which means it is still in cache). */
	if (blocknr_is_fake(znode_get_block(node)))
		return;

	ra_params = get_current_super_ra_params();

	if (znode_page(node) == NULL)
		jstartio(ZJNODE(node));

	if (znode_get_level(node) != LEAF_LEVEL)
		return;

	/* don't waste memory for read-ahead when low on memory */
	if (low_on_memory())
		return;

	write_current_tracef("...readahead\n");

	/* We can have locked nodes on upper tree levels, in this situation lock
	   priorities do not help to resolve deadlocks, we have to use TRY_LOCK
	   here. */
	grn_flags = (GN_CAN_USE_UPPER_LEVELS | GN_TRY_LOCK);

	i = 0;
	cur = zref(node);
	init_lh(&next_lh);
	while (i < ra_params->max) {
		const reiser4_block_nr *nextblk;

		if (!should_readahead_neighbor(cur, info))
			break;

		if (reiser4_get_right_neighbor(&next_lh, cur, ZNODE_READ_LOCK, grn_flags))
			break;

		if (JF_ISSET(ZJNODE(next_lh.node), JNODE_EFLUSH)) {
			/* emergency flushed znode is encountered. That means we are low on memory. Do not readahead
			   then */
			break;
		}

		nextblk = znode_get_block(next_lh.node);
		if (blocknr_is_fake(nextblk) ||
		    (ra_adjacent_only(ra_params->flags) && *nextblk != *znode_get_block(cur) + 1)) {
			break;
		}

		zput(cur);
		cur = zref(next_lh.node);
		done_lh(&next_lh);
		if (znode_page(cur) == NULL)
			jstartio(ZJNODE(cur));
		else
			/* Do not scan read-ahead window if pages already
			 * allocated (and i/o already started). */
			break;
		
		i ++;
	}
	zput(cur);
	done_lh(&next_lh);

	write_current_tracef("...readahead exits\n");
}

static inline loff_t get_max_readahead(struct reiser4_file_ra_state *ra)
{
	return VM_MAX_READAHEAD * 1024;
}

static inline loff_t get_min_readahead(struct reiser4_file_ra_state *ra)
{
	return VM_MIN_READAHEAD * 1024;
}


/* start i/o for the given window. */
static int do_reiser4_file_readahead (struct inode * inode, loff_t offset, loff_t size)
{
	reiser4_inode * object;
	reiser4_key start_key;
	reiser4_key stop_key;

	lock_handle lock;
	lock_handle next_lock;

	coord_t coord;
	tap_t tap;

	int ret;

	assert("zam-994", lock_stack_isclean(get_current_lock_stack()));

	object = reiser4_inode_data(inode);
	key_by_inode_unix_file(inode, offset, &start_key);
	key_by_inode_unix_file(inode, offset + size, &stop_key);

	init_lh(&lock);
	init_lh(&next_lock);

	/* stop on twig level */
	ret = coord_by_key(
		current_tree, &start_key, &coord, &lock, ZNODE_WRITE_LOCK, 
		FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL, 0, NULL);
	if (ret < 0)
		goto error;
	if (ret != CBK_COORD_FOUND) {
		ret = 0;
		goto error;
	}

	tap_init(&tap, &coord, &lock, ZNODE_WRITE_LOCK);
	ret = tap_load(&tap);
	if (ret)
		goto error0;

	while (1) {
		reiser4_key key;
		jnode * child;

		if (!item_is_internal(&coord))
			break;

		item_key_by_coord(&coord, &key);
		if (keyge(&key, &stop_key))
			break;

		ret = item_utmost_child(&coord, LEFT_SIDE, &child);
		if (ret || child == NULL)
			break;
		if (IS_ERR(child)) {
			ret = PTR_ERR(child);
			break;
		}

		ret = jstartio(child);
		jput(child);
		if (ret)
			break;

		ret = coord_next_unit(&coord);
		if (ret == 0)
			continue;

		ret = reiser4_get_right_neighbor (
			&next_lock, lock.node, ZNODE_WRITE_LOCK, GN_CAN_USE_UPPER_LEVELS);
		if (ret == -E_NO_NEIGHBOR) {
			ret = 0;
			break;
		}
		if (ret)
			break;
		ret = tap_move(&tap, &next_lock);
		if (ret)
			break;
		done_lh(&next_lock);
		coord_init_first_unit(&coord, lock.node);
	}
 error0:
	tap_done(&tap);
 error:
	done_lh(&lock);
	done_lh(&next_lock);
	return ret;
}

/* This is derived from the linux original read-ahead code (mm/readahead.c), and
 * cannot be licensed from Namesys in its current state.  */
int reiser4_file_readahead (struct file * file, loff_t offset, size_t size)
{
	loff_t min;
	loff_t max;
	loff_t orig_next_size;
	int actual;
	struct reiser4_file_ra_state * ra;
	struct inode * inode = file->f_dentry->d_inode;

	assert ("zam-995", inode != NULL);

	ra = &reiser4_get_file_fsdata(file)->ra;

	if (ra->next_size == -1UL) {
		/* disabled r/a case. */
		goto out;
	}

	max = get_max_readahead(ra);
	if (max == 0)
		goto out;

	min = get_min_readahead(ra);
	orig_next_size = ra->next_size;

	if (ra->next_size == 0 && offset == 0) {
		/*
		 * Special case - first read from first page.
		 * We'll assume it's a whole-file read, and
		 * grow the window fast.
		 */
		ra->next_size = max / 2;
		goto do_io;

	}

	if (offset >= ra->start && offset <= (ra->start + ra->size)) {
		/*
		 * A readahead hit.  Either inside the window, or one
		 * page beyond the end.  Expand the next readahead size.
		 */
		ra->next_size += 2 * size;
	} else {
		/*
		 * A miss - lseek, pagefault, pread, etc.  Shrink the readahead
		 * window.
		 */
		ra->next_size -= 2 * size;
	}

	if (ra->next_size > max)
		ra->next_size = max;
	if (ra->next_size <= 0L) {
		ra->next_size = -1UL;
		ra->size = 0;
		goto out;		/* Readahead is off */
	}

	/*
	 * Is this request outside the current window?
	 */
	if (offset < ra->start || offset >= (ra->start + ra->size)) {
do_io:
		/*
		 * This is the "unusual" path.  We come here during
		 * startup or after an lseek.  We invalidate the
		 * ahead window and get some I/O underway for the new
		 * current window.
		 */
		ra->start = offset;
		ra->size = ra->next_size;
		ra->ahead_start = 0;		/* Invalidate these */
		ra->ahead_size = 0;
		actual = do_reiser4_file_readahead(inode, offset, ra->size);
		// check_ra_success(ra, ra->size, actual, orig_next_size);
	} else {
		/* Have we merely advanced into the ahead window? */
		if (offset + size >= ra->ahead_start) {
			/*
			 * Yes, we have.  The ahead window now becomes
			 * the current window.
			 */
			ra->start = offset + size;
			ra->size = ra->ahead_size;
			ra->ahead_start = 0;
			ra->ahead_size = 0;

			/*
			 * Control now returns, probably to sleep until I/O
			 * completes against the first ahead page.
			 * When the second page in the old ahead window is
			 * requested, control will return here and more I/O
			 * will be submitted to build the new ahead window.
			 */
			goto out;
		}
		/*
		 * This read request is within the current window.  It is time
		 * to submit I/O for the ahead window while the application is
		 * crunching through the current window.
		 */
		if (ra->ahead_start == 0) {
			ra->ahead_start = ra->start + ra->size;
			ra->ahead_size = ra->next_size;
			actual = do_reiser4_file_readahead(
				inode, ra->ahead_start, ra->ahead_size);
			// check_ra_success(ra, ra->ahead_size,actual, orig_next_size);
		}
	}
out:
	return 0;
}


/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
