/*
  Copyright (c) 2017-2019 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../object.h"

/*
 * primitive migration operations over item
 */
enum migration_primitive_id {
	INVALID_ACTION = 0,
	MIGRATE_EXTENT = 1,
	SPLIT_EXTENT = 2,
	SKIP_EXTENT = 3
};

struct extent_migrate_context {
	enum migration_primitive_id act;
	coord_t *coord;
	reiser4_key key; /* key of extent item to be migrated */
	struct inode *inode;
	u32 new_loc;
	loff_t stop_off; /* offset of the leftmost byte to be migrated
			    in the iteration */
	loff_t done_off; /* offset of the latest byte migrated in the
			    iteration */
	reiser4_block_nr blocks_migrated;
	reiser4_block_nr split_pos; /* position in unit */
	lock_handle *lh;
	unsigned int migrate_whole_item:1;
	unsigned int stop:1;
};

int split_extent_unit(coord_t *coord, reiser4_block_nr pos, int adv_to_right);
/*
 * read the last page pointed out by extent item
 */
static int filler(void *data, struct page *page)
{
	reiser4_extent *ext;
	coord_t *coord = data;
#if REISER4_DEBUG
	reiser4_key key;
	item_key_by_coord(coord, &key);
	assert("edward-2122",
	       (get_key_offset(&key) +
		reiser4_extent_size(coord)) >> PAGE_SHIFT ==
	       page->index + 1);
#endif
	ext = extent_by_coord(coord);
	return __reiser4_readpage_extent(coord, ext,
					 extent_get_width(ext) - 1, page);
}

/**
 * "cut off" one unformatted block at the end of extent item
 * specified by @coord
 */
static int cut_off_tail_block(coord_t *coord, struct inode *inode)
{
	reiser4_key from, to;
	coord_t tail;
	/*
	 * construct a coord which points out to the tail block
	 */
	coord_dup(&tail, coord);
	tail.between = AT_UNIT;
	tail.unit_pos = coord_last_unit_pos(coord);

	item_key_by_coord(coord, &to);
	set_key_offset(&to, get_key_offset(&to) +
		       reiser4_extent_size(coord) - 1);
	from = to;
	set_key_offset(&from, get_key_offset(&to) - (current_blocksize - 1));

	return kill_node_content(&tail, &tail, &from, &to,
				 NULL, NULL, inode, 0);
}

static int reserve_migrate_one_block(struct inode *inode)
{
	reiser4_subvol *subv_m = get_meta_subvol();
	reiser4_tree *tree_m = &subv_m->tree;
	/*
	 * to migrate one page of a striped file we have to reserve
	 * disk space for:
	 *
	 * 1. find_file_item may have to insert empty node to the tree (empty
	 * leaf node between two extent items). This requires:
	 * (a) 1 block for that leaf node;
	 * (b) number of blocks which are necessary to perform insertion of an
	 * internal item into twig level.
	 * 2. for each of written pages there might be needed:
	 * (a) 1 block for the page itself
	 * (b) number of blocks which might be necessary to perform insertion
	 * of or paste to an extent item.
	 * 3. stat data update
	 *
	 * space for 2(a) will be reserved by update_extents_stripe()
	 */
	/*
	 * reserve space for 1, 2(b), 3
	 */
	grab_space_enable();
	return reiser4_grab_space(estimate_one_insert_item(tree_m) +
				  1 * estimate_one_insert_into_item(tree_m) +
				  estimate_one_insert_item(tree_m), 0, subv_m);
}

jnode *do_jget(struct page *pg);

/**
 * Relocate rightmost block pointed out by an extent item at
 * @mctx.coord to a new brick @mctx.new_loc. During migration
 * we remove an old pointer to that block from the tree and
 * insert a new one in another place determined by the new brick id.
 *
 * Pre-condition: position in the tree is locked at @mctx.coord
 * Post-condition: no longterm locks are held.
 */
