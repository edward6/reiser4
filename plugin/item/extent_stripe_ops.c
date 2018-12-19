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

void check_uf_coord(const uf_coord_t *uf_coord, const reiser4_key *key);
void check_jnodes(struct inode *inode, znode *twig, const reiser4_key *key,
		  int count);
int overwrite_extent_generic(struct inode *inode, uf_coord_t *uf_coord,
			     const reiser4_key *key, jnode **jnodes,
			     int count, int *plugged_hole,
			     int(*overwrite_one_block_fn)(struct inode *inode,
							  uf_coord_t *uf_coord,
							  const reiser4_key *key,
							  jnode *node,
							  int *hole_plugged));
ssize_t write_extent_generic(struct file *file, struct inode *inode,
			     const char __user *buf, size_t count,
			     loff_t *pos,
			     int(*readpage_fn)(struct file *file,
					       struct page *page),
			     int(*update_fn)(struct file *file,
					     struct inode *inode,
					     jnode **jnodes,
					     int count,
					     loff_t pos));
#if REISER4_DEBUG
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

static void check_uf_coord_stripe(const uf_coord_t *uf_coord,
				  const reiser4_key *key)
{
#if REISER4_DEBUG
	const coord_t *coord;
	reiser4_extent *ext;
	reiser4_key coord_key;

	coord = &uf_coord->coord;
	unit_key_by_coord(coord, &coord_key);
	ext = (reiser4_extent *)(zdata(coord->node) +
				 uf_coord->extension.extent.ext_offset);
	check_uf_coord(uf_coord, key);
	if (current_stripe_bits) {
		/*
		 * make sure that extent doesn't contain stripe boundary
		 */
		const coord_t *coord;
		reiser4_extent *ext;
		reiser4_key coord_key;

		coord = &uf_coord->coord;
		unit_key_by_coord(coord, &coord_key);
		ext = (reiser4_extent *)(zdata(coord->node) +
					 uf_coord->extension.extent.ext_offset);
		assert("edward-1815",
		       get_key_offset(&coord_key) >> current_stripe_bits ==
		       ((get_key_offset(&coord_key) +
			 (extent_get_width(ext) << PAGE_SHIFT) - 1) >>
			current_stripe_bits));
	}
#endif
}

/**
 * Hole in a striped file is not represented by any items,
 * so this function actually does nothing
 */
static int append_hole_stripe(uf_coord_t *uf_coord)
{
	assert("edward-2051", uf_coord->coord.between != AT_UNIT);
	uf_coord->valid = 0;
	return 0;
}

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
 * Push a pointer to one unallocated block to a hole in a
 * striped file. Location to push is determined by @uf_coord.
 * First try to push to an existing item at the left (if any).
 * If impossible, then create a new extent item and try to
 * merge it with an item at the right.
 * NOTE: the procedure of hole plugging can lead to appearing
 * mergeable items in a node. We can not leave them unmerged,
 * as it will be treated as corruption.
 */
