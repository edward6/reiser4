/* COPYRIGHT 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../object.h"
#include "../volume/volume.h"
#include <linux/swap.h>

static inline reiser4_extent *ext_by_offset(const znode *node, int offset)
{
	reiser4_extent *ext;

	ext = (reiser4_extent *) (zdata(node) + offset);
	return ext;
}

static inline reiser4_extent *ext_by_ext_coord(const uf_coord_t *uf_coord)
{
	return ext_by_offset(uf_coord->coord.node,
			     uf_coord->extension.extent.ext_offset);
}

/**
 * verify coord extension @uf_coord against @key:
 *
 * Makes sure that all fields of @uf_coord are set properly.
 * If @key is specified - check whether @uf_coord is set correspondingly.
 */
void check_uf_coord(const uf_coord_t *uf_coord, const reiser4_key *key)
{
#if REISER4_DEBUG
	const coord_t *coord;
	const struct extent_coord_extension *ext_coord;
	reiser4_extent *ext;
	reiser4_key coord_key;

	coord = &uf_coord->coord;
	unit_key_by_coord(coord, &coord_key);

	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_offset(coord->node, uf_coord->extension.extent.ext_offset);

	assert("edward-2047",
	       WITH_DATA(coord->node,
			 (uf_coord->valid == 1 &&
			  coord_is_iplug_set(coord) &&
			  item_is_extent(coord) &&
			  ext_coord->nr_units == nr_units_extent(coord) &&
			  ext == extent_by_coord(coord) &&
			  ext_coord->width == extent_get_width(ext) &&
			  coord->unit_pos < ext_coord->nr_units &&
			  ext_coord->pos_in_unit < ext_coord->width &&
			  memcmp(ext, &ext_coord->extent,
				 sizeof(reiser4_extent)) == 0)));
	if (key) {
		set_key_offset(&coord_key,
			       get_key_offset(&coord_key) +
			       (uf_coord->extension.extent.
				pos_in_unit << PAGE_SHIFT));
		set_key_ordering(&coord_key, get_key_ordering(key));
		assert("edward-2326", keyeq(key, &coord_key));
	}
#endif
}

#if REISER4_DEBUG

/**
 * return 1 if offset @off is inside of extent unit pointed to by @coord.
 * Set pos_in_unit inside of unit correspondingly
 */
static int offset_is_in_unit(const coord_t *coord, loff_t off)
{
	reiser4_key unit_key;
	__u64 unit_off;
	reiser4_extent *ext;

	ext = extent_by_coord(coord);

	unit_key_extent(coord, &unit_key);
	unit_off = get_key_offset(&unit_key);
	if (off < unit_off)
		return 0;
	if (off >= (unit_off + (current_blocksize * extent_get_width(ext))))
		return 0;
	return 1;
}

static int coord_matches_key_extent(struct inode *inode,
				    const coord_t *coord,
				    const reiser4_key *key)
{
	reiser4_key item_key;
	item_plugin *iplug;

	assert("vs-771", coord_is_existing_unit(coord));
	assert("edward-2090", item_is_extent(coord));

	iplug = item_plugin_by_coord(coord);
	/*
	 * check that in simple volumes logical order coincides with
	 * physical order
	 */
	assert("vs-1258",
	       ergo(current_vol_plug() ==
		    volume_plugin_by_id(SIMPLE_VOLUME_ID),
		    keylt(key, iplug->s.file.append_key(coord, &item_key))));
	assert("vs-1259",
	       ergo(current_vol_plug() ==
		    volume_plugin_by_id(SIMPLE_VOLUME_ID),
		    keyge(key, item_key_by_coord(coord, &item_key))));

	return offset_is_in_unit(coord, get_key_offset(key));
}
#endif

static int can_append(const reiser4_key *key, const coord_t *coord)
{
	reiser4_key append_key;

	return keyeq(key, append_key_extent(coord, &append_key));
}

static int append_hole_unix_file(coord_t *coord, lock_handle *lh,
				 const reiser4_key *key)
{
	reiser4_key append_key;
	reiser4_block_nr hole_width;
	reiser4_extent *ext, new_ext;
	reiser4_item_data idata;

	/* last item of file may have to be appended with hole */
	assert("vs-708", znode_get_level(coord->node) == TWIG_LEVEL);
	assert("vs-714", item_id_by_coord(coord) == EXTENT40_POINTER_ID);
	/*
	 * construct key of first byte which is not addressed by the
	 * last extent
	 */
	append_key_extent(coord, &append_key);
	assert("edward-2324", keyle(&append_key, key));
	/*
	 * extent item has to be appended with hole. Calculate length of that
	 * hole
	 */
	hole_width = ((get_key_offset(key) - get_key_offset(&append_key) +
		       current_blocksize - 1) >> current_blocksize_bits);
	assert("vs-954", hole_width > 0);

	/* set coord after last unit */
	coord_init_after_item_end(coord);

	/* get last extent in the item */
	ext = extent_by_coord(coord);
	if (state_of_extent(ext) == HOLE_EXTENT) {
		/*
		 * last extent of a file is hole extent. Widen that extent by
		 * @hole_width blocks. Note that we do not worry about
		 * overflowing - extent width is 64 bits
		 */
		reiser4_set_extent(get_meta_subvol(), ext, HOLE_EXTENT_START,
				   extent_get_width(ext) + hole_width);
		znode_make_dirty(coord->node);
		return 0;
	}
	/*
	 * append last item of the file with hole extent unit
	 */
	assert("vs-713", (state_of_extent(ext) == ALLOCATED_EXTENT ||
			  state_of_extent(ext) == UNALLOCATED_EXTENT));

	reiser4_set_extent(get_meta_subvol(), &new_ext,
			   HOLE_EXTENT_START, hole_width);
	init_new_extent(EXTENT40_POINTER_ID, &idata, &new_ext, 1);
	return insert_into_item(coord, lh, &append_key, &idata, 0);
}

