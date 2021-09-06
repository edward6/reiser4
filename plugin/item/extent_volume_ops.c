/*
  Copyright (c) 2017-2021 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../object.h"
#include "../volume/volume.h"

int try_merge_with_right_item(coord_t *left);
int try_merge_with_left_item(coord_t *right);
int split_extent_unit(coord_t *coord, reiser4_block_nr pos, int adv_to_right);
int update_item_key(coord_t *target, const reiser4_key *key);
void reset_migration_context(struct migration_context *mctx);

struct extent_ra_ctx {
	const coord_t *coord;
	reiser4_extent *ext;
	reiser4_block_nr off;
};

/**
 * read page pointed out by extent item
 */
static int filler(void *data, struct page *page)
{
	struct extent_ra_ctx *ra_ctx = data;

	return __reiser4_readpage_extent(ra_ctx->coord, ra_ctx->ext,
					 ra_ctx->off, page);
}

/**
 * read all pages pointed out by extent unit @ext starting from @off
 * @idx: index of the first page pointed out by extent unit
 */
static int readpages_extent_unit(const coord_t *coord, reiser4_extent *ext,
				 struct address_space *mapping, pgoff_t idx,
				 struct page **pages, int item_nr_pages,
				 int off_in_pages)
{
	int i;
	int ret;
	struct extent_ra_ctx ra_ctx;
	int unit_nr_pages = extent_get_width(ext);

	ra_ctx.coord = coord;
	ra_ctx.ext = ext;
	ra_ctx.off = 0;

	for (i = 0; i < unit_nr_pages; i++) {
		struct page *page;
		page = read_cache_page(mapping, idx + i, filler, &ra_ctx);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			unit_nr_pages = i;
			goto error;
		}
		pages[i + off_in_pages] = page;
		ra_ctx.off ++;
	}
	return 0;
 error:
	for (i = 0; i < unit_nr_pages; i++)
		put_page(pages[i + off_in_pages]);
	return ret;
}

/**
 * read and pin pages pointed out by extent item at @coord
 */
static int readpages_extent_item(const coord_t *coord,
				 struct address_space *mapping,
				 struct migration_context *mctx)
{
	int i;
	int ret;
	int nr_read = 0;
	reiser4_key key; /* key of the first unit to read from */
	coord_t iter_coord;
	pgoff_t idx;

	coord_dup(&iter_coord, coord);
	iter_coord.unit_pos = 0;
	item_key_by_coord(coord, &key);

	idx = get_key_offset(&key) >> PAGE_SHIFT;

	while (iter_coord.unit_pos <= coord_last_unit_pos(&iter_coord)) {
		reiser4_extent *ext = extent_by_coord(&iter_coord);

		ret = readpages_extent_unit(coord, ext, mapping, idx,
					    mctx->pages, mctx->nr_pages,
					    nr_read);
		if (ret)
			goto error;

		nr_read += extent_get_width(ext);
		idx += extent_get_width(ext);
		iter_coord.unit_pos += 1;
	}
	assert("edward-2405", nr_read == mctx->nr_pages);
	return 0;
 error:
	for (i = 0; i < nr_read; i++)
		put_page(mctx->pages[i]);
	return ret;
}

/**
 * "cut off" all units except the first one from the item specified by @coord
 */
static int cut_off_tail(coord_t *coord, struct inode *inode)
{
	coord_t from_coord;
	coord_t to_coord;

	assert("edward-2485", coord->between == AT_UNIT);

	if (nr_units_extent(coord) == 1)
		return 0;

	coord_dup(&from_coord, coord);
	coord_dup(&to_coord, coord);

	from_coord.unit_pos = 1;
	to_coord.unit_pos = coord_last_unit_pos(coord);

	return kill_node_content(&from_coord, &to_coord, NULL, NULL,
				 NULL, NULL, inode, 0);
}

/**
 * Deallocate all logical blocks pointed out by the item at mctx->coord,
 * make them dirty and put them to a transaction
 */
