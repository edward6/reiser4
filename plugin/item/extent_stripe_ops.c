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

void check_uf_coord(const uf_coord_t *uf_coord, const reiser4_key *key);
void check_jnodes(znode *twig, const reiser4_key *key, int count);

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
static int plug_hole_stripe(uf_coord_t *uf_coord, const reiser4_key *key)
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
		zrelse(twig_node);

		return 0;
	}
	/*
	 * We are on the twig level.
	 * Try to push to the extent item pointed out by @coord
	 */
	assert("edward-2057", item_is_extent(coord));

	if (!keyeq(key, append_key_extent(coord, &akey))) {
		/*
		 * can not push. Create a new item
		 */
		reiser4_set_extent(subvol_by_key(key), &new_ext,
				   UNALLOCATED_EXTENT_START, 1);
		init_new_extent(EXTENT41_POINTER_ID, &idata, &new_ext, 1);
		ret = insert_by_coord(coord, &idata, key, uf_coord->lh, 0);
	} else {
		/*
		 * can push. Paste at the end of item
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
				      jnode **jnodes, int count)
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
	reiser4_set_extent(subvol_by_key(key), &new_ext,
			   UNALLOCATED_EXTENT_START, count);
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
	check_jnodes(uf_coord->lh->node, key, count);
	/*
	 * assign fake block numbers to all jnodes, capture and mark them dirty
	 */
	subv = current_origin(get_key_ordering(key));

	result = check_insert_atom_brick_info(subv->id, &abi);
	if (result)
		return result;

	block = fake_blocknr_unformatted(count, subv);
	for (i = 0; i < count; i ++, block ++) {
		node = jnodes[i];
		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);

		jnode_set_subvol(node, subv);
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
static int append_extent_stripe(uf_coord_t *uf_coord, const reiser4_key *key,
				jnode **jnodes,	int count)
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
						  jnodes, count);
	/*
	 * try to merge with the file body's last item
	 * If impossible, then create a new item
	 */
	append_key_extent(coord, &akey);
	if (!keyeq(&akey, key))
		/*
		 * can not merge
		 */
		goto create;
	/*
	 * can merge with the last item
	 */
	assert("edward-2268",
	       subvol_by_key(key) == find_data_subvol(coord));

	ext = extent_by_coord(coord);
	switch (state_of_extent(ext)) {
	case UNALLOCATED_EXTENT:
		/*
		 * fast paste without carry
		 */
		extent_set_width(subvol_by_key(key), ext,
				 extent_get_width(ext) + count);
		znode_make_dirty(coord->node);
		goto process_jnodes;
	case ALLOCATED_EXTENT:
		/*
		 * paste with possible carry
		 */
		reiser4_set_extent(subvol_by_key(key), &new_ext,
				   UNALLOCATED_EXTENT_START, count);
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
	reiser4_set_extent(subvol_by_key(key), &new_ext,
			   UNALLOCATED_EXTENT_START, count);
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
	check_jnodes(uf_coord->lh->node, key, count);
	/*
	 * assign fake block numbers to all jnodes. FIXME: make sure whether
	 * twig node containing inserted extent item is locked
	 */
	subv = current_origin(get_key_ordering(key));

	result = check_insert_atom_brick_info(subv->id, &abi);
	if (result)
		return result;

	for (i = 0; i < count; i ++) {
		node = jnodes[i];
		block = fake_blocknr_unformatted(1, subv);
		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);

		jnode_set_subvol(node, subv);
		jnode_set_block(node, &block);

		result = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(result != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	return count;
}

static int process_one_block(uf_coord_t *uf_coord, const reiser4_key *key,
			     jnode *node, int *hole_plugged)
{
	reiser4_block_nr block;

	if (uf_coord->coord.between != AT_UNIT) {
		/*
		 * there is no item containing @key in the tree
		 */
		int result;
		reiser4_subvol *subv;

		uf_coord->valid = 0;
		inode_add_blocks(mapping_jnode(node)->host, 1);
		result = plug_hole_stripe(uf_coord, key);
		if (result)
			return result;
		subv = current_origin(get_key_ordering(key));
		block = fake_blocknr_unformatted(1, subv);
		if (hole_plugged)
			*hole_plugged = 1;
		JF_SET(node, JNODE_CREATED);
		if (node->subvol == NULL)
			jnode_set_subvol(node, subv);
		else
			assert("edward-2219", node->subvol == subv);
	} else {
		reiser4_extent *ext;
		struct extent_coord_extension *ext_coord;

		assert("edward-2220",
		       jnode_get_subvol(node) ==
		       find_data_subvol(&uf_coord->coord));
		assert("edward-2221",
		       jnode_get_subvol(node)->id ==
		       get_key_ordering(key));

		ext_coord = ext_coord_by_uf_coord(uf_coord);
		check_uf_coord(uf_coord, NULL);
		ext = (reiser4_extent *)(zdata(uf_coord->coord.node) +
					 uf_coord->extension.extent.ext_offset);
		if (state_of_extent(ext) != ALLOCATED_EXTENT)
			return RETERR(-EIO);

		block = extent_get_start(ext) + ext_coord->pos_in_unit;
	}
	jnode_set_block(node, &block);
	return 0;
}

int process_extent_stripe(uf_coord_t *uf_coord, const reiser4_key *key,
			  jnode **jnodes, int *plugged_hole)
{
	int ret;
	jnode *node = jnodes[0];
	struct atom_brick_info *abi;

	ret = check_insert_atom_brick_info(get_key_ordering(key), &abi);
	if (ret)
		return ret;

	if (*jnode_get_block(node) == 0) {
		ret = process_one_block(uf_coord, key, node, plugged_hole);
		if (ret)
			return ret;
	}
	/*
	 * make sure that locked twig node contains
	 * all jnodes we are about to capture
	 */
	check_jnodes(uf_coord->lh->node, key, 1);

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

/**
 * Check that disk space reservation made by grab_data_blocks(), or
 * grab_data_block_reserved() at the beginning of regular file operation
 * is still valid, i.e. not spoiled by concurrent volume operation.
 *
 * If reservation is valid, then return 0. Otherwise, return -EAGAIN and
 * set @subv to a brick where additional reservation should be made.
 * Besides, if that additional reservation should be made for the new
 * volume configuration, then set @case_id to 1. Otherwise set it to 0.
 *
 * Pre-condition: lognterm lock is held at the position where the extent(s)
 * are supposed to be updated.
 */
static int validate_data_reservation(const coord_t *coord, struct inode *inode,
				     loff_t pos, reiser4_subvol **subv,
				     jnode *node, int *case_id)
{
	reiser4_subvol *new;

	assert("edward-2279", get_current_csi()->data_subv != NULL);

	new = inode_file_plugin(inode)->calc_data_subvol(inode, pos);

	if (get_current_csi()->data_subv != new) {
		/*
		 * space was reseved for old configuration,
		 * but we also need to reserve for the new one.
		 */
		get_current_csi()->data_subv = NULL;
		*case_id = 1;
		*subv = new;
		return -EAGAIN;
	} else if (node->subvol && node->subvol != new) {
		/*
		 * space was reserved for new configuration,
		 * but we also need to reserve for the old one.
		 */
		assert("edward-2325",
		       WITH_DATA(coord->node,
				 coord_is_existing_item(coord) &&
				 find_data_subvol(coord) == node->subvol));
		*case_id = 0;
		*subv = node->subvol; /* old */
		return -EAGAIN;
	}
	return 0;
}

/**
 * Handle possible races with concurrent volume operations.
 * Pre-condition: read_dist_lock is held.
 */
int fix_data_reservation(const coord_t *coord, lock_handle *lh,
			 struct inode *inode, loff_t pos, jnode *node,
			 int count, int truncate)
{
	int ret;
	int new;
	reiser4_subvol *dsubv;

	ret = validate_data_reservation(coord, inode, pos, &dsubv, node, &new);
	if (likely(ret == 0))
		/* no fixup is needed */
		return 0;

	ON_DEBUG(notice("edward-2327",
			"Handling races with volume ops (case %d)",
			new));
	/*
	 * Disk space reservation, we made earlier for data,
	 * lost its actuality. Make reservation on brick @dsubv.
	 * Try to do it the fast way under the longterm lock
	 */
	grab_space_enable();
	if (truncate) {
		/* grab from reserved area */
		assert("edward-2275", current ==
		       get_current_super_private()->delete_mutex_owner);

		ret = reiser4_grab_reserved(reiser4_get_current_sb(),
					    count, 0, dsubv);
	} else
		/* grab from regular area */
		ret = reiser4_grab_space(count, 0, dsubv);
	if (!ret)
		goto fixed;
	/*
	 * Our attempt to make reservation by the fast way failed.
	 * Try to grab space more agressively (by committing transactions).
	 * For this we need to release the longterm lock.
	 */
	done_lh(lh);
	grab_space_enable();
	if (truncate) {
		/* grab from reserved area */
		assert("edward-2330", current ==
		       get_current_super_private()->delete_mutex_owner);

		ret = reiser4_grab_reserved(reiser4_get_current_sb(),
					    count, BA_CAN_COMMIT, dsubv);
	} else
		/*
		 * grab from regular area
		 */
		ret = reiser4_grab_space(count, BA_CAN_COMMIT, dsubv);
	if (ret)
		return ret;
	ret = -EAGAIN;
 fixed:
	if (new)
		/* update hint */
		set_current_data_subvol(dsubv);
	return ret;
}

/**
 * Update file body after writing @count blocks at offset @pos.
 * Return 0 on success.
 */
static int __update_extents_stripe(struct hint *hint, struct inode *inode,
				   jnode **jnodes, int count, loff_t pos,
				   int *plugged_hole, int truncate)
{
	int ret = 0;
	reiser4_key key;
	int fixed = 0;

	if (!count)
		/* expanding truncate - nothing to do */
		goto out;
	assert("edward-2323", ergo(truncate != 0, count == 1));

	pos = (loff_t)index_jnode(jnodes[0]) << PAGE_SHIFT;
	/*
	 * construct non-precise key
	 */
	build_body_key_stripe(inode, pos, &key);
	do {
		znode *loaded;

		ret = find_file_item_nohint(&hint->ext_coord.coord,
					    hint->ext_coord.lh, &key,
					    ZNODE_WRITE_LOCK, inode);
		if (IS_CBKERR(ret))
			return -EIO;

		if (current_dist_plug()->v.fix != NULL && !fixed) {
			/*
			 * Fix up disk space reservation for data.
			 * Distribution lock (per-volume, or per-file)
			 * should be held
			 */
			ret = current_dist_plug()->v.fix(&hint->ext_coord.coord,
							 hint->ext_coord.lh,
							 inode, pos, jnodes[0],
							 count, truncate);
			if (ret == -EAGAIN) {
				/* Repeat with the same key */
				fixed = 1;
				continue;
			} else if (ret)
				return ret;
		}
		/*
		 * Longterm lock is held.
		 * Reset fixup status for next iteration
		 */
		fixed = 0;
		assert("edward-2284", get_current_data_subvol() != NULL);
		/*
		 * after making sure that destination data brick is valid,
		 * make key precise
		 */
		set_key_ordering(&key, get_current_data_subvol()->id);

		ret = zload(hint->ext_coord.coord.node);
		BUG_ON(ret != 0);
		loaded = hint->ext_coord.coord.node;
		check_node(loaded);

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
		check_node(hint->ext_coord.lh->node);

		jnodes += ret;
		count -= ret;
		pos += ret * PAGE_SIZE;
		/*
		 * Update key for the next iteration.
		 * We don't know its ordering component, so set maximal value.
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
 out:
	clear_current_data_subvol();
	return ret;
}

int update_extents_stripe(struct file *file, struct inode *inode,
				 jnode **jnodes, int count, loff_t pos)
{
	int ret;
	struct hint hint;

	assert("edward-2063", reiser4_lock_counters()->d_refs == 0);

	ret = load_file_hint(file, &hint);
	if (ret)
		return ret;
	ret = __update_extents_stripe(&hint, inode, jnodes, count, pos,
				      NULL, 0);
	assert("edward-2065",  reiser4_lock_counters()->d_refs == 0);
	return ret;
}

int update_extent_stripe(struct inode *inode, jnode *node, loff_t pos,
			 int *plugged_hole, int truncate)
{
	int ret;
	struct hint hint;

	assert("edward-2066", pos == (loff_t)index_jnode(node) << PAGE_SHIFT);

	hint_init_zero(&hint);

	ret = __update_extents_stripe(&hint, inode, &node, 1,
				      pos, plugged_hole, truncate);

	assert("edward-2067", ret == 1 || ret < 0);
	return (ret == 1) ? 0 : ret;
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