/**
 * @twig: longterm locked twig node
 */
void check_jnodes(znode *twig, const reiser4_key *key, int count)
{
	coord_t coord;
	reiser4_key node_key, jnode_key;

	if (current_vol_plug() != volume_plugin_by_id(SIMPLE_VOLUME_ID))
		return;

	jnode_key = *key;

	assert("edward-2094", twig != NULL);
	assert("edward-2095", znode_get_level(twig) == TWIG_LEVEL);
	assert("edward-2096", znode_is_write_locked(twig));

	zload(twig);
	/* get the smallest key in twig node */
	coord_init_first_unit(&coord, twig);
	assert("edward-2097",
	       item_is_extent(&coord) || item_is_internal(&coord));

	unit_key_by_coord(&coord, &node_key);
	assert("edward-2098", keyle(&node_key, &jnode_key));

	/* get the greatest key in the twig node */
	coord_init_last_unit(&coord, twig);
	assert("edward-2099",
	       item_is_extent(&coord) || item_is_internal(&coord));

	unit_key_by_coord(&coord, &node_key);

	if (item_is_extent(&coord))
		item_plugin_by_coord(&coord)->s.file.append_key(&coord,
								&node_key);
	set_key_offset(&jnode_key,
		       get_key_offset(&jnode_key) +
		       (loff_t)count * PAGE_SIZE - 1);
	assert("edward-2100", keylt(&jnode_key, &node_key));
	zrelse(twig);
}

/**
 * append last file item
 * @uf_coord: coord to start insertion from
 * @jnodes: array of jnodes
 * @count: number of jnodes in the array
 *
 * There is already at least one extent item of file @inode in the tree.
 * Append the last of them with unallocated extent unit of width @count.
 * Assign fake block numbers to jnodes corresponding to the inserted extent.
 */
static int append_last_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			      jnode **jnodes, int count)
{
	int result;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	coord_t *coord;
	struct extent_coord_extension *ext_coord;
	reiser4_extent *ext;
	reiser4_block_nr block;
	jnode *node;
	int i;
	struct atom_brick_info *abi;

	coord = &uf_coord->coord;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);

	/* check correctness of position in the item */
	assert("vs-228", coord->unit_pos == coord_last_unit_pos(coord));
	assert("vs-1311", coord->between == AFTER_UNIT);
	assert("vs-1302", ext_coord->pos_in_unit == ext_coord->width - 1);

	if (!can_append(key, coord)) {
		/* hole extent has to be inserted */
		result = append_hole_unix_file(coord, uf_coord->lh, key);
		uf_coord->valid = 0;
		return result;
	}
	if (count == 0)
		return 0;
	assert("", get_key_offset(key) == (loff_t)index_jnode(jnodes[0]) * PAGE_SIZE);

	inode_add_blocks(mapping_jnode(jnodes[0])->host, count);

	switch (state_of_extent(ext)) {
	case UNALLOCATED_EXTENT:
		/*
		 * last extent unit of the file is unallocated one
		 * Increase its width by @count
		 */
		reiser4_set_extent(get_meta_subvol(), ext,
				   UNALLOCATED_EXTENT_START,
				   extent_get_width(ext) + count);
		znode_make_dirty(coord->node);

		/* update coord extension */
		ext_coord->width += count;
		ext_coord->pos_in_unit += count;
		ON_DEBUG(extent_set_width(get_meta_subvol(),
					  &uf_coord->extension.extent.extent,
					  ext_coord->width));
		break;
	case HOLE_EXTENT:
	case ALLOCATED_EXTENT:
		/*
		 * last extent unit of the file is either hole or allocated
		 * one. Append one unallocated extent of width @count
		 */
		reiser4_set_extent(get_meta_subvol(), &new_ext,
				   UNALLOCATED_EXTENT_START, count);
		init_new_extent(EXTENT40_POINTER_ID, &idata, &new_ext, 1);
		result = insert_into_item(coord, uf_coord->lh, key, &idata, 0);
		uf_coord->valid = 0;
		if (result)
			return result;
		break;

	default:
		return RETERR(-EIO);
	}
	/*
	 * make sure that we hold long term locked twig node containing all
	 * jnodes we are about to capture
	 */
	ON_DEBUG(check_jnodes(uf_coord->lh->node, key, count));
	/*
	 * assign fake block numbers to all jnodes. FIXME: make sure whether
	 * twig node containing inserted extent item is locked
	 */
	result = check_insert_atom_brick_info(get_meta_subvol()->id, &abi);
	if (result)
		return result;

	for (i = 0; i < count; i ++) {
		node = jnodes[i];
		block = fake_blocknr_unformatted(1, get_meta_subvol());
		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);

		jnode_set_subvol(node, get_meta_subvol());
		jnode_set_block(node, &block);

 		result = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(result != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);
	}
	return count;
}