static int migrate_one_block(struct extent_migrate_context *mctx)
{
	int ret;
	coord_t *coord;
	reiser4_key key;
	struct page *page;
	pgoff_t index;
	struct inode *inode;
	jnode *node;
	/*
	 * read the rightmost page pointed out by the extent pointer
	 */
	inode = mctx->inode;
	coord = mctx->coord;

	item_key_by_coord(coord, &key);

	assert("edward-2123", get_key_objectid(&key) == get_inode_oid(inode));

	index = (get_key_offset(&key) + reiser4_extent_size(coord) -
		 current_blocksize) >> current_blocksize_bits;

	ret = reserve_migrate_one_block(inode);
	if (ret)
		return ret;
	/*
	 * FIXME-EDWARD: read-ahead is our everything!!!!!!!!
	 */
	page = read_cache_page(inode->i_mapping, index, filler, coord);
	if (IS_ERR(page))
		return PTR_ERR(page);
	lock_page(page);
	node = do_jget(page);
	if (IS_ERR(node)) {
		unlock_page(page);
		return PTR_ERR(node);
	}
	set_page_dirty_notag(page);
	JF_SET(node, JNODE_WRITE_PREPARED);
	unlock_page(page);

#if REISER4_DEBUG
	assert("edward-2124", index == page->index);
	assert("edward-2125",
	       node == jfind(inode->i_mapping, index));
	jput(node);
	assert("edward-2126",
	       node == jlookup(get_inode_oid(inode), page->index));
	jput(node);
#endif
	ret = cut_off_tail_block(coord, inode);
	done_lh(mctx->lh);
	if (ret)
		goto out;
	/*
	 * at this point our block became orphan and unallocated -
	 * deallocation happened at kill_hook_extent().
	 * Here we release all resources associated with jnode (except
	 * detaching jnode's page) and assign a new brick.
	 */
	spin_lock_jnode(node);
	node->blocknr = 0;
	node->subvol = current_origin(mctx->new_loc);
	reiser4_uncapture_jnode(node); /* this unlocks jnode */
	/*
	 * create a pointer to the orphan unallocated unformatted
	 * block at the new location. It will be performed as hole
	 * plugging operation, see plug_hole_stripe() - our jnode
	 * will be captured and made dirty. At flush time our block
	 * will get new location on the new brick.
	 */
	assert("edward-2127",
	       node->subvol == calc_data_subvol(inode, page_offset(page)));

	ret = update_extent_stripe(inode, node, NULL, 0);
	if (ret)
		warning("edward-1897",
			"Failed to migrate block %lu of inode %llu (%d)",
			index, (unsigned long long)get_inode_oid(inode),
			ret);
	JF_CLR(node, JNODE_WRITE_PREPARED);
 out:
	jput(node);
	put_page(page);
	return ret;
}

/**
 * relocate an extent item, or its right part of @mctx.size to another
 * brick @mctx.new_loc.
 * Pre-condition: position in the tree is write-locked
 */
static int do_migrate_extent(struct extent_migrate_context *mctx)
{
	int ret = 0;
	lock_handle *lh = mctx->lh;
	coord_t *coord = mctx->coord;
	assert("edward-2106", coord->node == lh->node);
	assert("edward-2128",
	       get_key_ordering(&mctx->key) != mctx->new_loc);

	while (1) {
		znode *loaded;
		loff_t done_off;

		ret = zload(coord->node);
		if (ret)
			break;
		loaded = coord->node;
		done_off = get_key_offset(&mctx->key) +
			reiser4_extent_size(coord) - PAGE_SIZE;
		/*
		 * this will release the longterm lock
		 */
		ret = migrate_one_block(mctx);
		zrelse(loaded);
		if (ret)
			break;
		mctx->done_off = done_off;
		mctx->blocks_migrated ++;

		drop_exclusive_access(unix_file_inode_data(mctx->inode));
		reiser4_throttle_write(mctx->inode);
		get_exclusive_access(unix_file_inode_data(mctx->inode));

		if (done_off == get_key_offset(&mctx->key) ||
		    (mctx->migrate_whole_item && done_off == mctx->stop_off)) {
			/*
			 * no more blocks to be migrated in this item,
			 * or migration plans scheduled by what_to_do()
			 * has been fulfilled
			 */
			mctx->stop = 1;
			break;
		}
		assert("edward-2350", done_off != 0);
		/*
		 * go to the rightmost not migrated block
		 */
		set_key_offset(&mctx->key, done_off - 1);
		ret = find_file_item_nohint(coord, lh, &mctx->key,
					    ZNODE_WRITE_LOCK, mctx->inode);
		if (ret) {
			/*
			 * item not found (killed by concurrent
			 * truncate, or error happened)
			 */
			warning("edward-2318",
				"Item not found after migration (%d)", ret);
			done_lh(lh);
			if (!IS_CBKERR(ret)) {
				ret = 0;
				mctx->stop = 1;
			}
			break;
		}
		/*
		 * reset @mctx->key, as the item could be changed
		 * while we had keeping the lock released.
		 */
		ret = zload(coord->node);
		if (ret)
			break;
		item_key_by_coord(coord, &mctx->key);
		zrelse(coord->node);

		if (done_off == mctx->stop_off)
			/*
			 * extent has been partially migrated,
			 * we need also to process the rest of
			 * it in the next iteration starting
			 * from what_to_do()
			 */
			break;
	};
	return ret;
}

/*
 * Reserve space on a meta-data brick for split_extent operation
 */
