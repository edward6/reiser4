/*
  Copyright (c) 2018-2020 Eduard O. Shishkin

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
int find_stripe_item(hint_t *hint, const reiser4_key *key,
		     znode_lock_mode lock_mode, struct inode *inode);
reiser4_block_nr estimate_write_stripe_meta(int count);
int update_item_key(coord_t *target, const reiser4_key *key);

int try_merge_with_right_item(coord_t *left)
{
	coord_t right;

	coord_dup(&right, left);

	if (coord_next_item(&right))
		/*
		 * there is no items at the right
		 */
		return 0;
	if (are_items_mergeable(left, &right)) {
		node_plugin_by_node(left->node)->merge_items(left, &right);
		znode_make_dirty(left->node);
	}
	return 0;
}

int try_merge_with_left_item(coord_t *right)
{
	coord_t left;

	coord_dup(&left, right);

	if (coord_prev_item(&left))
		/*
		 * there is no items at the left
		 */
		return 0;

	if (are_items_mergeable(&left, right)) {
		node_plugin_by_node(left.node)->merge_items(&left, right);
		znode_make_dirty(right->node);
	}
	return 0;
}

static inline int can_push_left(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key akey;

	return keyeq(key, append_key_extent(coord, &akey));
}

static inline int can_push_right(const coord_t *coord, const reiser4_key *key)
{
	coord_t right;
	reiser4_key ikey;
	reiser4_key pkey;

	coord_dup(&right, coord);

	if (coord_next_item(&right))
		/*
		 * there is no items at the right
		 */
		return 0;

	memcpy(&pkey, key, sizeof(*key));
	set_key_offset(&pkey, get_key_offset(key) + PAGE_SIZE);

	return keyeq(&pkey, item_key_by_coord(&right, &ikey));
}

/**
 * Put a pointer to file's logical block to the storage tree
 *
 * @key: key of the pointer
 * @uf_coord: location of the pointer (was found by coord_by_key())
 *
 * Pre-condition: the logical block is not yet pointed out by any item
 * in the storage tree.
 *
 * First, try to add the pointer to existing items. If impossible, then
 * create a new extent item
 */
static int add_block_pointer(coord_t *coord, lock_handle *lh,
			     const reiser4_key *key)
{
	int ret = 0;
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
		ret = insert_extent_by_coord(coord, &idata, key, lh);
		if (ret)
			return ret;
		/*
		 * A new extent item has been inserted on the twig level.
		 * To merge it with an item at the right we need to find
		 * the insertion point, as carry_extent primitive doesn't
		 * provide it (only lock handle).
		 */
		twig_node = lh->node;
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
	 * First, try to push the pointer to existing extent items
	 */
	assert("edward-2057", item_is_extent(coord));

	if (can_push_left(coord, key)) {
		/*
		 * push to the end of current item
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
			ret = insert_into_item(coord, lh, key, &idata, 0);
			if (ret)
				return ret;
		}
		return WITH_DATA(lh->node, try_merge_with_right_item(coord));

	} else if (can_push_right(coord, key)) {
		/*
		 * push to the beginning of the item at right
		 */
		coord_next_item(coord);
		ext = extent_by_coord(coord);

		if ((state_of_extent(ext) == UNALLOCATED_EXTENT)) {
			/*
			 * fast paste
			 */
			extent_set_width(subvol_by_key(key), ext,
					 extent_get_width(ext) + 1);
			/*
			 * since we push to the beginning of item,
			 * we need to update its key
			 */
			return update_item_key(coord, key);
		} else {
			/*
			 * paste with possible carry
			 */
			coord->between = BEFORE_UNIT;
			reiser4_set_extent(subvol_by_key(key), &new_ext,
					   UNALLOCATED_EXTENT_START, 1);
			init_new_extent(EXTENT41_POINTER_ID,
					&idata, &new_ext, 1);
			return insert_into_item(coord, lh, key, &idata, 0);
		}
		/*
		 * note that resulted item is not mergeable with an item
		 * at the left (otherwise we would fall to can_push_left()
		 * branch above)
		 */
	} else {
		/*
		 * we can't push to existing items, so create a new one
		 */
		reiser4_set_extent(subvol_by_key(key), &new_ext,
				   UNALLOCATED_EXTENT_START, 1);
		init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
		ret = insert_by_coord(coord, &idata, key, lh, 0);
		if (ret)
			return ret;
		/*
		 * it could happen that the newly created item got
		 * to neighbor node, where it is mergeable with an
		 * item at the right
		 */
		return WITH_DATA(lh->node, try_merge_with_right_item(coord));
	}
}