static int insert_first_hole(coord_t *coord, lock_handle *lh,
			     const reiser4_key *key)
{
	reiser4_extent new_ext;
	reiser4_item_data idata;
	reiser4_key item_key;
	reiser4_block_nr hole_width;

	/* @coord must be set for inserting of new item */
	assert("vs-711", coord_is_between_items(coord));

	item_key = *key;
	set_key_offset(&item_key, 0ull);

	hole_width = ((get_key_offset(key) + current_blocksize - 1) >>
		      current_blocksize_bits);
	assert("vs-710", hole_width > 0);
	/*
	 * compose body of hole extent and insert item into tree
	 */
	reiser4_set_extent(get_meta_subvol(), &new_ext,
			   HOLE_EXTENT_START, hole_width);
	init_new_extent(EXTENT40_POINTER_ID, &idata, &new_ext, 1);
	return insert_extent_by_coord(coord, &idata, &item_key, lh);
}


/**
 * insert first file item
 * @inode: inode of file
 * @uf_coord: coord to start insertion from
 * @jnodes: array of jnodes
 * @count: number of jnodes in the array
 * @inode:
 *
 * There are no items of file @inode in the tree yet. Insert unallocated extent
 * of width @count into tree or hole extent if writing not to the
 * beginning. Assign fake block numbers to jnodes corresponding to the inserted
 * unallocated extent. Returns number of jnodes or error code.
 */
