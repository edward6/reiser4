/*
  Copyright (c) 2018 Eduard O. Shishkin

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
#include <linux/swap.h>

void check_uf_coord(const uf_coord_t *uf_coord, const reiser4_key *key);
void check_jnodes(znode *twig, const reiser4_key *key, int count);
size_t filemap_copy_from_user(struct page *page, unsigned long offset,
			      const char __user *buf, unsigned bytes);

#if 0
static void check_node(znode *node)
{
	const char *error;

	zload(node);
	assert("edward-2076",
	       check_node40(node, REISER4_NODE_TREE_STABLE, &error) == 0);
	zrelse(node);
}
#else
#define check_node(node) noop
#endif

static void try_merge_with_right_item(coord_t *left)
{
	coord_t right;

	coord_dup(&right, left);

	if (coord_next_item(&right))
		/*
		 * there is no items at the right
		 */
		return;
	if (are_items_mergeable(left, &right)) {
		node_plugin_by_node(left->node)->merge_items(left, &right);
		znode_make_dirty(left->node);
	}
}

/**
 * Push a pointer to one unallocated physical block to the
 * storage tree.
 *
 * @key: key of the logical block of the file's body;
 * @uf_coord: location to push (was found by coord_by_key());
 *
 * Pre-condition: logical block is not yet represented by any
 * pointer in the storage tree (thus, such name "plugging a hole")
 *
 * First try to push the pointer to an existing item at the left.
 * If impossible, then create a new extent item and try to merge
 * it with an item at the right.
 */
static int plug_hole_stripe(uf_coord_t *uf_coord, const reiser4_key *key)
{
	int ret = 0;
	znode *loaded;
	reiser4_key akey;
	coord_t *coord = &uf_coord->coord;
	reiser4_extent *ext;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	//ON_DEBUG(const char *error);

	assert("edward-2052", !coord_is_existing_unit(coord));

	if (coord->between != AFTER_UNIT) {
		/*
		 * there is no file items at the left (in physical
		 * order), thus we are on the leaf level, where the
		 * search procedure has landed. So, use a carry_extent
		 * primitive to insert a new extent item.
		 */
		znode *twig_node;
		assert("edward-2053", znode_is_loaded(coord->node));
		assert("edward-2054", coord->node->level == LEAF_LEVEL);
		BUG_ON(coord->node->level != LEAF_LEVEL);

		reiser4_set_extent(subvol_by_key(key), &new_ext,
				   UNALLOCATED_EXTENT_START, 1);
		init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
		ret = insert_extent_by_coord(coord, &idata,
					     key, uf_coord->lh);
		if (ret)
			return ret;
		/*
		 * A new extent item has been inserted on the twig level.
		 * To merge it with an item at the right we need to find
		 * the insertion point, as carry_extent primitive doesn't
		 * provide it (only lock handle).
		 */
		twig_node = uf_coord->lh->node;
		assert("edward-2073", twig_node != coord->node);

		ret = zload(twig_node);
		if (ret)
			return ret;
		coord_init_zero(coord);
		ret = node_plugin_by_node(twig_node)->lookup(twig_node,
							     key,
							     FIND_EXACT,
							     coord);
		BUG_ON(ret != NS_FOUND);
		assert("edward-2074", twig_node == coord->node);

		try_merge_with_right_item(coord);
#if 0
		assert("edward-2352",
		       check_node40(twig_node,
				    REISER4_NODE_TREE_STABLE, &error) == 0);
#endif
		zrelse(twig_node);
		return 0;
	}
	/*
	 * We are on the twig level.
	 * Try to push the pointer to the end of extent item specified
	 * by @coord
	 */
	assert("edward-2057", item_is_extent(coord));

	if (!keyeq(key, append_key_extent(coord, &akey))) {
		/*
		 * Can not push. Create a new item.
		 *
		 * FIXME-EDWARD: here it would be nice to try
		 * also to push to the beginning of the item at
		 * the right. However, current implementation
		 * of extent items doesn't allow to do it. We
		 * can only to create a new item and merge it
		 * with the right neighbor.
		 */
		reiser4_set_extent(subvol_by_key(key), &new_ext,
				   UNALLOCATED_EXTENT_START, 1);
		init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
		ret = insert_by_coord(coord, &idata, key, uf_coord->lh, 0);
	} else {
		/*
		 * We can push to the end of the item
		 */
		coord->unit_pos = coord_last_unit_pos(coord);
		ext = extent_by_coord(coord);

		assert("edward-2267",
		       subvol_by_key(key) == find_data_subvol(coord));

		if ((state_of_extent(ext) == UNALLOCATED_EXTENT)) {
			/*
			 * fast paste without carry
			 */
			extent_set_width(subvol_by_key(key), ext,
					 extent_get_width(ext) + 1);
			znode_make_dirty(coord->node);
		} else {
			/*
			 * paste with possible carry
			 */
			coord->between = AFTER_UNIT;
			reiser4_set_extent(subvol_by_key(key), &new_ext,
					   UNALLOCATED_EXTENT_START, 1);
			init_new_extent(EXTENT41_POINTER_ID,
					&idata, &new_ext, 1);
			ret = insert_into_item(coord, uf_coord->lh,
					       key, &idata, 0);
		}
	}
	if (ret)
		return ret;
	assert("edward-2075", coord->node == uf_coord->lh->node);
	/*
	 * Here @coord points out to the item, the pointer
	 * was pushed to, or to a newly created item. Try to
	 * merge it with the item at the right.
	 */
	ret = zload(coord->node);
	if (ret)
		return ret;
	loaded = coord->node;
	try_merge_with_right_item(coord);
#if 0
	assert("edward-2353",
	       check_node40(loaded,
			    REISER4_NODE_TREE_STABLE, &error) == 0);
#endif
	zrelse(loaded);
	return 0;
}