static int plug_hole_stripe(struct inode *inode,
			    uf_coord_t *uf_coord, const reiser4_key *key)
{
	int ret = 0;
	reiser4_key akey;
	coord_t *coord = &uf_coord->coord;
	reiser4_extent *ext;
	reiser4_extent new_ext;
	reiser4_item_data idata;

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

		reiser4_set_extent(&new_ext,
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
		zrelse(twig_node);

		return 0;
	}
	/*
	 * We are on the twig level.
	 * Try to push to the extent item pointed out by @coord
	 */
	assert("edward-2057", item_is_extent(coord));

	if (!keyeq(key, append_key_extent(inode, coord, &akey))) {
		/*
		 * can not push. Create a new item
		 */
		reiser4_set_extent(&new_ext, UNALLOCATED_EXTENT_START, 1);
		init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
		ret = insert_by_coord(coord, &idata, key, uf_coord->lh, 0);
	} else {
		/*
		 * can push. Paste at the end of item
		 */
		coord->unit_pos = coord_last_unit_pos(coord);
		ext = extent_by_coord(coord);
		if ((state_of_extent(ext) == UNALLOCATED_EXTENT)) {
			/*
			 * fast paste without carry
			 */
			extent_set_width(ext, extent_get_width(ext) + 1);
			znode_make_dirty(coord->node);
		} else {
			/*
			 * paste with possible carry
			 */
			coord->between = AFTER_UNIT;
			reiser4_set_extent(&new_ext,
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
	 * in this branch @coord gets updated by insertion
	 * primitives, so there is no need to find insertion
	 * point, just try to merge right away
	 */
	ret = zload(coord->node);
	if (ret)
		return ret;
	try_merge_with_right_item(coord);
	zrelse(coord->node);

	return 0;
}

static int insert_first_extent_stripe(uf_coord_t *uf_coord,
				      const reiser4_key *key,
				      jnode **jnodes, int count,
				      struct inode *inode)
{
	int result;
	int i;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	reiser4_block_nr block;
	jnode *node;
	reiser4_subvol *subv;
	struct atom_brick_info *abi;

	/* first extent insertion starts at leaf level */
	assert("edward-2059",
	       znode_get_level(uf_coord->coord.node) == LEAF_LEVEL);
	assert("edward-2060", coord_is_between_items(&uf_coord->coord));

	if (count == 0)
		return 0;
	inode_add_blocks(mapping_jnode(jnodes[0])->host, count);
	/*
	 * prepare for tree modification: compose body of item and item data
	 * structure needed for insertion
	 */
	reiser4_set_extent(&new_ext, UNALLOCATED_EXTENT_START, count);
	init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);

	/* insert extent item into the tree */
	result = insert_extent_by_coord(&uf_coord->coord, &idata, key,
					uf_coord->lh);
	if (result)
		return result;

	/*
	 * make sure that we hold long term locked twig node containing all
	 * jnodes we are about to capture
	 */
	check_jnodes(inode, uf_coord->lh->node, key, count);
	/*
	 * assign fake block numbers to all jnodes, capture and mark them dirty
	 */
	subv = calc_data_subvol(inode, get_key_offset(key));

	result = check_insert_atom_brick_info(subv->id, &abi);
	if (result)
		return result;

	block = fake_blocknr_unformatted(count, subv);
	for (i = 0; i < count; i ++, block ++) {
		node = jnodes[i];
		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);

		assert("edward-1934", node->subvol == subv);
		//node->subvol = subv;

		jnode_set_block(node, &block);
		result = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(result != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	/*
	 * invalidate coordinate, research must be performed to continue
	 * because write will continue on twig level
	 */
	uf_coord->valid = 0;
	return count;
}

/*
 * put a pointer to the last (in logical order) @count unallocated
 * unformatted blocks at the position determined by @uf_coord.
 */
static int append_extent_stripe(struct inode *inode, uf_coord_t *uf_coord,
				const reiser4_key *key, jnode **jnodes,
				int count)
{
	int i;
	int result;
	jnode *node;
	reiser4_block_nr block;
	reiser4_key akey;
	coord_t *coord = &uf_coord->coord;
	reiser4_extent *ext;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	reiser4_subvol *subv;
	struct atom_brick_info *abi;

	assert("edward-2061", coord->between != AT_UNIT);

	uf_coord->valid = 0;
	if (coord->between != AFTER_UNIT)
		/*
		 * there is no file items on the left
		 */
		return insert_first_extent_stripe(uf_coord, key,
						  jnodes, count, inode);
	/*
	 * try to merge with the file body's last item
	 * If impossible, then create a new item
	 */
	append_key_extent(inode, coord, &akey);
	if (!keyeq(&akey, key))
		/*
		 * can not merge
		 */
		goto create;
	/*
	 * can merge with the last item
	 */
	ext = extent_by_coord(coord);
	switch (state_of_extent(ext)) {
	case UNALLOCATED_EXTENT:
		/*
		 * fast paste without carry
		 */
		extent_set_width(ext, extent_get_width(ext) + count);
		znode_make_dirty(coord->node);
		goto process_jnodes;
	case ALLOCATED_EXTENT:
		/*
		 * paste with possible carry
		 */
		reiser4_set_extent(&new_ext, UNALLOCATED_EXTENT_START, count);
		init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
		result = insert_into_item(coord, uf_coord->lh, key, &idata, 0);
		if (result)
			return result;
		goto process_jnodes;
	default:
		return RETERR(-EIO);
	}
 create:
	/* create a new item */
	reiser4_set_extent(&new_ext, UNALLOCATED_EXTENT_START, count);
	init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
	result = insert_by_coord(coord, &idata, key, uf_coord->lh, 0);
	if (result)
		return result;
 process_jnodes:
	assert("edward-2062",
	       get_key_offset(key) ==
	       (loff_t)index_jnode(jnodes[0]) * PAGE_SIZE);
	inode_add_blocks(mapping_jnode(jnodes[0])->host, count);
	/*
	 * make sure that we hold long term locked twig node
	 * containing all jnodes we are about to capture
	 */
	check_jnodes(inode, uf_coord->lh->node, key, count);
	/*
	 * assign fake block numbers to all jnodes. FIXME: make sure whether
	 * twig node containing inserted extent item is locked
	 *
	 * FIXME-EDWARD: replace calc_data_subvol() with find_data_subvol():
	 * subvolums should be found by existing, or newly created extent.
	 */
	subv = calc_data_subvol(inode, get_key_offset(key));

	result = check_insert_atom_brick_info(subv->id, &abi);
	if (result)
		return result;

	for (i = 0; i < count; i ++) {
		node = jnodes[i];
		block = fake_blocknr_unformatted(1, subv);
		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);

		assert("edward-1954", node->subvol == subv);
		//node->subvol = subv;

		jnode_set_block(node, &block);
		result = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(result != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	return count;
}

/*
 * Overwrite one logical block of file's body. It can be in a hole,
 * which is not represented by any items.
 */
static int overwrite_one_block_stripe(struct inode *inode, uf_coord_t *uf_coord,
				      const reiser4_key *key, jnode *node,
				      int *hole_plugged)
{
	reiser4_block_nr block;

	if (uf_coord->coord.between != AT_UNIT) {
		/*
		 * the case of hole
		 */
		int result;
		reiser4_subvol *subv = node->subvol;

		assert("edward-1784",
		       subv == calc_data_subvol(inode, get_key_offset(key)));

		uf_coord->valid = 0;
		inode_add_blocks(mapping_jnode(node)->host, 1);
		result = plug_hole_stripe(inode, uf_coord, key);
		if (result)
			return result;
		block = fake_blocknr_unformatted(1, subv);
		if (hole_plugged)
			*hole_plugged = 1;
		JF_SET(node, JNODE_CREATED);
	} else {
		reiser4_extent *ext;
		struct extent_coord_extension *ext_coord;

		ext_coord = ext_coord_by_uf_coord(uf_coord);
		check_uf_coord_stripe(uf_coord, NULL);
		ext = (reiser4_extent *)(zdata(uf_coord->coord.node) +
					 uf_coord->extension.extent.ext_offset);
		if (state_of_extent(ext) != ALLOCATED_EXTENT)
			return RETERR(-EIO);

		block = extent_get_start(ext) + ext_coord->pos_in_unit;
	}
	jnode_set_block(node, &block);
	return 0;
}

static int overwrite_extent_stripe(struct inode *inode, uf_coord_t *uf_coord,
				   const reiser4_key *key, jnode **jnodes,
				   int count, int *plugged_hole)
{
	if (uf_coord->coord.between == AT_UNIT)
		init_coord_extension_extent(uf_coord, get_key_offset(key));

	return overwrite_extent_generic(inode, uf_coord, key,
					jnodes, count, plugged_hole,
					overwrite_one_block_stripe);
}

/*
 * update body of a striped file on write or expanding truncate operations
 */
static int __update_extents_stripe(struct hint *hint, struct inode *inode,
				   jnode **jnodes, int count, loff_t pos,
				   int *plugged_hole)
{
	int result;
	reiser4_key key;

	if (count != 0)
		/*
		 * count == 0 is special case: expanding truncate
		 */
		pos = (loff_t)index_jnode(jnodes[0]) << PAGE_SHIFT;

	build_body_key_stripe(inode, pos, &key, WRITE_OP);
	do {
		znode *loaded;
		const char *error;

		result = find_file_item_nohint(&hint->ext_coord.coord,
					       hint->ext_coord.lh, &key,
					       ZNODE_WRITE_LOCK, inode);
		if (IS_CBKERR(result))
			return result;

		result = zload(hint->ext_coord.coord.node);
		BUG_ON(result != 0);
		loaded = hint->ext_coord.coord.node;
		check_node(loaded);

		if (pos > round_up(i_size_read(inode), PAGE_SIZE))
			result = append_hole_stripe(&hint->ext_coord);
		else if (pos == round_up(i_size_read(inode), PAGE_SIZE))
			result = append_extent_stripe(inode, &hint->ext_coord,
						      &key, jnodes, count);
		else
			result = overwrite_extent_stripe(inode,
							 &hint->ext_coord,
							 &key, jnodes, count,
							 plugged_hole);
		//check_node(hint->ext_coord.lh->node);

		zload(hint->ext_coord.lh->node);
		assert("edward-2105",
		       check_node40(hint->ext_coord.lh->node,
				    REISER4_NODE_TREE_STABLE, &error) == 0);
		zrelse(hint->ext_coord.lh->node);

		zrelse(loaded);
		if (result < 0) {
			done_lh(hint->ext_coord.lh);
			break;
		}
		jnodes += result;
		count -= result;
		pos += result * PAGE_SIZE;

		build_body_key_stripe(inode, pos, &key, WRITE_OP);
		/*
		 * seal and unlock znode
		 */
		if (hint->ext_coord.valid)
			reiser4_set_hint(hint, &key, ZNODE_WRITE_LOCK);
		else
			reiser4_unset_hint(hint);

		if (count && i_size_read(inode) < get_key_offset(&key))
			/*
			 * update file size to not mistakenly
			 * detect a hole in the next iteration
			 */
			INODE_SET_FIELD(inode, i_size, pos);
	} while (count > 0);

	return result;
}

static int update_extents_stripe(struct file *file, struct inode *inode,
				 jnode **jnodes, int count, loff_t pos)
{
	int ret;
	struct hint hint;

	assert("edward-2063", reiser4_lock_counters()->d_refs == 0);

	ret = load_file_hint(file, &hint);
	if (ret)
		return ret;
	ret =  __update_extents_stripe(&hint, inode, jnodes, count, pos,
				       NULL);
	save_file_hint(file, &hint);
	assert("edward-2065",  reiser4_lock_counters()->d_refs == 0);
	return ret;
}

int update_extent_stripe(struct inode *inode, jnode *node, loff_t pos,
			 int *plugged_hole)
{
	int ret;
	struct hint hint;

	assert("edward-2066", pos == (loff_t)index_jnode(node) << PAGE_SHIFT);

	hint_init_zero(&hint);

	ret = __update_extents_stripe(&hint, inode, &node, 1,
				      pos, plugged_hole);

	assert("edward-2067", ret == 1 || ret < 0);
	return (ret == 1) ? 0 : ret;
}

ssize_t write_extent_stripe(struct file *file, struct inode *inode,
			    const char __user *buf, size_t count,
			    loff_t *pos)
{
	if (current_stripe_bits) {
		/*
		 * write data of only one stripe
		 */
		int to_stripe = current_stripe_size -
			(*pos & (current_stripe_size - 1));
		if (count > to_stripe)
			count = to_stripe;
	}
	return write_extent_generic(file, inode, buf, count, pos,
				    readpage_stripe, update_extents_stripe);
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