static int migrate_blocks(struct migration_context *mctx)
{
	int i;
	int ret;
	reiser4_key key;
	reiser4_extent *ext;
	reiser4_block_nr block;
	int nr_jnodes = 0;
	coord_t *coord = mctx->coord;
	reiser4_subvol *new_subv = current_origin(mctx->new_loc);
	struct atom_brick_info *abi;

	mctx->nr_pages = reiser4_extent_size(mctx->coord) >> PAGE_SHIFT;
	/*
	 * Reserve space on the new data brick.
	 * Balancing procedure is allowed to fail with ENOSPC.
	 */
	grab_space_enable();
	ret = reiser4_grab_space(mctx->nr_pages, 0, new_subv);
	if (ret)
		return ret;

	item_key_by_coord(coord, &key);
	assert("edward-2128", get_key_ordering(&key) != mctx->new_loc);
	assert("edward-2484", mctx->stop_off ==
	       get_key_offset(&key) + reiser4_extent_size(coord));

	ret = readpages_extent_item(coord, mctx->inode->i_mapping, mctx);
	if (ret)
		return ret;

	set_key_ordering(&key, mctx->new_loc);

	for (i = 0; i < mctx->nr_pages; i++) {
		struct page *page = mctx->pages[i];
		jnode *node;

		assert("edward-2407", page != NULL);
		assert("edward-2408",
		       page->index ==
		       (get_key_offset(&key) >> PAGE_SHIFT) + i);
		lock_page(page);
		node = jnode_of_page(page);
		if (IS_ERR(node)) {
			nr_jnodes = i;
			unlock_page(page);
			ret = PTR_ERR(node);
			goto error;
		}
		JF_SET(node, JNODE_WRITE_PREPARED);
		unlock_page(page);

	}
	nr_jnodes = mctx->nr_pages;
	/*
	 * cut all units except the first one;
	 * deallocate all blocks, pointed out by that first unit;
	 * set that unit as unallocated extent of proper width;
	 * update item's key to point out to the new brick;
	 */
	ret = cut_off_tail(coord, mctx->inode);
	if (ret)
		goto error;
	coord->unit_pos = 0;
	assert("edward-2486", coord_check(coord));
	assert("edward-2516", nr_units_extent(coord) == 1);

	ext = extent_by_coord(coord);
	if (state_of_extent(ext) == ALLOCATED_EXTENT) {
		reiser4_block_nr start = extent_get_start(ext);
		reiser4_block_nr len = extent_get_width(ext);

		reiser4_dealloc_blocks(&start,
				       &len,
				       0, BA_DEFER,
				       find_data_subvol(coord));
	}
	reiser4_set_extent(new_subv, ext,
			   UNALLOCATED_EXTENT_START, nr_jnodes);
	ret = update_item_key(coord, &key);
	/*
	 * Capture jnodes, set new addresses for them,
	 * and make them dirty. At flush time all the
	 * blocks will get new location on the new brick.
	 */
	ret = check_insert_atom_brick_info(new_subv->id, &abi);
	if (ret)
		goto error;
	block = fake_blocknr_unformatted(mctx->nr_pages, new_subv);

	for (i = 0; i < mctx->nr_pages; i++, block++) {
		jnode *node = jprivate(mctx->pages[i]);

		assert("edward-2417", node != NULL);

		set_page_dirty_notag(mctx->pages[i]);

		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);
		JF_CLR(node, JNODE_WRITE_PREPARED);

		node->subvol = new_subv;
		jnode_set_block(node, &block);

		ret = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(ret != 0);

		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);

		jput(node);
		put_page(mctx->pages[i]);
	}
	return 0;
 error:
	for (i = 0; i < mctx->nr_pages; i++) {
		if (i < nr_jnodes)
			jput(jprivate(mctx->pages[i]));
		put_page(mctx->pages[i]);
	}
	return ret;
}

/**
 * Create a new extent item right after the item specified by @coord
 * and move the tail part of the last one to that newly created item. It can
 * involve carry, if there is no free space on the node. Subtle!
 *
 * @unit_split_pos: splitting position in the unit.
 * The pair @coord and @unit_split_pos defines splitting position in the item.
 * If @unit_split_pos != 0, then the unit at @coord will be split at
 * @unit_split_pos offset and its right part will start the new item.
 * Otherwise, we'll split at the unit boundary and the unit at @coord will be
 * moved to the head of the new item.
 *
 * Upon successfull completion:
 * if @lh != NULL, then @coord is the first unit of the right part of the
 * split item. Otherwise, it is the last unit of the left part.
 */