#if 0
/**
 * Make sure that hole was plugged successfully.
 * That is, block pointer with @key exists in the storage tree
 */
static void check_plug_hole(uf_coord_t *uf_coord, const reiser4_key *key)
{
	int ret;
	coord_t coord;
	znode *node = uf_coord->coord.node;

	assert("edward-2354", node == uf_coord->lh->node);

	ret = zload(node);
	if (ret)
		return;
	ret = node_plugin_by_node(node)->lookup(node,
						key,
						FIND_EXACT,
						&coord);
	zrelse(node);
	assert("edward-2355", ret == NS_FOUND);
	assert("edward-2356", coord.between == AT_UNIT);
}
#endif

static int process_one_block(uf_coord_t *uf_coord, const reiser4_key *key,
			     jnode *node, int *hole_plugged)
{
	reiser4_subvol *subv;
	reiser4_block_nr block;

	subv = current_origin(get_key_ordering(key));
	assert("edward-2220", node->subvol == NULL || node->subvol == subv);

	if (uf_coord->coord.between != AT_UNIT) {
		/*
		 * there is no item containing @key in the tree
		 */
		int result;
		uf_coord->valid = 0;
		inode_add_blocks(mapping_jnode(node)->host, 1);
		result = plug_hole_stripe(uf_coord, key);
		if (result)
			return result;
		//ON_DEBUG(check_plug_hole(uf_coord, key));

		if (*jnode_get_block(node)) {
			/*
			 * Olala :(
			 * Non-zero address while block pointer is
			 * absent in the tree - this should not ever
			 * happen!
			 *
			 * Experience shows that address referred
			 * by that jnode is not actual, so we have
			 * the courage to overwrite it. Nevertheless:
			 * FIXME-EDWARD: Find the reason of appearance
			 * of such jnodes.
			 */
			warning("edward-2371",
				"Orphan jnode. Address (%llu %llu) overwritten",
				(unsigned long long )(*jnode_get_block(node)),
				(unsigned long long)(jnode_get_subvol(node)->id));
			JF_CLR(node, JNODE_RELOC);
			JF_CLR(node, JNODE_OVRWR);
		}
		block = fake_blocknr_unformatted(1, subv);
		if (hole_plugged)
			*hole_plugged = 1;
		JF_SET(node, JNODE_CREATED);
	} else {
		reiser4_extent *ext;
		struct extent_coord_extension *ext_coord;

		if (*jnode_get_block(node))
			return 0;

		ext_coord = ext_coord_by_uf_coord(uf_coord);
		check_uf_coord(uf_coord, NULL);
		ext = (reiser4_extent *)(zdata(uf_coord->coord.node) +
					 uf_coord->extension.extent.ext_offset);
		if (state_of_extent(ext) != ALLOCATED_EXTENT)
			return RETERR(-EIO);

		block = extent_get_start(ext) + ext_coord->pos_in_unit;
	}
	jnode_set_block(node, &block);
	jnode_set_subvol(node, subv);
	return 0;
}