static int insert_first_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			       jnode **jnodes, int count, struct inode *inode)
{
	int result;
	int i;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	reiser4_block_nr block;
	struct unix_file_info *uf_info;
	jnode *node;
	struct atom_brick_info *abi;

	/* first extent insertion starts at leaf level */
	assert("vs-719", znode_get_level(uf_coord->coord.node) == LEAF_LEVEL);
	assert("vs-711", coord_is_between_items(&uf_coord->coord));

	if (get_key_offset(key) != 0) {
		result = insert_first_hole(&uf_coord->coord, uf_coord->lh, key);
		uf_coord->valid = 0;
		uf_info = unix_file_inode_data(inode);

		/*
		 * first item insertion is only possible when writing to empty
		 * file or performing tail conversion
		 */
		assert("", (uf_info->container == UF_CONTAINER_EMPTY ||
			    (reiser4_inode_get_flag(inode,
						    REISER4_PART_MIXED) &&
			     reiser4_inode_get_flag(inode,
						    REISER4_PART_IN_CONV))));

		/* if file was empty - update its state */
		if (result == 0 && uf_info->container == UF_CONTAINER_EMPTY)
			uf_info->container = UF_CONTAINER_EXTENTS;
		return result;
	}
	if (count == 0)
		return 0;

	inode_add_blocks(mapping_jnode(jnodes[0])->host, count);

	/*
	 * prepare for tree modification: compose body of item and item data
	 * structure needed for insertion
	 */
	reiser4_set_extent(get_meta_subvol(), &new_ext,
			   UNALLOCATED_EXTENT_START, count);
	init_new_extent(EXTENT40_POINTER_ID, &idata, &new_ext, 1);

	/* insert extent item into the tree */
	result = insert_extent_by_coord(&uf_coord->coord, &idata, key,
					uf_coord->lh);
	if (result)
		return result;

	/*
	 * make sure that we hold long term locked twig node containing all
	 * jnodes we are about to capture
	 */
	ON_DEBUG(check_jnodes(uf_coord->lh->node, key, count));
	/*
	 * assign fake block numbers to all jnodes, capture and mark them dirty
	 */
	result = check_insert_atom_brick_info(get_meta_subvol()->id, &abi);
	if (result)
		return result;

	block = fake_blocknr_unformatted(count, get_meta_subvol());
	for (i = 0; i < count; i ++, block ++) {
		node = jnodes[i];
		spin_lock_jnode(node);
		JF_SET(node, JNODE_CREATED);
		/*
		 * unix file plugin stores everything in meta-data brick
		 */
		jnode_set_subvol(node, get_meta_subvol());
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

/**
 * replace hole extent with unallocated and holes
 * @uf_coord:
 * @key:
 * @node:
 * @h: structure containing coordinate, lock handle, key, etc
 *
 * Creates an unallocated extent of width 1 within a hole. In worst case two
 * additional extents can be created.
 */
static int plug_hole(uf_coord_t *uf_coord, const reiser4_key *key, int *how)
{
	struct replace_handle rh;
	reiser4_extent *ext;
	reiser4_block_nr width, pos_in_unit;
	coord_t *coord;
	struct extent_coord_extension *ext_coord;
	int return_inserted_position;

	check_uf_coord(uf_coord, key);

	rh.coord = coord_by_uf_coord(uf_coord);
	rh.lh = uf_coord->lh;
	rh.flags = 0;

	coord = coord_by_uf_coord(uf_coord);
	ext_coord = ext_coord_by_uf_coord(uf_coord);
	ext = ext_by_ext_coord(uf_coord);

	width = ext_coord->width;
	pos_in_unit = ext_coord->pos_in_unit;

	*how = 0;
	if (width == 1) {
		reiser4_set_extent(get_meta_subvol(), ext,
				   UNALLOCATED_EXTENT_START, 1);
		znode_make_dirty(coord->node);
		/* update uf_coord */
		ON_DEBUG(ext_coord->extent = *ext);
		*how = 1;
		return 0;
	} else if (pos_in_unit == 0) {
		/* we deal with first element of extent */
		if (coord->unit_pos) {
			/* there is an extent to the left */
			if (state_of_extent(ext - 1) == UNALLOCATED_EXTENT) {
				/*
				 * left neighboring unit is an unallocated
				 * extent. Increase its width and decrease
				 * width of hole
				 */
				extent_set_width(get_meta_subvol(), ext - 1,
						 extent_get_width(ext - 1) + 1);
				extent_set_width(get_meta_subvol(), ext,
						 width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos--;
				ext_coord->width = extent_get_width(ext - 1);
				ext_coord->pos_in_unit = ext_coord->width - 1;
				ext_coord->ext_offset -= sizeof(reiser4_extent);
				ON_DEBUG(ext_coord->extent =
					 *extent_by_coord(coord));
				*how = 2;
				return 0;
			}
		}
		/* extent for replace */
		reiser4_set_extent(get_meta_subvol(), &rh.overwrite,
				   UNALLOCATED_EXTENT_START, 1);
		/* extent to be inserted */
		reiser4_set_extent(get_meta_subvol(), &rh.new_extents[0],
				   HOLE_EXTENT_START,
				   width - 1);
		rh.nr_new_extents = 1;

		/* have replace_extent_unit() to return with @coord and
		   @uf_coord->lh set to unit which was replaced */
		return_inserted_position = 0;
		*how = 3;
	} else if (pos_in_unit == width - 1) {
		/* we deal with last element of extent */
		if (coord->unit_pos < nr_units_extent(coord) - 1) {
			/* there is an extent unit to the right */
			if (state_of_extent(ext + 1) == UNALLOCATED_EXTENT) {
				/*
				 * right neighboring unit is an unallocated
				 * extent. Increase its width and decrease
				 * width of hole
				 */
				extent_set_width(get_meta_subvol(),
						 ext + 1,
						 extent_get_width(ext + 1) + 1);
				extent_set_width(get_meta_subvol(),
						 ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos++;
				ext_coord->width = extent_get_width(ext + 1);
				ext_coord->pos_in_unit = 0;
				ext_coord->ext_offset += sizeof(reiser4_extent);
				ON_DEBUG(ext_coord->extent =
					 *extent_by_coord(coord));
				*how = 4;
				return 0;
			}
		}
		/* extent for replace */
		reiser4_set_extent(get_meta_subvol(), &rh.overwrite,
				   HOLE_EXTENT_START, width - 1);
		/* extent to be inserted */
		reiser4_set_extent(get_meta_subvol(), &rh.new_extents[0],
				   UNALLOCATED_EXTENT_START, 1);
		rh.nr_new_extents = 1;

		/* have replace_extent_unit() to return with @coord and
		   @uf_coord->lh set to unit which was inserted */
		return_inserted_position = 1;
		*how = 5;
	} else {
		/* extent for replace */
		reiser4_set_extent(get_meta_subvol(), &rh.overwrite,
				   HOLE_EXTENT_START, pos_in_unit);
		/* extents to be inserted */
		reiser4_set_extent(get_meta_subvol(), &rh.new_extents[0],
				   UNALLOCATED_EXTENT_START, 1);
		reiser4_set_extent(get_meta_subvol(), &rh.new_extents[1],
				   HOLE_EXTENT_START, width - pos_in_unit - 1);
		rh.nr_new_extents = 2;

		/* have replace_extent_unit() to return with @coord and
		   @uf_coord->lh set to first of units which were inserted */
		return_inserted_position = 1;
		*how = 6;
	}
	unit_key_by_coord(coord, &rh.paste_key);
	set_key_offset(&rh.paste_key, get_key_offset(&rh.paste_key) +
		       extent_get_width(&rh.overwrite) * current_blocksize);

	uf_coord->valid = 0;
	return replace_extent_unit(EXTENT40_POINTER_ID,
				   &rh, return_inserted_position);
}

/**
 * If @node corresponds to hole extent - create unallocated extent for it and
 * assign fake block number. If @node corresponds to allocated extent - assign
 * block number of jnode
 */
static int overwrite_one_block(uf_coord_t *uf_coord, const reiser4_key *key,
			       jnode *node, int *hole_plugged)
{
	int result;
	struct extent_coord_extension *ext_coord;
	reiser4_extent *ext;
	reiser4_block_nr block;
	int how;

	assert("vs-1312", uf_coord->coord.between == AT_UNIT);

	result = 0;
	ext_coord = ext_coord_by_uf_coord(uf_coord);
	check_uf_coord(uf_coord, NULL);
	ext = ext_by_ext_coord(uf_coord);
	assert("", state_of_extent(ext) != UNALLOCATED_EXTENT);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		assert("edward-2215", node->subvol == get_meta_subvol());

		block = extent_get_start(ext) + ext_coord->pos_in_unit;
		break;

	case HOLE_EXTENT:
		assert("edward-2216", node->subvol == NULL);

		inode_add_blocks(mapping_jnode(node)->host, 1);
		result = plug_hole(uf_coord, key, &how);
		if (result)
			return result;
		block = fake_blocknr_unformatted(1, get_meta_subvol());
		if (hole_plugged)
			*hole_plugged = 1;
		JF_SET(node, JNODE_CREATED);
		jnode_set_subvol(node, get_meta_subvol());
		break;

	default:
		return RETERR(-EIO);
	}

	jnode_set_block(node, &block);
	return 0;
}

/**
 * move_coord - move coordinate forward
 * @uf_coord:
 *
 * Move coordinate one data block pointer forward. Return 1 if coord is set to
 * the last one already or is invalid.
 */
static int move_coord(uf_coord_t *uf_coord)
{
	struct extent_coord_extension *ext_coord;

	if (uf_coord->valid == 0)
		return 1;
	ext_coord = &uf_coord->extension.extent;
	ext_coord->pos_in_unit ++;
	if (ext_coord->pos_in_unit < ext_coord->width)
		/* coordinate moved within the unit */
		return 0;

	/* end of unit is reached. Try to move to next unit */
	ext_coord->pos_in_unit = 0;
	uf_coord->coord.unit_pos ++;
	if (uf_coord->coord.unit_pos < ext_coord->nr_units) {
		/* coordinate moved to next unit */
		ext_coord->ext_offset += sizeof(reiser4_extent);
		ext_coord->width =
			extent_get_width(ext_by_offset
					 (uf_coord->coord.node,
					  ext_coord->ext_offset));
		ON_DEBUG(ext_coord->extent =
			 *ext_by_offset(uf_coord->coord.node,
					ext_coord->ext_offset));
		return 0;
	}
	/* end of item is reached */
	uf_coord->valid = 0;
	return 1;
}

/**
 * Process @count logical blocks of a file.
 * Returns number of handled jnodes.
 */
static int overwrite_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			    jnode **jnodes, int count, int *plugged_hole)
{
	int result;
	reiser4_key k;
	int i;
	jnode *node;
	struct atom_brick_info *abi;
	reiser4_subvol *subv = get_meta_subvol();

	result = check_insert_atom_brick_info(subv->id, &abi);
	if (result)
		return result;

	k = *key;
	for (i = 0; i < count; i ++) {
		node = jnodes[i];
		if (*jnode_get_block(node) == 0) {
			result = overwrite_one_block(uf_coord, &k,
						     node, plugged_hole);
			if (result)
				return result;
		}
		/*
		 * make sure that we hold long term locked twig node containing
		 * all jnodes we are about to capture
		 */
		ON_DEBUG(check_jnodes(uf_coord->lh->node, &k, 1));
		/*
		 * assign fake block numbers to all jnodes, capture and mark
		 * them dirty
		 */
		spin_lock_jnode(node);
		result = reiser4_try_capture(node, ZNODE_WRITE_LOCK, 0);
		BUG_ON(result != 0);
		jnode_make_dirty_locked(node);
		spin_unlock_jnode(node);

		if (uf_coord->valid == 0)
			return i + 1;

		check_uf_coord(uf_coord, &k);

		if (move_coord(uf_coord)) {
			/*
			 * failed to move to the next node pointer. Either end
			 * of file or end of twig node is reached. In the later
			 * case we might go to the right neighbor.
			 */
			uf_coord->valid = 0;
			return i + 1;
		}
		/* update key for next iteration */

		set_key_offset(&k, get_key_offset(&k) + PAGE_SIZE);
	}
	return count;
}

int update_extent_unix_file(struct inode *inode, jnode *node,
			    loff_t pos, int *plugged_hole)
{
	int result;
	znode *loaded;
	uf_coord_t uf_coord;
	coord_t *coord;
	lock_handle lh;
	reiser4_key key;

	assert("", reiser4_lock_counters()->d_refs == 0);

	build_body_key_unix_file(inode, pos, &key);

	init_uf_coord(&uf_coord, &lh);
	coord = &uf_coord.coord;
	result = find_file_item_nohint(coord, &lh, &key,
				       ZNODE_WRITE_LOCK, inode);
	if (IS_CBKERR(result)) {
		assert("", reiser4_lock_counters()->d_refs == 0);
		return result;
	}

	result = zload(coord->node);
	BUG_ON(result != 0);
	loaded = coord->node;

	if (coord->between == AFTER_UNIT) {
		/*
		 * append existing extent item with unallocated extent
		 */
		init_coord_extension_extent(&uf_coord,
					    get_key_offset(&key));
		result = append_last_extent(&uf_coord, &key, &node, 1);
	} else if (coord->between == AT_UNIT) {
		/*
		 * overwrite existing extent
		 * FIXME: not optimal yet. Will be optimized if new
		 * write will show performance win.
		 */
		init_coord_extension_extent(&uf_coord,
					    get_key_offset(&key));
		result = overwrite_extent(&uf_coord, &key,
					  &node, 1, plugged_hole);
	} else {
		/*
		 * there are no items of this file in the tree yet.
		 * Create first item of the file inserting one
		 * unallocated extent
		 */
		result = insert_first_extent(&uf_coord, &key, &node, 1, inode);
	}
	assert("edward-2048", result == 1 || result < 0);

	zrelse(loaded);
	done_lh(&lh);
	assert("edward-2049", reiser4_lock_counters()->d_refs == 0);

	return (result == 1) ? 0 : result;
}

static int update_extents_unix_file(struct file *file, struct inode *inode,
				    jnode **jnodes, int count, loff_t pos)
{
	struct hint hint;
	reiser4_key key;
	int result;
	znode *loaded;

	result = load_file_hint(file, &hint);
	BUG_ON(result != 0);

	if (count != 0)
		/*
		 * count == 0 is special case: expanding truncate
		 */
		pos = (loff_t)index_jnode(jnodes[0]) << PAGE_SHIFT;
	build_body_key_unix_file(inode, pos, &key);

	assert("", reiser4_lock_counters()->d_refs == 0);

	do {
		result = find_file_item(&hint, &key, ZNODE_WRITE_LOCK, inode);
		if (IS_CBKERR(result)) {
			assert("", reiser4_lock_counters()->d_refs == 0);
			return result;
		}

		result = zload(hint.ext_coord.coord.node);
		BUG_ON(result != 0);
		loaded = hint.ext_coord.coord.node;

		if (hint.ext_coord.coord.between == AFTER_UNIT) {
			/*
			 * append existing extent item with unallocated extent
			 * of width nr_jnodes
			 */
			if (hint.ext_coord.valid == 0)
				/* NOTE: get statistics on this */
				init_coord_extension_extent(&hint.ext_coord,
							  get_key_offset(&key));
			result = append_last_extent(&hint.ext_coord,
						    &key, jnodes, count);
		} else if (hint.ext_coord.coord.between == AT_UNIT) {
			/*
			 * overwrite
			 * not optimal yet. Will be optimized if new write will
			 * show performance win.
			 */
			if (hint.ext_coord.valid == 0)
				/* NOTE: get statistics on this */
				init_coord_extension_extent(&hint.ext_coord,
							  get_key_offset(&key));
			result = overwrite_extent(&hint.ext_coord, &key,
						  jnodes, count, NULL);
		} else {
			/*
			 * there are no items of this file in the tree
			 * yet. Create first item of the file inserting one
			 * unallocated extent of * width nr_jnodes
			 */
			result = insert_first_extent(&hint.ext_coord, &key,
						     jnodes, count, inode);
		}
		zrelse(loaded);
		if (result < 0) {
			done_lh(hint.ext_coord.lh);
			break;
		}

		jnodes += result;
		count -= result;
		set_key_offset(&key, get_key_offset(&key) + result * PAGE_SIZE);

		/* seal and unlock znode */
		if (hint.ext_coord.valid)
			reiser4_set_hint(&hint, &key, ZNODE_WRITE_LOCK);
		else
			reiser4_unset_hint(&hint);

	} while (count > 0);

	save_file_hint(file, &hint);
	assert("", reiser4_lock_counters()->d_refs == 0);
	return result;
}

/**
 * Estimate and reserve space for extent write operation
 * @inode: inode of the file to write to;
 * @offset: write position;
 * @count: number of written pages.
 */
static int reserve_write_extent(struct inode *inode, int count)
{
	reiser4_subvol *subv = get_meta_subvol();
	reiser4_tree *tree = &subv->tree;
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
	 * 3. stat data update
	 */
	grab_space_enable();
	return reiser4_grab_space(count /* for 2(a) */ +
		     estimate_one_insert_item(tree) +
		     count * estimate_one_insert_into_item(tree) +
		     estimate_one_insert_item(tree), BA_CAN_COMMIT, subv);
}

/*
 * filemap_copy_from_user no longer exists in generic code, because it
 * is deadlocky (copying from user while holding the page lock is bad).
 * As a temporary fix for reiser4, just define it here.
 */
size_t filemap_copy_from_user(struct page *page, unsigned long offset,
			      const char __user *buf, unsigned bytes)
{
	char *kaddr;
	int left;

	kaddr = kmap_atomic(page);
	left = __copy_from_user_inatomic(kaddr + offset, buf, bytes);
	kunmap_atomic(kaddr);

	if (left != 0) {
		/* Do it the slow way */
		kaddr = kmap(page);
		left = __copy_from_user(kaddr + offset, buf, bytes);
		kunmap(page);
	}
	return bytes - left;
}

/**
 * @file: file to write to
 * @buf: address of user-space buffer
 * @count: number of bytes to write
 * @pos: position in file to write to
 */
ssize_t write_extent_unix_file(struct file *file, struct inode *inode,
			       const char __user *buf, size_t count,
			       loff_t *pos)
{
	int have_to_update_extent;
	int nr_pages;
	int nr_dirty = 0;
	struct page *page;
	jnode *jnodes[DEFAULT_WRITE_GRANULARITY + 1];
	unsigned long index;
	unsigned long end;
	int i;
	int to_page, page_off;
	size_t written;
	size_t left = count;
	int result = 0;
	/*
	 * calculate number of pages which are to be written
	 */
	index = *pos >> PAGE_SHIFT;
	end = ((*pos + count - 1) >> PAGE_SHIFT);
	nr_pages = end - index + 1;
	assert("edward-2293", nr_pages <= DEFAULT_WRITE_GRANULARITY + 1);

	if (reserve_write_extent(inode, nr_pages))
		return RETERR(-ENOSPC);

	if (count == 0) {
		/* case of expanding truncate */
		update_extents_unix_file(file, inode, jnodes, 0, *pos);
		return 0;
	}
	BUG_ON(get_current_context()->trans->atom != NULL);

	/* get pages and jnodes */
	for (i = 0; i < nr_pages; i ++) {
		page = find_or_create_page(inode->i_mapping, index + i,
					   reiser4_ctx_gfp_mask_get());
		if (page == NULL) {
			nr_pages = i;
			result = RETERR(-ENOMEM);
			goto out;
		}
		jnodes[i] = jnode_of_page(page);
		if (IS_ERR(jnodes[i])) {
			unlock_page(page);
			put_page(page);
			nr_pages = i;
			result = RETERR(-ENOMEM);
			goto out;
		}
		/* prevent jnode and page from disconnecting */
		JF_SET(jnodes[i], JNODE_WRITE_PREPARED);
		unlock_page(page);
	}
	BUG_ON(get_current_context()->trans->atom != NULL);

	have_to_update_extent = 0;

	page_off = (*pos & (PAGE_SIZE - 1));
	for (i = 0; i < nr_pages; i ++) {
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
				result = readpage_unix_file(NULL, page);
				assert("edward-2050", result == 0);
				BUG_ON(result != 0);
				/* wait for read completion */
				lock_page(page);
				BUG_ON(!PageUptodate(page));
			} else
				result = 0;
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
			result = RETERR(-EFAULT);
			break;
		}

		flush_dcache_page(page);
		set_page_dirty_notag(page);
		unlock_page(page);
		nr_dirty++;

		mark_page_accessed(page);
		SetPageUptodate(page);

		if (jnodes[i]->blocknr == 0)
			have_to_update_extent ++;

		page_off = 0;
		buf += to_page;
		left -= to_page;
		BUG_ON(get_current_context()->trans->atom != NULL);
	}
	if (have_to_update_extent) {
		result = update_extents_unix_file(file, inode,
						  jnodes, nr_dirty, *pos);

		assert("edward-2278", result == -ENOSPC || result >= 0);
		if (result < 0)
			goto out;
	} else {
		for (i = 0; i < nr_dirty; i ++) {
			struct atom_brick_info *abi;
			assert("edward-1983", jnodes[i]->subvol != NULL);

			spin_lock_jnode(jnodes[i]);
			result = reiser4_try_capture(jnodes[i],
						     ZNODE_WRITE_LOCK, 0);
			spin_unlock_jnode(jnodes[i]);
			BUG_ON(result != 0);

			result = check_insert_atom_brick_info(jnodes[i]->subvol->id,
							     &abi);
			if (result)
				goto out;

			spin_lock_jnode(jnodes[i]);
			jnode_make_dirty_locked(jnodes[i]);
			spin_unlock_jnode(jnodes[i]);
		}
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
	return (count - left) ? (count - left) : result;
}

int __reiser4_readpage_extent(const coord_t *coord,
			      reiser4_extent *ext, reiser4_block_nr pos,
			      struct page *page)
{
	jnode *j;
	struct address_space *mapping;
	unsigned long index;
	oid_t oid;
	int state;
	reiser4_block_nr block;

	mapping = page->mapping;
	oid = get_inode_oid(mapping->host);
	index = page->index;
	state = (ext != NULL ? state_of_extent(ext) : HOLE_EXTENT);

	switch (state) {
	case HOLE_EXTENT:
		j = jfind(mapping, index);
		if (j == NULL) {
			zero_user(page, 0, PAGE_SIZE);
			SetPageUptodate(page);
			unlock_page(page);
			return 0;
		}
		spin_lock_jnode(j);
		if (!jnode_page(j)) {
			jnode_attach_page(j, page);
		} else {
			BUG_ON(jnode_page(j) != page);
			assert("vs-1504", jnode_page(j) == page);
		}
		block = *jnode_get_io_block(j);
		spin_unlock_jnode(j);
		if (block == 0) {
			zero_user(page, 0, PAGE_SIZE);
			SetPageUptodate(page);
			unlock_page(page);
			jput(j);
			return 0;
		}
		/*
		 * page was eflushed previously
		 * (currently eflush is not supported)
		 */
		assert("edward-2213", 0);
		break;
	case ALLOCATED_EXTENT:
		j = jnode_of_page(page);
		if (IS_ERR(j))
			return PTR_ERR(j);
		/*
		 * set "IO address" - a pair (subvolume, block number)
		 */
		if (j->subvol == NULL)
			jnode_set_subvol(j, find_data_subvol(coord));
		else
			assert("edward-2217",
			       j->subvol == find_data_subvol(coord));

		if (*jnode_get_block(j) == 0) {
			reiser4_block_nr blocknr;

			blocknr = extent_get_start(ext) + pos;
			jnode_set_block(j, &blocknr);
		} else
			assert("vs-1403",
			       j->blocknr == extent_get_start(ext) + pos);
		break;

	case UNALLOCATED_EXTENT:
		assert("edward-2214", 0);
		j = jfind(mapping, index);
		assert("nikita-2688", j);
		assert("vs-1426", jnode_page(j) == NULL);

		spin_lock_jnode(j);
		jnode_attach_page(j, page);
		spin_unlock_jnode(j);
		break;

	default:
		warning("vs-957", "wrong extent\n");
		return RETERR(-EIO);
	}
	BUG_ON(j == 0);
	reiser4_page_io(page, j, READ, reiser4_ctx_gfp_mask_get());
	jput(j);
	return 0;
}

int read_extent_unix_file(flow_t *flow, hint_t *hint,
			  struct kiocb *iocb, struct iov_iter *iter)
{
	uf_coord_t *uf_coord;
	coord_t *coord;
	loff_t from_extent;
	reiser4_key ikey;
	ssize_t read;
	size_t wanted;

	assert("vs-1353", current_blocksize == PAGE_SIZE);
	assert("vs-572", flow->user == 1);
	assert("vs-1351", flow->length > 0);

	uf_coord = &hint->ext_coord;

	check_uf_coord(uf_coord, NULL);
	assert("vs-33", uf_coord->lh == &hint->lh);

	coord = &uf_coord->coord;
	assert("vs-1119", znode_is_rlocked(coord->node));
	assert("vs-1120", znode_is_loaded(coord->node));
	assert("vs-1256", coord_matches_key_extent(file_inode(iocb->ki_filp),
						   coord, &flow->key));
	assert("edward-22",
	       get_key_offset(item_key_by_coord(coord, &ikey)) +
	       reiser4_extent_size(coord) > get_key_offset(&flow->key));
	/*
	 * how many bytes can we copy out from this extent
	 */
	from_extent = get_key_offset(item_key_by_coord(coord, &ikey)) +
		reiser4_extent_size(coord) - get_key_offset(&flow->key);
	/*
	 * set a seal and release twig node
	 */
	reiser4_set_hint(hint, &flow->key, ZNODE_READ_LOCK);
	/* &hint->lh is done */

	wanted = iov_iter_count(iter);
	iov_iter_truncate(iter, from_extent);
	/*
	 * read not more than @from_extent bytes from this extent
	 */
	read = generic_file_read_iter(iocb, iter);
	if (read > 0)
		wanted -= read;
	iov_iter_reexpand(iter, wanted);

	if (read <= 0)
		return read;
	move_flow_forward(flow, read);
	return 0;
}

/**
 * reiser4_read->unix_file_read->page_cache_readahead->
 * ->reiser4_readpage_dispatch->readpage_unix_file->
 * ->reiser4_readpage_extent
 * or
 * filemap_fault->reiser4_readpage_dispatch->readpage_unix_file->
 * ->reiser4_readpage_extent
 *
 * At the beginning: coord->node is read locked, zloaded, page is
 * locked, coord is set to existing unit inside of extent item (it
 * is not necessary that coord matches to page->index)
 */
int reiser4_readpage_extent(void *vp, struct page *page)
{
	uf_coord_t *uf_coord = vp;
	ON_DEBUG(coord_t * coord = &uf_coord->coord);
	ON_DEBUG(reiser4_key key);

	assert("vs-1040", PageLocked(page));
	assert("vs-1050", !PageUptodate(page));
	assert("vs-1039", page->mapping && page->mapping->host);

	assert("vs-1044", znode_is_loaded(coord->node));
	assert("vs-758", item_is_extent(coord));
	assert("vs-1046", coord_is_existing_unit(coord));
	assert("vs-1045", znode_is_rlocked(coord->node));
	assert("vs-1047",
	       page->mapping->host->i_ino ==
	       get_key_objectid(item_key_by_coord(coord, &key)));
	check_uf_coord(uf_coord, NULL);

	return __reiser4_readpage_extent(&uf_coord->coord,
					 ext_by_ext_coord(uf_coord),
					 uf_coord->extension.extent.pos_in_unit,
					 page);
}

int get_block_address_extent(const coord_t *coord, sector_t block,
			     sector_t *result)
{
	reiser4_extent *ext;

	if (!coord_is_existing_unit(coord))
		return RETERR(-EINVAL);

	ext = extent_by_coord(coord);

	if (state_of_extent(ext) != ALLOCATED_EXTENT)
		/* FIXME: bad things may happen if it is unallocated extent */
		*result = 0;
	else {
		reiser4_key key;

		unit_key_by_coord(coord, &key);
		assert("vs-1645",
		       block >= get_key_offset(&key) >> current_blocksize_bits);
		assert("vs-1646",
		       block <
		       (get_key_offset(&key) >> current_blocksize_bits) +
		       extent_get_width(ext));
		*result =
		    extent_get_start(ext) + (block -
					     (get_key_offset(&key) >>
					      current_blocksize_bits));
	}
	return 0;
}

reiser4_key *append_key_extent(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(key) + reiser4_extent_size(coord));

	assert("vs-610", get_key_offset(key) &&
	       (get_key_offset(key) & (current_blocksize - 1)) == 0);
	return key;
}

