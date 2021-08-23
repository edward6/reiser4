/*
  Copyright (c) 2017-2020 Eduard O. Shishkin

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

#define MIGRATION_GRANULARITY (8192)

int try_merge_with_right_item(coord_t *left);
int try_merge_with_left_item(coord_t *right);
int split_extent_unit(coord_t *coord, reiser4_block_nr pos, int adv_to_right);
int update_item_key(coord_t *target, const reiser4_key *key);

/*
 * primitive migration operations over item
 */
enum migration_primitive_id {
	INVALID_ACTION = 0,
	MIGRATE_EXTENT = 1,
	SKIP_EXTENT = 2
};

struct extent_migrate_context {
	enum migration_primitive_id act;
	struct page **pages;
	int nr_pages;
	coord_t *coord;
	reiser4_key *key; /* key of extent item to be migrated */
	struct inode *inode;
	u32 new_loc;
	loff_t stop_off; /* offset of the leftmost byte not to be processed */
	reiser4_block_nr unit_split_pos; /* position in unit */
	lock_handle *lh;
};

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
static int readpages_extent_unit(struct file_ra_state *ra,
				 const coord_t *coord, reiser4_extent *ext,
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
		if (PageReadahead(page))
			page_cache_async_readahead(mapping, ra, NULL, page,
					 idx + i,
					 item_nr_pages - off_in_pages - i);
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
				 struct extent_migrate_context *mctx)
{
	int i;
	int ret;
	int nr_read = 0;
	reiser4_key key; /* key of the first unit to read from */
	coord_t iter_coord;
	pgoff_t idx;
	struct file_ra_state ra = { 0 };

	coord_dup(&iter_coord, coord);
	iter_coord.unit_pos = 0;
	item_key_by_coord(coord, &key);
	file_ra_state_init(&ra, mapping);

	idx = get_key_offset(&key) >> PAGE_SHIFT;