int process_extent_stripe(uf_coord_t *uf_coord, const reiser4_key *key,
			  jnode **jnodes, int *plugged_hole)
{
	int ret;
	jnode *node = jnodes[0];
	struct atom_brick_info *abi;

	ret = process_one_block(uf_coord, key, node, plugged_hole);
	if (ret)
		return ret;
	assert("edward-2358", node->subvol != NULL);
	/*
	 * make sure that locked twig node contains
	 * all jnodes we are about to capture
	 */
	check_jnodes(uf_coord->lh->node, key, 1);

	ret = check_insert_atom_brick_info(node->subvol->id, &abi);
	if (ret)
		return ret;

	spin_lock_jnode(node);
	ret = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
	BUG_ON(ret != 0);
	jnode_make_dirty_locked(node);
	spin_unlock_jnode(node);
#if REISER4_DEBUG
	if (uf_coord->valid)
		check_uf_coord(uf_coord, key);
#endif
	return 1;
}

/*
 * to write @count pages to a file by extents we have to reserve disk
 * space for:
 *
 * 1. find_file_item() may have to insert empty node to the tree
 * (empty leaf node between two extent items). This requires:
 * (a) 1 block for the leaf node;
 * (b) number of formatted blocks which are necessary to perform
 * insertion of an internal item into twig level.
 *
 * 2. for each of written pages there might be needed:
 * (a) 1 unformatted block for the page itself;
 * (b) number of blocks which might be necessary to insert or
 * paste to an extent item.
 *
 * 3. stat data update.
 */

/**
 * Reserve space on meta-data brick needed to write, or truncate
 * @count data pages.
 *
 * @count: number of data pages to be written (or truncated).
 */
int reserve_stripe_meta(int count, int truncate)
{
	int count_m;
	reiser4_subvol *subv_m = get_meta_subvol();
	reiser4_tree *tree_m = &subv_m->tree;
	/*
	 * Reserve space for 1, 2b, 3 (see comment above)
	 */
	grab_space_enable();
	count_m = estimate_one_insert_item(tree_m) +
		count * estimate_one_insert_into_item(tree_m) +
		estimate_one_insert_item(tree_m);
	if (truncate)
		return reiser4_grab_reserved(reiser4_get_current_sb(),
					     count_m, BA_CAN_COMMIT, subv_m);
	else
		return reiser4_grab_space(count_m, BA_CAN_COMMIT, subv_m);
}

/**
 * Reserve space on data brick needed to write, or truncate
 * @count data pages.
 *
 * @dsubv: data brick to perform reservation on;
 * @count: number of data pages to be written or truncated.
 */
int reserve_stripe_data(int count, reiser4_subvol *dsubv, int truncate)
{
	assert("edward-2359", ergo(truncate, count == 1));

	grab_space_enable();
	if (truncate)
		return reiser4_grab_reserved(reiser4_get_current_sb(),
					     count, BA_CAN_COMMIT, dsubv);
	else
		return reiser4_grab_space(count, 0, dsubv);
}

/**
 * Determine on what brick a data page will be stored,
 * and reserve space on that brick.
 */
static int locate_reserve_data(coord_t *coord, lock_handle *lh,
			       reiser4_key *key, struct inode *inode,
			       loff_t pos, jnode *node, int truncate,
			       reiser4_subvol **loc)
{
	int ret;

	if (coord->between == AT_UNIT) {
		ret = zload(coord->node);
		if (ret)
			return ret;
		*loc = find_data_subvol(coord);
		zrelse(coord->node);
		assert("edward-2360",
		       ergo(node->subvol, node->subvol == *loc));
	} else if (node->subvol)
		/*
		 * this is a hint from migration procedure
		 */
		*loc = node->subvol;
	else
		*loc = calc_data_subvol(inode, pos);

	assert("edward-2361", *loc != NULL);
	/*
	 * Now reserve space on @loc.
	 * Note that in the case of truncate the space
	 * has been already reserved in shorten_stripe()
	 */
	return truncate ? 0 : reserve_stripe_data(1, *loc, 0);
}

/**
 * Update file body after writing @count blocks at offset @pos.
 * Return 0 on success.
 */