static int __split_extent_item(coord_t *coord, reiser4_block_nr unit_split_pos,
			     lock_handle *lh)
{
	int ret;
	coord_t cut_from;
	coord_t cut_to;
	char *tail_copy;
	char *tail_orig;
	int tail_num_units;
	int tail_len;
	reiser4_item_data idata;
	reiser4_key split_key;
	reiser4_key item_key;
	ON_DEBUG(reiser4_key check_key);
	znode *orig_node;

	assert("edward-2109", znode_is_loaded(coord->node));
	assert("edward-2143", ergo(unit_split_pos == 0, coord->unit_pos > 0));

	orig_node = coord->node;
	memset(&idata, 0, sizeof(idata));
	item_key_by_coord(coord, &item_key);
	unit_key_by_coord(coord, &split_key);
	set_key_offset(&split_key,
		       get_key_offset(&split_key) +
		       (unit_split_pos << current_blocksize_bits));

	if (unit_split_pos != 0) {
		/*
		 * start from splitting the unit.
		 * NOTE: it may change the item @coord (specifically, split
		 * it and move its part to the right neighbor
		 */
		ret = split_extent_unit(coord, unit_split_pos, lh ? 1 : 0);
		if (ret)
			return ret;
		/*
		 * check if it was the case of item splitting at desired offset
		 * (see the comment above).
		 */
		if (lh && orig_node != lh->node) {
			/*
			 * item was split at specified offset,
			 * its parts are on different neighboring nodes,
			 * and are at the right one
			 */
			assert("edward-2503", coord->node == lh->node);
			assert("edward-2504",
			       WITH_DATA(lh->node,
					 keyeq(&split_key,
					       unit_key_by_coord(coord,
								 &check_key))));
			return 0;

		} else if (!lh &&
			   (reiser4_extent_size(coord) ==
			    get_key_offset(&split_key) -
			    get_key_offset(&item_key))) {
			/*
			 * item was split at specified offset,
			 * its parts are on different neighboring nodes,
			 */
			return 0;
		}
		/*
		 * The unit got split in the same item/node.
		 * The number of units of the item got incremented
		 */
		assert("edward-2505", ergo(lh, orig_node == lh->node));
		assert("edward-2110",
		       keyeq(&item_key, item_key_by_coord(coord, &check_key)));
		assert("edward-2426", reiser4_extent_size(coord) >
		       get_key_offset(&split_key) - get_key_offset(&item_key));

		/* move to the left part of the unit that was split */
		if (lh)
			coord->unit_pos--;
		tail_orig =
			node_plugin_by_node(coord->node)->item_by_coord(coord) +
			(coord->unit_pos + 1) * sizeof(reiser4_extent);
		tail_num_units = coord_num_units(coord) - coord->unit_pos - 1;
	} else {
		/*
		 * none of the units is subjected to splitting -
		 * we'll split the item at units boundary.
		 */
		tail_orig =
			node_plugin_by_node(coord->node)->item_by_coord(coord) +
			coord->unit_pos * sizeof(reiser4_extent);
		tail_num_units = coord_num_units(coord) - coord->unit_pos;
	}
	assert("edward-2427", tail_num_units > 0);

	tail_len = tail_num_units * sizeof(reiser4_extent);

	tail_copy = kmalloc(tail_len, reiser4_ctx_gfp_mask_get());
	if (!tail_copy)
		return -ENOMEM;
	memcpy(tail_copy, tail_orig, tail_len);
	/*
	 * cut off the tail from the original item
	 */
	coord_dup(&cut_from, coord);
	if (unit_split_pos)
		/* the original unit was split */
		cut_from.unit_pos ++;
	coord_dup(&cut_to, coord);
	cut_to.unit_pos = coord_num_units(coord) - 1;
	/*
	 * cut the original tail
	 */
	cut_node_content(&cut_from, &cut_to, NULL, NULL, NULL);
	/* make sure that @coord is valid after split operation */
	if (unit_split_pos == 0)
		coord->unit_pos --;

	assert("edward-2428",
	       get_key_offset(item_key_by_coord(coord, &check_key)) +
	       reiser4_extent_size(coord) == get_key_offset(&split_key));
	/*
	 * finally, create a new item
	 */
	init_new_extent(item_id_by_coord(&cut_from),
			&idata, tail_copy, tail_num_units);
	coord_init_after_item(&cut_from);

	ret = insert_by_coord(&cut_from, &idata, &split_key,
			      lh, COPI_DONT_SHIFT_LEFT);
	if (lh)
		coord_dup_nocheck(coord, &cut_from);
	kfree(tail_copy);
	return 0;
}

/**
 * Split at @offset the extent item pointed out by @coord
 *
 * Pre-condition:
 * @offset is an offset in bytes in the file body
 * @key is the key of the item to split
 *
 * Post-condition on success:
 * The item is split into two ones.
 * @key is the key of the right part, formed as a result of the split
 * operation. If @lh is not NULL, then @coord is the leftmost unit
 * of the right part. Othewise, @coord is the rightmost unit of the
 * left part
 */
static int split_extent_item(coord_t *coord, reiser4_key *key, loff_t offset,
			     lock_handle *lh)
{
	reiser4_key unit_key;
	int ret;

	set_key_offset(key, offset);
	ret = lookup_extent(key, FIND_EXACT, coord);
	if (ret != CBK_COORD_FOUND) {
		warning("edward-2506", "Corrupted extent pointer. FSCK?");
		return -EIO;
	}
	unit_key_by_coord(coord, &unit_key);
	return __split_extent_item(coord,
		       (offset - get_key_offset(&unit_key)) >> PAGE_SHIFT,
				 lh);
}

static void init_migration_context(struct migration_context *mctx,
				   struct inode *inode, coord_t *coord,
				   reiser4_key *key, lock_handle *lh)
{
	reset_migration_context(mctx);

	mctx->coord = coord;
	mctx->key = key;
	mctx->inode = inode;
	mctx->lh = lh;
}