static int reserve_split_extent_item(void)
{
	reiser4_subvol *subv_m = get_meta_subvol();

	grab_space_enable();
	/*
	 * a new item will be created during split
	 */
	return reiser4_grab_space(estimate_one_insert_item(&subv_m->tree),
				  0, subv_m);
}

/**
 * Create a new extent item right after the item specified by @mctx.coord
 * and move a tail part of the last one to that newly created item. It can
 * involve carry, if there is no free space on the node.
 *
 * The pair (@mctx.coord and @mctx.split_pos) defines "split position" in
 * the following sense. If @mctx.split_pos != 0, then unit at @mctx.coord
 * will be split at @mctx.split_pos offset and its right part will become
 * the first unit in the new item. Otherwise, that unit will become the
 * first unit in the new item.
 *
 * Upon successfull completion @mctx.coord points out to the same, or
 * preceding unit.
 */
static int split_extent_item(struct extent_migrate_context *mctx)
{
	int ret;
	coord_t *coord;
	coord_t cut_from;
	coord_t cut_to;
	char *tail_copy;
	char *tail_orig;
	int tail_num_units;
	int tail_len;
	reiser4_item_data idata;
	reiser4_key split_key;
	reiser4_key item_key;

	coord = mctx->coord;
	assert("edward-2109", znode_is_loaded(coord->node));
	assert("edward-2143", ergo(mctx->split_pos == 0, coord->unit_pos > 0));

	memset(&idata, 0, sizeof(idata));
	item_key_by_coord(coord, &item_key);
	unit_key_by_coord(coord, &split_key);
	set_key_offset(&split_key,
		       get_key_offset(&split_key) +
		       (mctx->split_pos << current_blocksize_bits));

	tail_num_units = coord_num_units(coord) - coord->unit_pos;
	tail_len = tail_num_units * sizeof(reiser4_extent);

	if (mctx->split_pos != 0) {
#if REISER4_DEBUG
		reiser4_key check_key;
#endif
		/*
		 * start from splitting the unit
		 */
		ret = split_extent_unit(coord,
					mctx->split_pos,
					0 /* stay on the original position */);
		if (ret)
			return ret;
		assert("edward-2110",
		       keyeq(&item_key, item_key_by_coord(coord, &check_key)));
		/*
		 * check if it was the case of new item creation.
		 * If so, then nothing to do any more: the item
		 * has been split already
		 */
		if (reiser4_extent_size(coord) ==
		    get_key_offset(&split_key) - get_key_offset(&item_key))
			/*
			 * item was split at specified offset
			 */
			return 0;
		/*
		 * @coord is the original unit that was split
		 */
		tail_orig =
			node_plugin_by_node(coord->node)->item_by_coord(coord) +
			(coord->unit_pos + 1) * sizeof(reiser4_extent);
	} else {
		/*
		 * @coord is the original unit that was not
		 * subjected to splitting
		 */
		tail_orig =
			node_plugin_by_node(coord->node)->item_by_coord(coord) +
			coord->unit_pos * sizeof(reiser4_extent);
	}
	tail_copy = kmalloc(tail_len, reiser4_ctx_gfp_mask_get());
	if (!tail_copy)
		return -ENOMEM;
	memcpy(tail_copy, tail_orig, tail_len);
	/*
	 * cut off the tail from the original item
	 */
	coord_dup(&cut_from, coord);
	if (mctx->split_pos)
		/* the original unit was split */
		cut_from.unit_pos ++;
	coord_dup(&cut_to, coord);
	cut_to.unit_pos = coord_num_units(coord) - 1;

	cut_node_content(&cut_from, &cut_to, NULL, NULL, NULL);
	/* make sure that @coord is valid after cut operation */
	if (mctx->split_pos == 0)
		coord->unit_pos --;
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

	ret = reserve_split_extent_item();
	if (ret)
		return ret;
	loaded = mctx->coord->node;
	ret = zload(loaded);
	if (ret)
		return ret;
	ret = split_extent_item(mctx);
	zrelse(loaded);
	return ret;
}

static void init_migration_context(struct extent_migrate_context *mctx,
				   struct inode *inode, coord_t *coord,
				   lock_handle *lh)
{
	memset(mctx, 0, sizeof(*mctx));
	mctx->coord = coord;
	mctx->inode = inode;
	mctx->lh = lh;
	item_key_by_coord(coord, &mctx->key);
}

/*
 * clean up everything except @inode, @coord, @key and @lh
 */
static void reset_migration_context(struct extent_migrate_context *mctx)
{
	mctx->act = INVALID_ACTION;
	mctx->stop = 0;
	mctx->split_pos = 0;
	mctx->blocks_migrated = 0;
	mctx->migrate_whole_item = 0;
}

/**
 * Assign primitive migration operation over the given item
 * specified by @mctx.coord
 */