static int __update_extents_stripe(struct hint *hint, struct inode *inode,
				   jnode **jnodes, int count,
				   int *plugged_hole, int truncate)
{
	int ret = 0;
	reiser4_key key;
	loff_t pos;

	if (!count)
		/* expanding truncate - nothing to do */
		return 0;
	assert("edward-2323", ergo(truncate != 0, count == 1));

	pos = ((loff_t)index_jnode(jnodes[0]) << PAGE_SHIFT);
	/*
	 * construct non-precise key
	 */
	build_body_key_stripe(inode, pos, &key);
	do {
		znode *loaded;
		reiser4_subvol *dsubv = NULL;

		ret = find_file_item_nohint(&hint->ext_coord.coord,
					    hint->ext_coord.lh, &key,
					    ZNODE_WRITE_LOCK, inode);
		if (IS_CBKERR(ret))
			return -EIO;
		/*
		 * reserve space for data
		 */
		ret = locate_reserve_data(&hint->ext_coord.coord,
					  hint->ext_coord.lh, &key,
					  inode, pos, jnodes[0],
					  truncate, &dsubv);
		if (ret) {
			done_lh(hint->ext_coord.lh);
			break;
		}
		assert("edward-2284", dsubv != NULL);
		assert("edward-2362",
		       ergo(jnodes[0]->subvol, jnodes[0]->subvol == dsubv));
		/*
		 * Now when we know location of data block, make key precise
		 */
		set_key_ordering(&key, dsubv->id);
		ret = zload(hint->ext_coord.coord.node);
		BUG_ON(ret != 0);
		loaded = hint->ext_coord.coord.node;
		//check_node(loaded);

		if (hint->ext_coord.coord.between == AT_UNIT)
			init_coord_extension_extent(&hint->ext_coord,
						    get_key_offset(&key));
		/*
		 * overwrite extent (or create a new one, if it doesn't exist)
		 */
		ret = process_extent_stripe(&hint->ext_coord, &key, jnodes,
					    plugged_hole);
		zrelse(loaded);
		if (ret < 0) {
			done_lh(hint->ext_coord.lh);
			break;
		}
#if 0
		zload(hint->ext_coord.lh->node);
		assert("edward-2076",
		       check_node40(hint->ext_coord.lh->node,
				    REISER4_NODE_TREE_STABLE, &error) == 0);
		zrelse(hint->ext_coord.lh->node);
#endif
		jnodes += ret;
		count -= ret;
		pos += ret * PAGE_SIZE;
		/*
		 * Update key for the next iteration.
		 * We don't know location of the next data block,
		 * so set maximal ordering value.
		 */
		set_key_offset(&key, pos);
		set_key_ordering(&key, KEY_ORDERING_MASK);
		/*
		 * seal and unlock znode
		 */
		if (0 /*hint->ext_coord.valid */)
			reiser4_set_hint(hint, &key, ZNODE_WRITE_LOCK);
		else
			reiser4_unset_hint(hint);
	} while (count > 0);
	return ret;
}

int update_extents_stripe(struct file *file, struct inode *inode,
				 jnode **jnodes, int count)
{
	int ret;
	struct hint hint;

	assert("edward-2063", reiser4_lock_counters()->d_refs == 0);

	ret = load_file_hint(file, &hint);
	if (ret)
		return ret;
	ret = __update_extents_stripe(&hint, inode, jnodes, count,
				      NULL, 0);
	assert("edward-2065",  reiser4_lock_counters()->d_refs == 0);
	return ret;
}

int update_extent_stripe(struct inode *inode, jnode *node,
			 int *plugged_hole, int truncate)
{
	int ret;
	struct hint hint;

	hint_init_zero(&hint);

	ret = __update_extents_stripe(&hint, inode, &node, 1,
				      plugged_hole, truncate);

	assert("edward-2067", ret == 1 || ret < 0);
	return (ret == 1) ? 0 : ret;
}

int find_or_create_extent_stripe(struct page *page, int truncate)
{
	int result;
	struct inode *inode;
	int plugged_hole = 0;

	jnode *node;

	assert("edward-2372", page->mapping && page->mapping->host);

	inode = page->mapping->host;

	lock_page(page);
	node = jnode_of_page(page);
	if (IS_ERR(node)) {
		unlock_page(page);
		return PTR_ERR(node);
	}
	JF_SET(node, JNODE_WRITE_PREPARED);
	unlock_page(page);

	result = update_extent_stripe(inode, node, &plugged_hole,
				      truncate);
	JF_CLR(node, JNODE_WRITE_PREPARED);

	if (result) {
		jput(node);
		warning("edward-1549",
			"failed to update extent (%d)", result);
		return result;
	}
	if (plugged_hole)
		reiser4_update_sd(inode);

	BUG_ON(node->atom == NULL);

	if (get_current_context()->entd) {
		entd_context *ent = get_entd_context(inode->i_sb);

		if (ent->cur_request->page == page)
			/*
			 * the following reference will be
			 * dropped in reiser4_writeout
			 */
			ent->cur_request->node = jref(node);
	}
	jput(node);
	return 0;
}

/*
 * Non-exclusive access to the file must be acquired
 */