/* plugin->u.item.s.file.init_coord_extension */
void init_coord_extension_extent(uf_coord_t * uf_coord, loff_t lookuped)
{
	coord_t *coord;
	struct extent_coord_extension *ext_coord;
	reiser4_key key;
	loff_t offset;

	assert("vs-1295", uf_coord->valid == 0);

	coord = &uf_coord->coord;
	assert("vs-1288", coord_is_iplug_set(coord));
	assert("vs-1327", znode_is_loaded(coord->node));

	if (coord->between != AFTER_UNIT && coord->between != AT_UNIT)
		return;

	ext_coord = &uf_coord->extension.extent;
	ext_coord->nr_units = nr_units_extent(coord);
	ext_coord->ext_offset =
	    (char *)extent_by_coord(coord) - zdata(coord->node);
	ext_coord->width = extent_get_width(extent_by_coord(coord));
	ON_DEBUG(ext_coord->extent = *extent_by_coord(coord));
	uf_coord->valid = 1;

	/* pos_in_unit is the only uninitialized field in extended coord */
	if (coord->between == AFTER_UNIT) {
		assert("vs-1330",
		       coord->unit_pos == nr_units_extent(coord) - 1);

		ext_coord->pos_in_unit = ext_coord->width - 1;
	} else {
		/* AT_UNIT */
		unit_key_by_coord(coord, &key);
		offset = get_key_offset(&key);

		assert("vs-1328", offset <= lookuped);
		assert("vs-1329",
		       lookuped <
		       offset + ext_coord->width * current_blocksize);
		ext_coord->pos_in_unit =
		    ((lookuped - offset) >> current_blocksize_bits);
	}
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