static void what_to_do(struct extent_migrate_context *mctx)
{
	loff_t off1, off2;
	loff_t split_off;

	coord_t *coord;
	lookup_result ret;
	struct inode *inode = mctx->inode;
	reiser4_key split_key;

	coord = mctx->coord;
	zload(coord->node);
	coord_clear_iplug(coord);

	reset_migration_context(mctx);
	/*
	 * find split offset in the item, i.e. maximal offset,
	 * so that data bytes at offset and (offset - 1) belong
	 * to different bricks in the new logical volume
	 */
	if (current_stripe_bits == 0) {
		mctx->new_loc = calc_data_subvol(inode, 0)->id;
		goto split_off_not_found;
	}
	/* offset of the leftmost byte */
	off1 = get_key_offset(&mctx->key);
	/* offset of rightmost byte */
	off2 = off1 + reiser4_extent_size(coord) - 1;

	/* normalize offsets */
	off1 = off1 - (off1 & (current_stripe_size - 1));
	off2 = off2 - (off2 & (current_stripe_size - 1));

	mctx->new_loc = calc_data_subvol(inode, off2)->id;

	while (off1 < off2) {
		off2 -= current_stripe_size;
		if (calc_data_subvol(inode, off2)->id != mctx->new_loc) {
			split_off = off2 + current_stripe_size;
			goto split_off_found;
		}
	}
 split_off_not_found:
	mctx->stop_off = get_key_offset(&mctx->key);
	if (mctx->new_loc != get_key_ordering(&mctx->key)) {
		/*
		 * the whole item should be migrated
		 */
		mctx->migrate_whole_item = 1;
		mctx->act = MIGRATE_EXTENT;
		zrelse(coord->node);
		return;
	} else {
		/*
		 * the item is not to be splitted, or
		 * migrated - finish to process extent
		 */
		mctx->stop = 1;
		mctx->act = SKIP_EXTENT;
		zrelse(coord->node);
		return;
	}
 split_off_found:
	assert("edward-2112",
	       (get_key_offset(&mctx->key) < split_off) &&
	       (split_off < (get_key_offset(&mctx->key) +
			     reiser4_extent_size(coord))));

	mctx->stop_off = split_off;

	if (mctx->new_loc != get_key_ordering(&mctx->key)) {
		/*
		 * Only a part of item should be migrated.
		 * In this case we don't perform the regular
		 * split operation - the item will be "split"
		 * by migration procedure
		 */
		mctx->migrate_whole_item = 0;
		mctx->act = MIGRATE_EXTENT;
		zrelse(coord->node);
		return;
	}
	/*
	 * The item to be split, its right part to be
	 * skipped, and the left part to be processed in
	 * the next iteration starting from what_to_do().
	 * Now calculate position for split.
	 */
	memcpy(&split_key, &mctx->key, sizeof(split_key));
	set_key_offset(&split_key, split_off);
	ret = lookup_extent(&split_key, FIND_EXACT, coord);

	assert("edward-2113", ret == CBK_COORD_FOUND);
	assert("edward-2114", coord->between == AT_UNIT);

	unit_key_by_coord(coord, &split_key);

	assert("edward-2115", get_key_offset(&split_key) <= split_off);

	mctx->split_pos =
		(split_off - get_key_offset(&split_key)) >> PAGE_SHIFT;
	mctx->act = SPLIT_EXTENT;
	zrelse(coord->node);
	return;
}

#define MIGRATION_GRANULARITY (1024)

int reiser4_migrate_extent(coord_t *coord, lock_handle *lh, struct inode *inode,
			   loff_t *done_off)
{
	int ret = 0;
	reiser4_block_nr blocks_migrated = 0;
	struct extent_migrate_context mctx;

	init_migration_context(&mctx, inode, coord, lh);

	while (!mctx.stop) {
		what_to_do(&mctx);
		switch(mctx.act) {
		case SKIP_EXTENT:
			*done_off = mctx.stop_off;
			goto out;
		case SPLIT_EXTENT:
			ret = do_split_extent(&mctx);
			if (ret)
				goto out;
			continue;
		case MIGRATE_EXTENT:
			ret = do_migrate_extent(&mctx);
			if (ret)
				goto out;
			assert("edward-2351", mctx.blocks_migrated > 0);
			*done_off = mctx.done_off;

			blocks_migrated += mctx.blocks_migrated;
			if (blocks_migrated >= MIGRATION_GRANULARITY) {
				/*
				 * Interrupt long migration to commit
				 * transaction
				 */
				ret = -E_REPEAT;
				goto out;
			}
			break;
		default:
			impossible("edward-2116",
				   "Bad migrate action id %d", mctx.act);
		}
	}
 out:
	done_lh(lh);
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