	while (iter_coord.unit_pos <= coord_last_unit_pos(&iter_coord)) {
		reiser4_extent *ext = extent_by_coord(&iter_coord);

		ret = readpages_extent_unit(&ra, coord, ext, mapping, idx,
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
 * "cut off" a number of unformatted blocks at the end of extent item
 * specified by @coord.
 * @from_off: offset to cut from.
 */
static int cut_off_tail(coord_t *coord, struct inode *inode,
			loff_t from_off)
{
	reiser4_key from, to;
	coord_t from_coord;
	coord_t to_coord;

	coord_dup(&from_coord, coord);

	coord_dup(&to_coord, coord);
	to_coord.between = AT_UNIT;
	to_coord.unit_pos = coord_last_unit_pos(coord);

	item_key_by_coord(coord, &to);
	set_key_offset(&to, get_key_offset(&to) +
		       reiser4_extent_size(coord) - 1);

	from = to;
	set_key_offset(&from, from_off);

	return kill_node_content(&from_coord, &to_coord, &from, &to,
				 NULL, NULL, inode, 0);
}

static int migrate_blocks(struct extent_migrate_context *mctx)
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
	ON_DEBUG(reiser4_key check_key);

	/*
	 * Reserve space on the new data brick.
	 * Balancing procedure is allowed to fail with ENOSPC.
	 */
	grab_space_enable();
	ret = reiser4_grab_space(mctx->nr_pages, 0, new_subv);
	if (ret)
		return ret;

	ON_DEBUG(item_key_by_coord(coord, &check_key));
	assert("edward-2483",
	       get_key_offset(&check_key) == get_key_offset(mctx->key));
	assert("edward-2484", mctx->stop_off ==
	       get_key_offset(&check_key) + reiser4_extent_size(coord));

	ret = readpages_extent_item(coord, mctx->inode->i_mapping, mctx);
	if (ret)
		return ret;

	memcpy(&key, mctx->key, sizeof(key));
	set_key_ordering(&key, mctx->new_loc);

	for (i = 0; i < mctx->nr_pages; i++) {
		struct page *page = mctx->pages[i];
		jnode *node;

		assert("edward-2407", page != NULL);
		assert("edward-2408",
		       page->index ==
		       (get_key_offset(&check_key) >> PAGE_SHIFT) + i);
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
	if (nr_units_extent(coord) > 1) {
		assert("edward-2485", coord_check(coord));
		ret = cut_off_tail(coord, mctx->inode,
				   get_key_offset(mctx->key) +
				   reiser4_extent_size_at(coord, 1));
		if (ret)
			goto error;
		coord->unit_pos = 0;
		assert("edward-2486", coord_check(coord));
	}
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

static int do_migrate_extent(struct extent_migrate_context *mctx)
{
	int ret = 0;
	coord_t *coord = mctx->coord;

	assert("edward-2128",
	       get_key_ordering(mctx->key) != mctx->new_loc);

	ret = zload(coord->node);
	if (ret)
		return ret;

	mctx->nr_pages = reiser4_extent_size(coord) >> PAGE_SHIFT;
	mctx->pages = reiser4_vmalloc(sizeof(mctx->pages) * mctx->nr_pages);
	if (!mctx->pages) {
		zrelse(coord->node);
		return RETERR(-ENOMEM);
	}
	ret = migrate_blocks(mctx);

	vfree(mctx->pages);
	zrelse(coord->node);
	if (ret)
		return ret;
	return 0;
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
 * if @unit_split_pos != 0, then @coord points out to the same unit, which
 * became smaller after split. Otherwise, @coord points out to the preceding
 * unit.
 */
static int split_extent_item(coord_t *coord, reiser4_block_nr unit_split_pos)
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

	assert("edward-2109", znode_is_loaded(coord->node));
	assert("edward-2143", ergo(unit_split_pos == 0, coord->unit_pos > 0));

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
		ret = split_extent_unit(coord, unit_split_pos,
					0 /* stay on the original position */);
		if (ret)
			return ret;
		assert("edward-2110",
		       keyeq(&item_key, item_key_by_coord(coord, &check_key)));
		/*
		 * check if it was the case of item splitting at desired offset
		 * (see the comment above).
		 */
		if (reiser4_extent_size(coord) ==
		    get_key_offset(&split_key) - get_key_offset(&item_key)) {
			/*
			 * item was split at specified offset - nothing to do
			 * any more
			 *
			 * make sure that @coord is valid after cut operation
			 */
			if (unit_split_pos == 0)
				coord->unit_pos --;
			return 0;
		}
		assert("edward-2426", reiser4_extent_size(coord) >
		       get_key_offset(&split_key) - get_key_offset(&item_key));
		/*
		 * unit at @coord decreased, number of units in the item
		 * got incremented
		 */
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
	/* make sure that @coord is valid after cut operation */
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
			      0 /* lh */, COPI_DONT_SHIFT_LEFT);
	kfree(tail_copy);
	return 0;
}

static int do_split_extent(struct extent_migrate_context *mctx)
{
	int ret;
	znode *loaded;

	loaded = mctx->coord->node;
	ret = zload(loaded);
	if (ret)
		return ret;
	assert("edward-2487", coord_check(mctx->coord));
	ret = split_extent_item(mctx->coord, mctx->unit_split_pos);
	assert("edward-2488", coord_check(mctx->coord));
	zrelse(loaded);
	return ret;
}

static void init_migration_context(struct extent_migrate_context *mctx,
				   struct inode *inode, coord_t *coord,
				   reiser4_key *key, lock_handle *lh)
{
	memset(mctx, 0, sizeof(*mctx));
	mctx->coord = coord;
	mctx->key = key;
	mctx->inode = inode;
	mctx->lh = lh;
}

int what_to_do(struct extent_migrate_context *mctx, u64 *dst_id)
{
	loff_t off1, off2;
	loff_t split_off;

	coord_t *coord;
	lookup_result ret;
	struct inode *inode = mctx->inode;
	reiser4_key split_key;

	coord = mctx->coord;
	zload(coord->node);
	item_key_by_coord(coord, mctx->key);
	/*
	 * calculate offsets of leftmost and rightmost bytes
	 * pointed out by the item
	 */
	off1 = get_key_offset(mctx->key);
	off2 = off1 + reiser4_extent_size(coord);
	mctx->new_loc =
		dst_id != NULL ? *dst_id : calc_data_subvol(inode, off1)->id;

	if (mctx->new_loc == get_key_ordering(mctx->key))
		mctx->act = SKIP_EXTENT;
	else
		mctx->act = MIGRATE_EXTENT;
	/*
	 * find split offset in the item, i.e. the smallest offset, so that
	 * data bytes at (offset - 1) and (offset) belong to different
	 * bricks in the logical volume in the new configuration.
	 * The split offset will be the offset of the right item after split
	 */
	split_off = off1;
	while (1) {
		split_off += current_stripe_size;
		if (split_off >= off2)
			break;
		if (calc_data_subvol(inode, split_off)->id != mctx->new_loc)
			goto split_off_found;
		if (mctx->act == MIGRATE_EXTENT &&
		    split_off >= (MIGRATION_GRANULARITY << PAGE_SHIFT)) {
			/*
			 * split offset is not found, but the extent
			 * is too large, so we have to migrate a part
			 * of the item
			 */
			goto split_off_found;
		}
	}
	/*
	 * split offset not found. The whole item is either
	 * to be migrated, or to be skipped
	 */
	mctx->stop_off = off2;
	zrelse(coord->node);
	return 0;
 split_off_found:
	/*
	 * set current position to the found split offset
	 */
	assert("edward-2112", (off1 < split_off) &&
	       (split_off < off1 + reiser4_extent_size(coord)));
	/*
	 * we split at stripe boundary, thus reducing number
	 * of expensive merge operations
	 */
	split_off -= (split_off & (current_stripe_size - 1));
	mctx->stop_off = split_off;

	memcpy(&split_key, mctx->key, sizeof(split_key));
	set_key_offset(&split_key, split_off);
	ret = lookup_extent(&split_key, FIND_EXACT, coord);

	assert("edward-2113", ret == CBK_COORD_FOUND);
	assert("edward-2114", coord->between == AT_UNIT);

	unit_key_by_coord(coord, &split_key);

	assert("edward-2115", get_key_offset(&split_key) <= split_off);
	mctx->unit_split_pos =
		(split_off - get_key_offset(&split_key)) >> PAGE_SHIFT;

	zrelse(coord->node);
	/*
	 * The item to be split, its left part to be skipped or migrated,
	 * and the right part to be processed in the next iteration of
	 * migrate_stripe().
	 */
	return do_split_extent(mctx);
}

int reiser4_migrate_extent(coord_t *coord, lock_handle *lh,
			   struct inode *inode, loff_t *done_off, u64 *dst_id)
{
	int ret = 0;
	reiser4_key key;
	struct extent_migrate_context mctx;

	init_migration_context(&mctx, inode, coord, &key, lh);
	/*
	 * find "stop offset" in the item, split the
	 * item at this offset (if found) and assign
	 * a primitive action over the left part
	 */
	ret = what_to_do(&mctx, dst_id);
	if (ret)
		return ret;
	switch(mctx.act) {
	case SKIP_EXTENT:
		break;
	case MIGRATE_EXTENT:
		/*
		 * maximun number of blocks to be migrated per
		 * iteration is determined by MIGRATION_GRANULARITY
		 */
		ret = do_migrate_extent(&mctx);
		if (ret)
			return ret;
		break;
	default:
		impossible("edward-2489",
			   "Bad migrate action id %d", mctx.act);
	}
	if (get_key_offset(&key) != 0) {
		/* try to merge with already processed item at the left */
		ret = zload(mctx.coord->node);
		if (ret)
			return ret;
		assert("edward-2490", coord_check(mctx.coord));
		try_merge_with_left_item(mctx.coord);
		zrelse(mctx.coord->node);
	}
	*done_off = mctx.stop_off - 1;
	return 0;
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