static void done_migration_context(struct migration_context *mctx)
{
	done_load_count(&mctx->dh);
}

/**
 * Prepare extent item at mctx->coord for migration. If the item is
 * "semi-migrated", then split it at the "done" offset and go to the
 * unprocessed part. For the last one find "stop offset" within it,
 * split the item at that offset (if found) and assign a primitive
 * action over the left part. The right part will be processed in the
 * next iteration of migrate_stripe()
 */
int what_to_do(struct migration_context *mctx, u64 *dst_id,
	       loff_t start_off)
{
	loff_t off1, off2;
	loff_t split_off;

	ON_DEBUG(reiser4_key check_key);
	struct inode *inode = mctx->inode;
	coord_t *coord = mctx->coord;
	load_count *dh = &mctx->dh;
	lookup_result ret;
	int augment;

	ret = incr_load_count_znode(dh, coord->node);
	if (ret)
		return ret;
	item_key_by_coord(coord, mctx->key);
	if (start_off > get_key_offset(mctx->key)) {
		/*
		 * At the time when the longterm lock was released
		 * the rightmost migrated item got merged with the
		 * leftmost not yet migrated one.
		 *
		 * Split that "semi-migrated" item at the @start_off
		 * and go to the beginning of the right part to resume
		 * migration from it.
		 */
		ret = split_extent_item(coord, mctx->key, start_off,
					mctx->lh);
		if (ret)
			return ret;
		assert("edward-2507", mctx->lh->node == coord->node);
		/*
		 * We could move to different node. Load it.
		 */
		ret = check_load_count_new(dh, coord->node);
		if (ret)
			return ret;
	}
	/* Here @coord is the beginning of the item to be migrated */
	assert("edward-2508", keyeq(mctx->key,
				    item_key_by_coord(coord, &check_key)));
	/*
	 * calculate offsets of leftmost and rightmost bytes
	 * pointed out by the item
	 */
	off1 = get_key_offset(mctx->key);
	off2 = off1 + reiser4_extent_size(coord);
	mctx->new_loc =
		dst_id != NULL ? *dst_id : calc_data_subvol(inode, off1)->id;

	if (mctx->new_loc == get_key_ordering(mctx->key))
		mctx->act = SKIP_ITEM;
	else
		mctx->act = MIGRATE_ITEM;
	/*
	 * Find split offset in the item, i.e. the smallest offset, so that
	 * data bytes at (offset - 1) and (offset) belong to different
	 * bricks in the logical volume with the new configuration.
	 * The split offset will be the offset of the right item after the
	 * split operation
	 */
	split_off = off1;
	augment = current_stripe_size - (off1 & (current_stripe_size  - 1));
	while (1) {
		split_off += augment;
		if (split_off >= off2)
			/* not found */
			break;
		if (calc_data_subvol(inode, split_off)->id != mctx->new_loc)
			goto split_off_found;
		if (mctx->act == MIGRATE_ITEM &&
		    split_off - off1 >= MIGRATION_GRANULARITY) {
			/*
			 * split offset is not found, but the extent
			 * is too large, so we have to migrate a part
			 * of the item
			 */
			goto split_off_found;
		}
		augment = current_stripe_size;
	}
	/*
	 * split offset not found. The whole item is either to be
	 * migrated, or to be skipped
	 */
	mctx->stop_off = off2;
	return 0;
 split_off_found:
	assert("edward-2112", (off1 < split_off) &&
	       (split_off < off1 + reiser4_extent_size(coord)));

	mctx->stop_off = split_off;
	return split_extent_item(coord, mctx->key, split_off, NULL);
}

int reiser4_migrate_extent(void *data, coord_t *coord, lock_handle *lh,
			   struct inode *inode, unsigned int *migrated,
			   loff_t start_off, loff_t *done_off, u64 *dst_id)
{
	int ret = 0;
	reiser4_key key;
	struct migration_context *mctx = data;

	init_migration_context(mctx, inode, coord, &key, lh);
	ret = what_to_do(mctx, dst_id, start_off);
	if (ret)
		goto out;
	switch(mctx->act) {
	case MIGRATE_ITEM:
		ret = migrate_blocks(mctx);
		if (ret)
			goto out;
		break;
	case SKIP_ITEM:
		break;
	default:
		impossible("edward-2489",
			   "Bad migration action id %d", mctx->act);
	}
	if (start_off) {
		/*
		 * try to merge with already processed
		 * item at the left
		 */
		assert("edward-2490", coord_check(mctx->coord));
		try_merge_with_left_item(mctx->coord);
	}
	*done_off = mctx->stop_off - 1;
	*migrated = mctx->nr_pages;
 out:
	done_migration_context(mctx);
	return ret;
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 80
 * scroll-step: 1
 * End:
 */