static int __update_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			   jnode *node, reiser4_subvol *subv)
{
	int ret;
	reiser4_block_nr block;
	struct atom_brick_info *abi;

	assert("edward-2468", subv == current_origin(get_key_ordering(key)));
	assert("edward-2220", node->subvol == NULL || node->subvol == subv);

	if (uf_coord->coord.between != AT_UNIT) {
		/*
		 * the logical block is not pointed out by any item in the tree
		 */
		if (*jnode_get_block(node)) {
			/*
			 * FIXME: explain in details appearance of such jnodes
			 */
			spin_lock_jnode(node);
			node->blocknr = 0;
			node->subvol = NULL;
			reiser4_uncapture_jnode(node);
		}
		assert("edward-2469", node->subvol == NULL);

		inode_add_blocks(mapping_jnode(node)->host, 1);
		/*
		 * adding a block pointer to the tree invalidates the extension
		 */
		uf_coord->valid = 0;
		ret = add_block_pointer(&uf_coord->coord, uf_coord->lh, key);
		if (ret)
			return ret;

		block = fake_blocknr_unformatted(1, subv);
		jnode_set_block(node, &block);
		jnode_set_subvol(node, subv);
		JF_SET(node, JNODE_CREATED);

	} else if (*jnode_get_block(node) == 0) {
		reiser4_extent *ext;
		struct extent_coord_extension *ext_coord;

		assert("edward-2477", uf_coord->valid);
		assert("edward-2470", node->subvol == NULL);

		ext_coord = ext_coord_by_uf_coord(uf_coord);
		check_uf_coord(uf_coord, NULL);
		ext = (reiser4_extent *)(zdata(uf_coord->coord.node) +
					 uf_coord->extension.extent.ext_offset);
		if (state_of_extent(ext) != ALLOCATED_EXTENT)
			return RETERR(-EIO);

		block = extent_get_start(ext) + ext_coord->pos_in_unit;
		jnode_set_block(node, &block);
		jnode_set_subvol(node, subv);
	}
	/*
	 * make sure that locked twig node contains jnode
	 * we are about to capture
	 */
	ON_DEBUG(check_jnodes(uf_coord->lh->node, key, 1));

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
	return 0;
}

/**
 * Find out on which brick the file's logical block should be stored,
 * and try to reserve space on that brick.
 *
 * Pre-condition: longterm lock is held
 */
static int locate_reserve_data(coord_t *coord, reiser4_key *key,
			       struct inode *inode, loff_t pos, jnode *node,
			       reiser4_subvol **loc, unsigned flags)
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
	} else if (reiser4_is_set(reiser4_get_current_sb(),
				  REISER4_PROXY_IO) &&
		   !(flags & UPX_PROXY_FULL))
		*loc = get_proxy_subvol();
	else
		*loc = calc_data_subvol(inode, pos);

	assert("edward-2361", *loc != NULL);
	/*
	 * Now we know where to reserve space for the logical block
	 */
	if (flags & UPX_TRUNCATE)
		/* the space has been already reserved in shorten_stripe() */
		return 0;
	grab_space_enable();
	return reiser4_grab_space(1 /* count */,
				  0 /* flags */, *loc /* where */);
}

#define FAST_SEQ_WRITE (1)

static int update_extent(struct hint *hint, struct inode *inode,
			 jnode *node, unsigned flags)
{
	int ret = 0;
	reiser4_key key;
	loff_t off;
	znode *loaded;
	reiser4_subvol *dsubv = NULL;

	off = ((loff_t)index_jnode(node) << PAGE_SHIFT);
	/*
	 * construct non-precise key
	 */
	build_body_key_stripe(inode, off, &key);

#if FAST_SEQ_WRITE
	ret = find_stripe_item(hint, &key, ZNODE_WRITE_LOCK, inode);
#else
	ret = find_file_item_nohint(&hint->ext_coord.coord,
				    hint->ext_coord.lh, &key,
				    ZNODE_WRITE_LOCK, inode);
	hint->ext_coord.valid == 0
#endif
	if (IS_CBKERR(ret)) {
		reiser4_unset_hint(hint);
		return RETERR(-EIO);
	}
	/* reserve space for data */
	ret = locate_reserve_data(&hint->ext_coord.coord, &key, inode,
				  off, node, &dsubv, flags);
	if (ret) {
		reiser4_unset_hint(hint);
		return ret;
	}
	assert("edward-2284", dsubv != NULL);
	assert("edward-2362",
	       ergo(node->subvol, node->subvol == dsubv));
	/*
	 * Now when we know location of data block, make key precise
	 */
	set_key_ordering(&key, dsubv->id);

	loaded = hint->ext_coord.coord.node;
	ret = zload(loaded);
	if (ret) {
		reiser4_unset_hint(hint);
		return ret;
	}
	if (hint->ext_coord.coord.between == AT_UNIT &&
	    !hint->ext_coord.valid)
		init_coord_extension_extent(&hint->ext_coord,
					    get_key_offset(&key));
	/*
	 * This will put a new block pointer to the tree,
	 * if there were no such ones
	 */
	ret = __update_extent(&hint->ext_coord, &key, node, dsubv);
	zrelse(loaded);
	if (ret) {
		reiser4_unset_hint(hint);
		return ret;
	}
	if (hint->ext_coord.valid == 0) {
		/*
		 * a new block pointer was put,
		 * update coord respectively
		 */
		hint->ext_coord.coord.between = AT_UNIT;

		loaded = hint->lh.node;
		ret = zload(loaded);
		if (unlikely(ret)) {
			reiser4_unset_hint(hint);
			return ret;
		}
		init_coord_extension_extent(&hint->ext_coord,
					    get_key_offset(&key));
		zrelse(loaded);
	}
	assert("edward-2478", hint->ext_coord.valid);
	reiser4_set_hint(hint, &key, ZNODE_WRITE_LOCK);
	return 0;
}

int update_extent_stripe(struct page *page, struct hint *hint, unsigned flags)
{
	int ret;
	struct inode *inode;
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

	ret = update_extent(hint, inode, node, flags);
	JF_CLR(node, JNODE_WRITE_PREPARED);
	if (ret) {
		if (ret != -ENOSPC)
			warning("edward-1549",
				"failed to update extent (%d)", ret);
		jput(node);
		return ret;
	}
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
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * scroll-step: 1
 * End:
 */