ssize_t write_extent_stripe(struct file *file, struct inode *inode,
			    const char __user *buf, size_t count,
			    loff_t *pos)
{
	int nr_pages;
	int nr_dirty = 0;
	struct page *page;
	jnode *jnodes[DEFAULT_WRITE_GRANULARITY + 1];
	unsigned long index;
	unsigned long end;
	int i;
	int to_page, page_off;
	size_t left = count;
	int ret = 0;
	/*
	 * calculate number of pages which are to be written
	 */
	index = *pos >> PAGE_SHIFT;
	end = ((*pos + count - 1) >> PAGE_SHIFT);
	nr_pages = end - index + 1;
	assert("edward-2363", nr_pages <= DEFAULT_WRITE_GRANULARITY + 1);
	/*
	 * First of all reserve space on meta-data brick.
	 * In particular, it is needed to "drill" the leaf level
	 * by search procedure.
	 */
	ret = reserve_stripe_meta(nr_pages, 0);
	if (ret)
		return ret;
	if (count == 0) {
		/* case of expanding truncate */
		update_extents_stripe(file, inode, jnodes, 0);
		return 0;
	}
	BUG_ON(get_current_context()->trans->atom != NULL);

	/* get pages and jnodes */
	for (i = 0; i < nr_pages; i ++) {
		page = find_or_create_page(inode->i_mapping, index + i,
					   reiser4_ctx_gfp_mask_get());
		if (page == NULL) {
			nr_pages = i;
			ret = RETERR(-ENOMEM);
			goto out;
		}
		jnodes[i] = jnode_of_page(page);
		if (IS_ERR(jnodes[i])) {
			unlock_page(page);
			put_page(page);
			nr_pages = i;
			ret = RETERR(-ENOMEM);
			goto out;
		}
		/* prevent jnode and page from disconnecting */
		JF_SET(jnodes[i], JNODE_WRITE_PREPARED);
		unlock_page(page);
	}
	BUG_ON(get_current_context()->trans->atom != NULL);

	page_off = (*pos & (PAGE_SIZE - 1));
	for (i = 0; i < nr_pages; i ++) {
		size_t written;
		to_page = PAGE_SIZE - page_off;
		if (to_page > left)
			to_page = left;
		page = jnode_page(jnodes[i]);
		if (page_offset(page) < inode->i_size &&
		    !PageUptodate(page) && to_page != PAGE_SIZE) {
			/*
			 * the above is not optimal for partial write to last
			 * page of file when file size is not at boundary of
			 * page
			 */
			lock_page(page);
			if (!PageUptodate(page)) {
				ret = readpage_stripe(NULL, page);
				assert("edward-2364", ret == 0);
				BUG_ON(ret != 0);
				/* wait for read completion */
				lock_page(page);
				BUG_ON(!PageUptodate(page));
			} else
				ret = 0;
			unlock_page(page);
		}

		BUG_ON(get_current_context()->trans->atom != NULL);
		fault_in_pages_readable(buf, to_page);
		BUG_ON(get_current_context()->trans->atom != NULL);

		lock_page(page);
		if (!PageUptodate(page) && to_page != PAGE_SIZE)
			zero_user_segments(page, 0, page_off,
					   page_off + to_page,
					   PAGE_SIZE);

		written = filemap_copy_from_user(page, page_off, buf, to_page);
		if (unlikely(written != to_page)) {
			unlock_page(page);
			ret = RETERR(-EFAULT);
			break;
		}

		flush_dcache_page(page);
		set_page_dirty_notag(page);
		unlock_page(page);
		nr_dirty ++;

		mark_page_accessed(page);
		SetPageUptodate(page);

		page_off = 0;
		buf += to_page;
		left -= to_page;
		BUG_ON(get_current_context()->trans->atom != NULL);
	}

	left = count;
	page_off = (*pos & (PAGE_SIZE - 1));

	for (i = 0; i < nr_dirty; i ++)	{
		to_page = PAGE_SIZE - page_off;
		if (to_page > left)
			to_page = left;
		ret = update_extents_stripe(file, inode, &jnodes[i], 1);
		assert("edward-2365", ret == -ENOSPC || ret >= 0);
		if (ret < 0)
			break;
		page_off = 0;
		left -= to_page;
	}
 out:
	for (i = 0; i < nr_pages; i ++) {
		put_page(jnode_page(jnodes[i]));
		JF_CLR(jnodes[i], JNODE_WRITE_PREPARED);
		jput(jnodes[i]);
	}
	/*
	 * the only errors handled so far is ENOMEM and
	 * EFAULT on copy_from_user
	 */
	return (count - left) ? (count - left) : ret;
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * scroll-step: 1
 * End:
 */
