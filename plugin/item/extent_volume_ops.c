/*
  Copyright (c) 2017 Eduard O. Shishkin

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
	int migrate_whole_item;
	reiser4_key key; /* key of extent item to be migrated,
			    is set by what_to_do() */
	struct inode *inode;
	u32 new_loc;
	u64 size; /* bytes to migrate */
	reiser4_block_nr split_pos; /* position in unit */
	lock_handle *lh;
#if REISER4_DEBUG
	reiser4_block_nr item_len;
#endif
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

static int reserve_migrate_one_block(struct inode *inode, u32 where)
{
	int ret;
	reiser4_subvol *subv_m = get_meta_subvol();
	reiser4_tree *tree_m = &subv_m->tree;
	reiser4_subvol *subv_d = current_origin(where);
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
	 * reserve space for 2(a)
	 */
	grab_space_enable();
	ret = reiser4_grab_space(1, 0, subv_d);
	if (ret)
		return ret;
	/*
	 * reserve space for 1, 2(b), 3
	 */
	grab_space_enable();
	ret = reiser4_grab_space(estimate_one_insert_item(tree_m) +
				 1 * estimate_one_insert_into_item(tree_m) +
				 estimate_one_insert_item(tree_m), 0, subv_m);
	if (ret)
		return ret;
	set_current_data_subvol(subv_d);
	return 0;
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

	ret = reserve_migrate_one_block(inode, mctx->new_loc);
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
	if (ret) {
		put_page(page);
		return ret;
	}
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

	ret = update_extent_stripe(inode, node, page_offset(page), NULL, 0);
	clear_current_data_subvol();
	if (ret)
		warning("edward-1897",
			"Failed to migrate block %lu of inode %llu (%d)",
			index, (unsigned long long)get_inode_oid(inode),
			ret);
	JF_CLR(node, JNODE_WRITE_PREPARED);
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
	reiser4_block_nr nr_blocks;

	lock_handle *lh = mctx->lh;
	coord_t *coord = mctx->coord;
	reiser4_key *key = &mctx->key;

	assert("edward-2106", coord->node == lh->node);
	assert("edward-2128", get_key_ordering(key) != mctx->new_loc);

	nr_blocks = mctx->size >> current_blocksize_bits;

	while (nr_blocks) {
		znode *loaded;

		ret = zload(coord->node);
		if (ret)
			break;
		loaded = coord->node;
		/*
		 * this will release the longterm lock
		 */
		ret = migrate_one_block(mctx);
		assert("edward-2322", get_current_data_subvol() == NULL);
		zrelse(loaded);
		if (ret)
			break;
		reiser4_throttle_write(mctx->inode);

		nr_blocks --;
		if (nr_blocks == 0 && mctx->migrate_whole_item)
			/*
			 * the whole item has been migrated
			 */
			break;
		/*
		 * go to the beginning of the item
		 */
		ret = find_file_item_nohint(coord, lh, key,
					    ZNODE_WRITE_LOCK, mctx->inode);
		if (ret) {
			/*
			 * item not found (killed by concurrent
			 * truncate, or error happened)
			 */
			warning("edward-2318",
				"Item not found after migration (%d)", ret);
			done_lh(lh);
			if (!IS_CBKERR(ret))
				ret = 0;
			break;
		}
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
#if REISER4_DEBUG
	mctx->item_len = reiser4_extent_size(coord);
#endif
}

static void reset_migration_context(struct extent_migrate_context *mctx)
{
	mctx->act = INVALID_ACTION;
	mctx->size = 0;
	mctx->split_pos = 0;
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
	reiser4_key *key = &mctx->key;
	lookup_result ret;
	struct inode *inode = mctx->inode;
	file_plugin *fplug = inode_file_plugin(inode);

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
		mctx->new_loc = fplug->calc_data_subvol(inode, 0)->id;
		goto split_off_not_found;
	}
	/* offset of the leftmost byte */
	off1 = get_key_offset(item_key_by_coord(coord, key));
	/* offset of rightmost byte */
	off2 = off1 + reiser4_extent_size(coord) - 1;

	/* normalize offsets */
	off1 = off1 - (off1 & (current_stripe_size - 1));
	off2 = off2 - (off2 & (current_stripe_size - 1));

	mctx->new_loc = fplug->calc_data_subvol(inode, off2)->id;

	while (off1 < off2) {
		off2 -= current_stripe_size;
		if (fplug->calc_data_subvol(inode, off2)->id != mctx->new_loc) {
			split_off = off2 + current_stripe_size;
			goto split_off_found;
		}
	}
 split_off_not_found:
	mctx->size = reiser4_extent_size(coord);
	if (mctx->new_loc != get_key_ordering(key)) {
		/*
		 * the whole item should be migrated
		 */
		mctx->migrate_whole_item =
			(mctx->size == reiser4_extent_size(coord));
		mctx->act = MIGRATE_EXTENT;
		zrelse(coord->node);
		return;
	} else {
		/*
		 * the item is not to be split, or migrated
		 */
		mctx->act = SKIP_EXTENT;
		zrelse(coord->node);
		return;
	}
 split_off_found:
	assert("edward-2112",
	       (get_key_offset(key) < split_off) &&
	       (split_off < (get_key_offset(key) +
			     reiser4_extent_size(coord))));

	mctx->size = get_key_offset(key) +
		reiser4_extent_size(coord) - split_off;

	if (mctx->new_loc != get_key_ordering(key)) {
		/*
		 * a part of item should be migrated
		 */
		mctx->migrate_whole_item =
			(mctx->size == reiser4_extent_size(coord));
		mctx->act = MIGRATE_EXTENT;
		zrelse(coord->node);
		return;
	}
	/*
	 * the item should be split, calculate position to split
	 */
	set_key_offset(key, split_off);
	ret = lookup_extent(key, FIND_EXACT, coord);

	assert("edward-2113", ret == CBK_COORD_FOUND);
	assert("edward-2114", coord->between == AT_UNIT);

	unit_key_by_coord(coord, key);

	assert("edward-2115", get_key_offset(key) <= split_off);

	mctx->split_pos =
		(split_off - get_key_offset(key)) >> current_blocksize_bits;
	mctx->act = SPLIT_EXTENT;
	zrelse(coord->node);
	return;
}

#define MIGRATION_GRANULARITY (1024)

int reiser4_migrate_extent(coord_t *coord, lock_handle *lh, struct inode *inode)
{
	int ret = 0;
	reiser4_block_nr blocks_migrated = 0;
	u64 len = reiser4_extent_size(coord);
	struct extent_migrate_context mctx;

	init_migration_context(&mctx, inode, coord, lh);

	while (len) {
		what_to_do(&mctx);

		switch(mctx.act) {
		case MIGRATE_EXTENT:
			ret = do_migrate_extent(&mctx);
			if (ret)
				break;
			blocks_migrated += (mctx.size >> PAGE_SHIFT);
			break;
		case SPLIT_EXTENT:
			ret = do_split_extent(&mctx);
			break;
		case SKIP_EXTENT:
			break;
		default:
			impossible("edward-2116", "bad migrate action id");
		}
		if (ret)
			break;
		assert("edward-2117", mctx.size <= len);
		len -= mctx.size;
#if REISER4_DEBUG
		mctx.item_len -= mctx.size;
#endif
		if (len && blocks_migrated >= MIGRATION_GRANULARITY) {
			/*
			 * migration process will be restarted
			 * for the same item after atom commit
			 */
			ret = -E_REPEAT;
			break;
		}
	}
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
