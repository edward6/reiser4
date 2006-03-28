/* COPYRIGHT 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../object.h"

#include <linux/quotaops.h>
#include <linux/swap.h>
#include "../../../../mm/filemap.h"


static inline reiser4_extent *ext_by_offset(const znode *node, int offset)
{
	reiser4_extent *ext;

	ext = (reiser4_extent *) (zdata(node) + offset);
	return ext;
}

/**
 * check_uf_coord - verify coord extension
 * @uf_coord:
 * @key:
 *
 * Makes sure that all fields of @uf_coord are set properly. If @key is
 * specified - check whether @uf_coord is set correspondingly.
 */
static void check_uf_coord(const uf_coord_t *uf_coord, const reiser4_key *key)
{
#if REISER4_DEBUG
	const coord_t *coord;
	const extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;

	coord = &uf_coord->coord;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_offset(coord->node, uf_coord->extension.extent.ext_offset);

	assert("",
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
		reiser4_key coord_key;
		
		unit_key_by_coord(&uf_coord->coord, &coord_key);
		set_key_offset(&coord_key,
			       get_key_offset(&coord_key) +
			       (uf_coord->extension.extent.
				pos_in_unit << PAGE_CACHE_SHIFT));
		assert("", keyeq(key, &coord_key));
	}
#endif
}

static inline reiser4_extent *ext_by_ext_coord(const uf_coord_t *uf_coord)
{
	check_uf_coord(uf_coord, NULL);

	return ext_by_offset(uf_coord->coord.node,
			     uf_coord->extension.extent.ext_offset);
}

#if REISER4_DEBUG

/**
 * offset_is_in_unit
 *
 *
 *
 */
/* return 1 if offset @off is inside of extent unit pointed to by @coord. Set
   pos_in_unit inside of unit correspondingly */
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

static int
coord_matches_key_extent(const coord_t * coord, const reiser4_key * key)
{
	reiser4_key item_key;

	assert("vs-771", coord_is_existing_unit(coord));
	assert("vs-1258", keylt(key, append_key_extent(coord, &item_key)));
	assert("vs-1259", keyge(key, item_key_by_coord(coord, &item_key)));

	return offset_is_in_unit(coord, get_key_offset(key));
}

#endif

/**
 * can_append - 
 * @key:
 * @coord:
 *
 * Returns 1 if @key is equal to an append key of item @coord is set to
 */
static int can_append(const reiser4_key *key, const coord_t *coord)
{
	reiser4_key append_key;

	return keyeq(key, append_key_extent(coord, &append_key));
}

/**
 * append_hole
 * @coord:
 * @lh:
 * @key:
 *
 */
static int append_hole(coord_t *coord, lock_handle *lh,
		       const reiser4_key *key)
{
	reiser4_key append_key;
	reiser4_block_nr hole_width;
	reiser4_extent *ext, new_ext;
	reiser4_item_data idata;

	/* last item of file may have to be appended with hole */
	assert("vs-708", znode_get_level(coord->node) == TWIG_LEVEL);
	assert("vs-714", item_id_by_coord(coord) == EXTENT_POINTER_ID);

	/* key of first byte which is not addressed by this extent */
	append_key_extent(coord, &append_key);

	assert("", keyle(&append_key, key));
	
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
		set_extent(ext, HOLE_EXTENT_START,
			   extent_get_width(ext) + hole_width);
		znode_make_dirty(coord->node);
		return 0;
	}

	/* append last item of the file with hole extent unit */
	assert("vs-713", (state_of_extent(ext) == ALLOCATED_EXTENT ||
			  state_of_extent(ext) == UNALLOCATED_EXTENT));

	set_extent(&new_ext, HOLE_EXTENT_START, hole_width);
	init_new_extent(&idata, &new_ext, 1);
	return insert_into_item(coord, lh, &append_key, &idata, 0);
}

/**
 * append_last_extent - append last file item
 * @uf_coord: coord to start insertion from
 * @jnodes: array of jnodes
 * @count: number of jnodes in the array
 *
 * There is already at least one extent item of file @inode in the tree. Append
 * the last of them with unallocated extent unit of width @count. Assign
 * fake block numbers to jnodes corresponding to the inserted extent.
 */
static int append_last_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			      jnode **jnodes, int count)
{
	int result;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;
	reiser4_block_nr block;
	int i;

	coord = &uf_coord->coord;
	ext_coord = &uf_coord->extension.extent;
	ext = ext_by_ext_coord(uf_coord);

	/* check correctness of position in the item */
	assert("vs-228", coord->unit_pos == coord_last_unit_pos(coord));
	assert("vs-1311", coord->between == AFTER_UNIT);
	assert("vs-1302", ext_coord->pos_in_unit == ext_coord->width - 1);

	if (!can_append(key, coord)) {
		/* hole extent has to be inserted */
		result = append_hole(coord, uf_coord->lh, key);
		uf_coord->valid = 0;
		return result;
	}

	if (count) {
		result = DQUOT_ALLOC_BLOCK_NODIRTY(mapping_jnode(jnodes[0])->host,
						   count);
		BUG_ON(result != 0);
	}

	switch (state_of_extent(ext)) {
	case UNALLOCATED_EXTENT:
		/*
		 * last extent unit of the file is unallocated one. Increase
		 * its width by @count
		 */
		set_extent(ext, UNALLOCATED_EXTENT_START,
			   extent_get_width(ext) + count);
		znode_make_dirty(coord->node);

		/* update coord extension */
		ext_coord->width += count;
		ON_DEBUG(extent_set_width
			 (&uf_coord->extension.extent.extent,
			  ext_coord->width));
		break;

	case HOLE_EXTENT:
	case ALLOCATED_EXTENT:
		/*
		 * last extent unit of the file is either hole or allocated
		 * one. Append one unallocated extent of width @count
		 */
		set_extent(&new_ext, UNALLOCATED_EXTENT_START, count);
		init_new_extent(&idata, &new_ext, 1);
		result = insert_into_item(coord, uf_coord->lh, key, &idata, 0);
		uf_coord->valid = 0;
		if (result)
			return result;
		break;

	default:
		return RETERR(-EIO);
	}

	/*
	 * assign fake block numbers to all jnodes. FIXME: make sure whether
	 * twig node containing inserted extent item is locked
	 */
	block = fake_blocknr_unformatted(count);
	for (i = 0; i < count; i ++, block ++) {
		JF_SET(jnodes[i], JNODE_CREATED);
		jnode_set_block(jnodes[i], &block);
	}
	return count;
}

/**
 * insert_first_hole - inser hole extent into tree
 * @coord:
 * @lh:
 * @key:
 *
 *
 */
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

	/* compose body of hole extent and insert item into tree */
	set_extent(&new_ext, HOLE_EXTENT_START, hole_width);
	init_new_extent(&idata, &new_ext, 1);
	return insert_extent_by_coord(coord, &idata, &item_key, lh);
}


/**
 * insert_first_extent - insert first file item
 * @inode: inode of file
 * @uf_coord: coord to start insertion from
 * @jnodes: array of jnodes
 * @count: number of jnodes in the array
 *
 * There are no items of file @inode in the tree yet. Insert unallocated extent
 * of width @count into tree or hole extent if writing not to the
 * beginning. Assign fake block numbers to jnodes corresponding to the inserted
 * unallocated extent. Returns number of jnodes or error code.
 */
static int insert_first_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			       jnode **jnodes, int count)
{
	int result;
	int i;
	reiser4_extent new_ext;
	reiser4_item_data idata;
	reiser4_block_nr block;

	/* first extent insertion starts at leaf level */
	assert("vs-719", znode_get_level(uf_coord->coord.node) == LEAF_LEVEL);
	assert("vs-711", coord_is_between_items(&uf_coord->coord));


	if (get_key_offset(key) != 0) {
		result = insert_first_hole(&uf_coord->coord, uf_coord->lh, key);
		uf_coord->valid = 0;
		return result;
	}

	if (count) {
		result = DQUOT_ALLOC_BLOCK_NODIRTY(mapping_jnode(jnodes[0])->host, count);
		BUG_ON(result != 0);
	}
	/*
	 * prepare for tree modification: compose body of item and item data
	 * structure needed for insertion
	 */
	set_extent(&new_ext, UNALLOCATED_EXTENT_START, count);
	init_new_extent(&idata, &new_ext, 1);

	/* insert extent item into the tree */
	result = insert_extent_by_coord(&uf_coord->coord, &idata, key,
					uf_coord->lh);
	if (result)
		return result;

	/*
	 * assign fake block numbers to all jnodes. FIXME: make sure whether
	 * twig node containing inserted extent item is locked
	 */
	block = fake_blocknr_unformatted(count);
	for (i = 0; i < count; i ++, block ++) {
		JF_SET(jnodes[i], JNODE_CREATED);
		jnode_set_block(jnodes[i], &block);
	}

	/*
	 * invalidate coordinate, research must be performed to continue
	 * because write will continue on twig level
	 */
	uf_coord->valid = 0;
	return count;
}

/**
 * plug_hole - replace hole extent with unallocated and holes
 * @uf_coord:
 * @key:
 * @node:
 * @h: structure containing coordinate, lock handle, key, etc
 *
 * Creates an unallocated extent of width 1 within a hole. In worst case two
 * additional extents can be created.
 */
static int plug_hole(uf_coord_t *uf_coord, const reiser4_key *key)
{
	struct replace_handle rh;
	reiser4_extent *ext;
	reiser4_block_nr width, pos_in_unit;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
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


	if (width == 1) {
		set_extent(ext, UNALLOCATED_EXTENT_START, 1);
		znode_make_dirty(coord->node);
		/* update uf_coord */
		ON_DEBUG(ext_coord->extent = *ext);
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
				extent_set_width(ext - 1,
						 extent_get_width(ext - 1) + 1);
				extent_set_width(ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos--;
				ext_coord->width = extent_get_width(ext - 1);
				ext_coord->pos_in_unit = ext_coord->width - 1;
				ext_coord->ext_offset -= sizeof(reiser4_extent);
				ON_DEBUG(ext_coord->extent =
					 *extent_by_coord(coord));
				return 0;
			}
		}
		/* extent for replace */
		set_extent(&rh.overwrite, UNALLOCATED_EXTENT_START, 1);
		/* extent to be inserted */
		set_extent(&rh.new_extents[0], HOLE_EXTENT_START, width - 1);
		rh.nr_new_extents = 1;

		/* have replace_extent to return with @coord and @uf_coord->lh
		   set to unit which was replaced */
		return_inserted_position = 0;
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
				extent_set_width(ext + 1,
						 extent_get_width(ext + 1) + 1);
				extent_set_width(ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos++;
				ext_coord->width = extent_get_width(ext + 1);
				ext_coord->pos_in_unit = 0;
				ext_coord->ext_offset += sizeof(reiser4_extent);
				ON_DEBUG(ext_coord->extent =
					 *extent_by_coord(coord));
				return 0;
			}
		}
		/* extent for replace */
		set_extent(&rh.overwrite, HOLE_EXTENT_START, width - 1);
		/* extent to be inserted */
		set_extent(&rh.new_extents[0], UNALLOCATED_EXTENT_START, 1);
		rh.nr_new_extents = 1;

		/* have replace_extent to return with @coord and @uf_coord->lh
		   set to unit which was inserted */
		return_inserted_position = 1;
	} else {
		/* extent for replace */
		set_extent(&rh.overwrite, HOLE_EXTENT_START, pos_in_unit);
		/* extents to be inserted */
		set_extent(&rh.new_extents[0], UNALLOCATED_EXTENT_START, 1);
		set_extent(&rh.new_extents[1], HOLE_EXTENT_START,
			   width - pos_in_unit - 1);
		rh.nr_new_extents = 2;

		/* have replace_extent to return with @coord and @uf_coord->lh
		   set to first of units which were inserted */
		return_inserted_position = 1;
	}
	unit_key_by_coord(coord, &rh.paste_key);
	set_key_offset(&rh.paste_key, get_key_offset(&rh.paste_key) +
		       extent_get_width(&rh.overwrite) * current_blocksize);

	uf_coord->valid = 0;
	return replace_extent(&rh, return_inserted_position);
}

/**
 * overwrite_one_block -
 * @uf_coord:
 * @key:
 * @node:
 *
 * If @node corresponds to hole extent - create unallocated extent for it and
 * assign fake block number. If @node corresponds to allocated extent - assign
 * block number of jnode
 */
static int overwrite_one_block(uf_coord_t *uf_coord, const reiser4_key *key,
			       jnode *node, int *hole_plugged)
{
	int result;
	extent_coord_extension_t *ext_coord;
	reiser4_extent *ext;
	reiser4_block_nr block;

	assert("vs-1312", uf_coord->coord.between == AT_UNIT);

	result = 0;
	ext_coord = ext_coord_by_uf_coord(uf_coord);
	ext = ext_by_ext_coord(uf_coord);
	assert("", state_of_extent(ext) != UNALLOCATED_EXTENT);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		blocknr = extent_get_start(ext) + ext_coord->pos_in_unit;
		break;

	case HOLE_EXTENT:
		result = DQUOT_ALLOC_BLOCK_NODIRTY(mapping_jnode(node)->host, 1);
		BUG_ON(result != 0);
		result = plug_hole(uf_coord, key);
		if (result)
			return result;
		block = fake_blocknr_unformatted(1);
		if (hole_plugged)
			*hole_plugged = 1;
		JF_SET(node, JNODE_CREATED);
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
	extent_coord_extension_t *ext_coord;

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
 * overwrite_extent - 
 * @inode:
 *
 * Returns number of handled jnodes.
 */
static int overwrite_extent(uf_coord_t *uf_coord, const reiser4_key *key,
			    jnode **jnodes, int count, int *plugged_hole)
{
	int result;
	reiser4_key k;
	int i;

	k = *key;
	for (i = 0; i < count; i ++) {
		if (*jnode_get_block(jnodes[i]) == 0) {
			result = overwrite_one_block(uf_coord, &k, jnodes[i], plugged_hole);
			if (result)
				return result;
		}
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
		set_key_offset(&k, get_key_offset(&k) + PAGE_CACHE_SIZE);
	}

	return count;
}

/**
 * update_extent
 * @file:
 * @jnodes:
 * @count:
 * @off:
 * 
 */
static int update_extent(struct file *file, jnode **jnodes, int count, loff_t pos)
{
	struct inode *inode;
	struct hint hint;
	reiser4_key key;
	int result;
	znode *loaded;
	
	result = load_file_hint(file, &hint);
	BUG_ON(result != 0);
	
	inode = file->f_dentry->d_inode;

	if (count != 0)
		/*
		 * count == 0 is special case: expanding truncate
		 */
		pos = (loff_t)index_jnode(jnodes[0]) << PAGE_CACHE_SHIFT;
	key_by_inode_and_offset_common(inode, pos, &key);
	
	do {
		result = find_file_item(&hint, &key, ZNODE_WRITE_LOCK, inode);
		if (IS_CBKERR(result))
			return result;

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
			result = append_last_extent(&hint.ext_coord, &key,
						    jnodes, count);
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
						     jnodes, count);
		}
		zrelse(loaded);
		if (result < 0) {
			done_lh(hint.ext_coord.lh);
			break;
		}

		jnodes += result;
		count -= result;
		set_key_offset(&key, get_key_offset(&key) + result * PAGE_CACHE_SIZE);

		/* seal and unlock znode */
		if (hint.ext_coord.valid)
			set_hint(&hint, &key, ZNODE_WRITE_LOCK);
		else
			unset_hint(&hint);

	} while (count > 0);

	save_file_hint(file, &hint);
	return result;
}

int capture_bulk(jnode **jnodes, int count)
{
	int i;
	jnode *node;
	int result;

	for (i = 0; i < count; i ++) {
		node = jnodes[i];
		spin_lock_jnode(node);
 		result = try_capture(node, ZNODE_WRITE_LOCK, 0, 1 /* can_coc */ );
		BUG_ON(result != 0);
		jnode_make_dirty_locked(node);
		JF_CLR(node, JNODE_KEEPME);
		spin_unlock_jnode(node);
	}
	return 0;
}

/**
 * write_extent_reserve_space - reserve space for extent write operation
 * @inode:
 *
 * Estimates and reserves space which may be required for writing
 * WRITE_GRANULARITY pages of file.
 */
static int write_extent_reserve_space(struct inode *inode)
{
	__u64 count;
	reiser4_tree *tree;

	/*
	 * to write WRITE_GRANULARITY pages to a file by extents we have to
	 * reserve disk space for: 
 
	 * 1. find_file_item may have to insert empty node to the tree (empty
	 * leaf node between two extent items). This requires 1 block and
	 * number of blocks which are necessary to perform insertion of an
	 * internal item into twig level.

	 * 2. for each of written pages there might be needed 1 block and
	 * number of blocks which might be necessary to perform insertion of or
	 * paste to an extent item.

	 * 3. stat data update
	 */
	tree = tree_by_inode(inode);
	count = estimate_one_insert_item(tree) +
		WRITE_GRANULARITY * (1 + estimate_one_insert_into_item(tree)) +
		estimate_one_insert_item(tree);
	grab_space_enable();
	return reiser4_grab_space(count, 0 /* flags */);
}

/**
 * write_extent - write method of extent plugin
 * @file: file to write to
 * @buf: address of user-space buffer
 * @write_amount: number of bytes to write
 * @off: position in file to write to
 *
 * This is implementation of write method of struct file_operations for
 * unix file plugin.
 */
ssize_t write_extent(struct file *file, const char __user *buf, size_t count,
		     loff_t *pos)
{
	int have_to_update_extent;
	int nr_pages;
	struct page *pages[WRITE_GRANULARITY + 1];
	struct page *page;
	jnode *jnodes[WRITE_GRANULARITY + 1];
	struct inode *inode;
	unsigned long index;
	unsigned long end;
	int i;
	int to_page, page_off;
	size_t left, written;
	int result;

	inode = file->f_dentry->d_inode;
	if (write_extent_reserve_space(inode))
		return RETERR(-ENOMEM);

	if (count == 0) {
		/* truncate case */
		update_extent(file, jnodes, 0, *pos);
		return 0;
	}

	index = *pos >> PAGE_CACHE_SHIFT;
	/* calculate number of pages which are to be written */
      	end = ((*pos + count - 1) >> PAGE_CACHE_SHIFT);
	nr_pages = end - index + 1;
	assert("", nr_pages <= WRITE_GRANULARITY + 1);

	/* get pages and jnodes */
	for (i = 0; i < nr_pages; i ++) {
		pages[i] = find_or_create_page(inode->i_mapping, index + i, GFP_KERNEL);
		if (pages[i] == NULL) {
			while(i --) {
				unlock_page(pages[i]);
				page_cache_release(pages[i]);
			}
			return RETERR(-ENOMEM);			
		}

		jnodes[i] = jnode_of_page(pages[i]);
		if (IS_ERR(jnodes[i])) {
			unlock_page(pages[i]);
			page_cache_release(pages[i]);
			while (i --) {
				jput(jnodes[i]);
				page_cache_release(pages[i]);
			}
			return RETERR(-ENOMEM);			
		}
		unlock_page(pages[i]);
	}

	have_to_update_extent = 0;

	left = count;
	page_off = (*pos & (PAGE_CACHE_SIZE - 1));
	for (i = 0; i < nr_pages; i ++) {
		to_page = PAGE_CACHE_SIZE - page_off;
		if (to_page > left)
			to_page = left;
		page = pages[i];
		if (((loff_t)page->index << PAGE_CACHE_SHIFT) < inode->i_size &&
		    !PageUptodate(page) && to_page != PAGE_CACHE_SIZE) {
			/*
			 * the above is not optimal for partial write to last
			 * page of file when file size is not at boundary of
			 * page
			 */
			lock_page(page);
			result = readpage_unix_file(NULL, page);
			BUG_ON(result != 0);
			/* wait for read completion */
			lock_page(page);
			BUG_ON(!PageUptodate(page));
			unlock_page(page);
		}

		fault_in_pages_readable(buf, to_page);

		lock_page(page);
		if (!PageUptodate(page) && to_page != PAGE_CACHE_SIZE) {
			void *kaddr;

			kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr, 0, page_off);
			memset(kaddr + page_off + to_page, 0,
			       PAGE_CACHE_SIZE - (page_off + to_page));
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
		}

		written = filemap_copy_from_user(page, page_off, buf, to_page);
		/* FIXME: handle errors */
		BUG_ON(written != to_page);
		flush_dcache_page(page);
		set_page_dirty_internal(page);
		unlock_page(page);
		mark_page_accessed(page);
		SetPageUptodate(page);
		page_cache_release(page);

		if (jnodes[i]->blocknr == 0)
			have_to_update_extent ++;

		page_off = 0;
		buf += to_page;
		left -= to_page;
	}
 
	if (have_to_update_extent)
		update_extent(file, jnodes, nr_pages, *pos);

	/*
	 * capture all jnodes and mark them dirty. For the beginning it can be
	 * implemented as try_to_capture in for loop. no error handling yet.
	 */
	capture_bulk(jnodes, nr_pages);
	for (i = 0; i < nr_pages; i ++)
		jput(jnodes[i]);

	return count - left;
}

static inline void zero_page(struct page *page)
{
	char *kaddr = kmap_atomic(page, KM_USER0);

	memset(kaddr, 0, PAGE_CACHE_SIZE);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	SetPageUptodate(page);
	unlock_page(page);
}

static int
do_readpage_extent(reiser4_extent * ext, reiser4_block_nr pos,
		   struct page *page)
{
	jnode *j;
	struct address_space *mapping;
	unsigned long index;
	oid_t oid;
	reiser4_block_nr block;

	mapping = page->mapping;
	oid = get_inode_oid(mapping->host);
	index = page->index;

	switch (state_of_extent(ext)) {
	case HOLE_EXTENT:
		/*
		 * it is possible to have hole page with jnode, if page was
		 * eflushed previously.
		 */
		j = jfind(mapping, index);
		if (j == NULL) {
			zero_page(page);
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
			zero_page(page);
			jput(j);
			return 0;
		}
		break;

	case ALLOCATED_EXTENT:
		j = jnode_of_page(page);
		if (IS_ERR(j))
			return PTR_ERR(j);
		if (*jnode_get_block(j) == 0) {
			reiser4_block_nr blocknr;

			blocknr = extent_get_start(ext) + pos;
			jnode_set_block(j, &blocknr);
		} else
			assert("vs-1403",
			       j->blocknr == extent_get_start(ext) + pos);
		break;

	case UNALLOCATED_EXTENT:
		j = jfind(mapping, index);
		assert("nikita-2688", j);
		assert("vs-1426", jnode_page(j) == NULL);

		spin_lock_jnode(j);
		jnode_attach_page(j, page);
		spin_unlock_jnode(j);

		/* page is locked, it is safe to check JNODE_EFLUSH */
		assert("vs-1668", JF_ISSET(j, JNODE_EFLUSH));
		break;

	default:
		warning("vs-957", "wrong extent\n");
		return RETERR(-EIO);
	}

	BUG_ON(j == 0);
	page_io(page, j, READ, GFP_NOIO);
	jput(j);
	return 0;
}

static int
move_coord_pages(coord_t * coord, extent_coord_extension_t * ext_coord,
		 unsigned count)
{
	reiser4_extent *ext;

	ext_coord->expected_page += count;

	ext = ext_by_offset(coord->node, ext_coord->ext_offset);

	do {
		if (ext_coord->pos_in_unit + count < ext_coord->width) {
			ext_coord->pos_in_unit += count;
			break;
		}

		if (coord->unit_pos == ext_coord->nr_units - 1) {
			coord->between = AFTER_UNIT;
			return 1;
		}

		/* shift to next unit */
		count -= (ext_coord->width - ext_coord->pos_in_unit);
		coord->unit_pos++;
		ext_coord->pos_in_unit = 0;
		ext_coord->ext_offset += sizeof(reiser4_extent);
		ext++;
		ON_DEBUG(ext_coord->extent = *ext);
		ext_coord->width = extent_get_width(ext);
	} while (1);

	return 0;
}

static int readahead_readpage_extent(void *vp, struct page *page)
{
	int result;
	uf_coord_t *uf_coord;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	uf_coord = vp;
	coord = &uf_coord->coord;

	if (coord->between != AT_UNIT) {
		unlock_page(page);
		return RETERR(-EINVAL);
	}

	ext_coord = &uf_coord->extension.extent;
	if (ext_coord->expected_page != page->index) {
		/* read_cache_pages skipped few pages. Try to adjust coord to page */
		assert("vs-1269", page->index > ext_coord->expected_page);
		if (move_coord_pages
		    (coord, ext_coord,
		     page->index - ext_coord->expected_page)) {
			/* extent pointing to this page is not here */
			unlock_page(page);
			return RETERR(-EINVAL);
		}

		assert("vs-1274", offset_is_in_unit(coord,
						    (loff_t) page->
						    index << PAGE_CACHE_SHIFT));
		ext_coord->expected_page = page->index;
	}

	assert("vs-1281", page->index == ext_coord->expected_page);
	result =
	    do_readpage_extent(ext_by_ext_coord(uf_coord),
			       ext_coord->pos_in_unit, page);
	if (!result)
		move_coord_pages(coord, ext_coord, 1);
	return result;
}

static int move_coord_forward(uf_coord_t *ext_coord)
{
	coord_t *coord;
	extent_coord_extension_t *extension;

	check_uf_coord(ext_coord, NULL);

	extension = &ext_coord->extension.extent;
	extension->pos_in_unit++;
	if (extension->pos_in_unit < extension->width)
		/* stay within the same extent unit */
		return 0;

	coord = &ext_coord->coord;

	/* try to move to the next extent unit */
	coord->unit_pos++;
	if (coord->unit_pos < extension->nr_units) {
		/* went to the next extent unit */
		reiser4_extent *ext;

		extension->pos_in_unit = 0;
		extension->ext_offset += sizeof(reiser4_extent);
		ext = ext_by_offset(coord->node, extension->ext_offset);
		ON_DEBUG(extension->extent = *ext);
		extension->width = extent_get_width(ext);
		return 0;
	}

	/* there is no units in the item anymore */
	return 1;
}

/* this is called by read_cache_pages for each of readahead pages */
static int extent_readpage_filler(void *data, struct page *page)
{
	hint_t *hint;
	loff_t offset;
	reiser4_key key;
	uf_coord_t *ext_coord;
	int result;

	offset = (loff_t) page->index << PAGE_CACHE_SHIFT;
	key_by_inode_and_offset_common(page->mapping->host, offset, &key);

	hint = (hint_t *) data;
	ext_coord = &hint->ext_coord;

	BUG_ON(PageUptodate(page));
	unlock_page(page);

	if (hint_validate(hint, &key, 1 /* check key */ , ZNODE_READ_LOCK) != 0) {
		result = coord_by_key(current_tree, &key, &ext_coord->coord,
				      ext_coord->lh, ZNODE_READ_LOCK,
				      FIND_EXACT, TWIG_LEVEL,
				      TWIG_LEVEL, CBK_UNIQUE, NULL);
		if (result != CBK_COORD_FOUND) {
			unset_hint(hint);
			return result;
		}
		ext_coord->valid = 0;
	}

	if (zload(ext_coord->coord.node)) {
		unset_hint(hint);
		return RETERR(-EIO);
	}
	if (!item_is_extent(&ext_coord->coord)) {
		/* tail conversion is running in parallel */
		zrelse(ext_coord->coord.node);
		unset_hint(hint);
		return RETERR(-EIO);
	}

	if (ext_coord->valid == 0)
		init_coord_extension_extent(ext_coord, offset);

	check_uf_coord(ext_coord, &key);

	lock_page(page);
	if (!PageUptodate(page)) {
		result = do_readpage_extent(ext_by_ext_coord(ext_coord),
					    ext_coord->extension.extent.
					    pos_in_unit, page);
		if (result)
			unlock_page(page);
	} else {
		unlock_page(page);
		result = 0;
	}
	if (!result && move_coord_forward(ext_coord) == 0) {
		set_key_offset(&key, offset + PAGE_CACHE_SIZE);
		set_hint(hint, &key, ZNODE_READ_LOCK);
	} else
		unset_hint(hint);
	zrelse(ext_coord->coord.node);
	return result;
}

/* this is called by reiser4_readpages */
static void
extent_readpages_hook(struct address_space *mapping, struct list_head *pages,
		      void *data)
{
	/* FIXME: try whether having reiser4_read_cache_pages improves anything */
	read_cache_pages(mapping, pages, extent_readpage_filler, data);
}

static int
call_page_cache_readahead(struct address_space *mapping, struct file *file,
			  hint_t * hint,
			  unsigned long page_nr,
			  unsigned long ra_pages, struct file_ra_state *ra)
{
	reiser4_file_fsdata *fsdata;
	int result;

	fsdata = reiser4_get_file_fsdata(file);
	if (IS_ERR(fsdata))
		return page_nr;
	fsdata->ra2.data = hint;
	fsdata->ra2.readpages = extent_readpages_hook;

	result = page_cache_readahead(mapping, ra, file, page_nr, ra_pages);
	fsdata->ra2.readpages = NULL;
	return result;
}

/* this is called when readahead did not */
static int call_readpage(struct file *file, struct page *page)
{
	int result;

	result = readpage_unix_file_nolock(file, page);
	if (result)
		return result;

	lock_page(page);
	if (!PageUptodate(page)) {
		unlock_page(page);
		page_detach_jnode(page, page->mapping, page->index);
		warning("jmacd-97178", "page is not up to date");
		return RETERR(-EIO);
	}
	unlock_page(page);
	return 0;
}

static int filler(void *vp, struct page *page)
{
	return readpage_unix_file_nolock(vp, page);
}

/* Implements plugin->u.item.s.file.read operation for extent items. */
int read_extent(struct file *file, flow_t *flow, hint_t *hint)
{
	int result;
	struct page *page;
	unsigned long cur_page, next_page;
	unsigned long page_off, count;
	struct address_space *mapping;
	loff_t file_off;
	uf_coord_t *uf_coord;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
	unsigned long nr_pages, prev_page;
	struct file_ra_state ra;
	char *kaddr;

	assert("vs-1353", current_blocksize == PAGE_CACHE_SIZE);
	assert("vs-572", flow->user == 1);
	assert("vs-1351", flow->length > 0);

	uf_coord = &hint->ext_coord;
	
	check_uf_coord(uf_coord, NULL);
	assert("vs-33", uf_coord->lh == &hint->lh);

	coord = &uf_coord->coord;
	assert("vs-1119", znode_is_rlocked(coord->node));
	assert("vs-1120", znode_is_loaded(coord->node));
	assert("vs-1256", coord_matches_key_extent(coord, &flow->key));

	mapping = file->f_dentry->d_inode->i_mapping;
	ext_coord = &uf_coord->extension.extent;

	/* offset in a file to start read from */
	file_off = get_key_offset(&flow->key);
	/* offset within the page to start read from */
	page_off = (unsigned long)(file_off & (PAGE_CACHE_SIZE - 1));
	/* bytes which can be read from the page which contains file_off */
	count = PAGE_CACHE_SIZE - page_off;

	/* index of page containing offset read is to start from */
	cur_page = (unsigned long)(file_off >> PAGE_CACHE_SHIFT);
	next_page = cur_page;
	/* number of pages flow spans over */
	nr_pages =
	    ((file_off + flow->length + PAGE_CACHE_SIZE -
	      1) >> PAGE_CACHE_SHIFT) - cur_page;

	/* we start having twig node read locked. However, we do not want to
	   keep that lock all the time readahead works. So, set a sel and
	   release twig node. */
	set_hint(hint, &flow->key, ZNODE_READ_LOCK);
	/* &hint->lh is done-ed */

	ra = file->f_ra;
	prev_page = ra.prev_page;
	do {
		if (next_page == cur_page)
			next_page =
			    call_page_cache_readahead(mapping, file, hint,
						      cur_page, nr_pages, &ra);

		page = find_get_page(mapping, cur_page);
		if (unlikely(page == NULL)) {
			handle_ra_miss(mapping, &ra, cur_page);
			page = read_cache_page(mapping, cur_page, filler, file);
			if (IS_ERR(page))
				return PTR_ERR(page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_detach_jnode(page, mapping, cur_page);
				page_cache_release(page);
				warning("jmacd-97178",
					"extent_read: page is not up to date");
				return RETERR(-EIO);
			}
			unlock_page(page);
		} else {
			if (!PageUptodate(page)) {
				lock_page(page);

				assert("", page->mapping == mapping);
				if (PageUptodate(page))
					unlock_page(page);
				else {
					result = call_readpage(file, page);
					if (result) {
						page_cache_release(page);
						return RETERR(result);
					}
				}
			}
			if (prev_page != cur_page)
				mark_page_accessed(page);
			prev_page = cur_page;
		}

		/* If users can be writing to this page using arbitrary virtual
		   addresses, take care about potential aliasing before reading
		   the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		assert("nikita-3034", schedulable());

		/* number of bytes which are to be read from the page */
		if (count > flow->length)
			count = flow->length;

		result = fault_in_pages_writeable(flow->data, count);
		if (result) {
			page_cache_release(page);
			return RETERR(-EFAULT);
		}

		kaddr = kmap_atomic(page, KM_USER0);
		result = __copy_to_user_inatomic(flow->data,
					       kaddr + page_off, count);
		kunmap_atomic(kaddr, KM_USER0);
		if (result != 0) {
			kaddr = kmap(page);
			result = __copy_to_user(flow->data, kaddr + page_off, count);
			kunmap(page);
			if (unlikely(result))
				return RETERR(-EFAULT);
		}

		page_cache_release(page);

		/* increase key (flow->key), update user area pointer (flow->data) */
		move_flow_forward(flow, count);

		page_off = 0;
		cur_page ++;
		count = PAGE_CACHE_SIZE;
		nr_pages--;
	} while (flow->length);

	file->f_ra = ra;
	return 0;
}

/*
  plugin->u.item.s.file.readpages
*/
void
readpages_extent(void *vp, struct address_space *mapping,
		 struct list_head *pages)
{
	assert("vs-1739", 0);
	if (vp)
		read_cache_pages(mapping, pages, readahead_readpage_extent, vp);
}

/*
   plugin->s.file.readpage
   reiser4_read->unix_file_read->page_cache_readahead->reiser4_readpage->unix_file_readpage->extent_readpage
   or
   filemap_nopage->reiser4_readpage->readpage_unix_file->->readpage_extent

   At the beginning: coord->node is read locked, zloaded, page is
   locked, coord is set to existing unit inside of extent item (it is not necessary that coord matches to page->index)
*/
int readpage_extent(void *vp, struct page *page)
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

	return do_readpage_extent(ext_by_ext_coord(uf_coord),
				  uf_coord->extension.extent.pos_in_unit, page);
}

/**
 * capture_extent - capture page, make sure there is non hole extent for it
 * @key: key of first byte in @page
 * @uf_coord: coordinate and lock handle of position in the tree
 * @page: page to create
 * @mode: preliminary hint obtained via search
 *
 * This implements capture method of item plugin for extent items.  At the
 * beginning uf_coord->coord.node is write locked, zloaded, @page is not
 * locked, @uf_coord is set to in accordance to @key. Extent and jnode
 * corresponding to @page are created if they do not exist yet. Jnode is
 * captured and marked dirty. Tag of @page->mapping->page_tree specifying that
 * page may have no corresponding extent item is cleared.
 */
int
capture_extent(reiser4_key *key, uf_coord_t *uf_coord, struct page *page,
	       int *plugged_hole)
{
	jnode *j;
	int result;

	assert("vs-1051", page->mapping && page->mapping->host);
	assert("nikita-3139",
	       !inode_get_flag(page->mapping->host, REISER4_NO_SD));
	assert("vs-864", znode_is_wlocked(uf_coord->coord.node));
	assert("vs-1398",
	       get_key_objectid(key) == get_inode_oid(page->mapping->host));

	lock_page(page);
	j = jnode_of_page(page);
	if (IS_ERR(j)) {
		unlock_page(page);
		done_lh(uf_coord->lh);
		return PTR_ERR(j);
	}
	spin_lock_jnode(j);
	eflush_del(j, 1);
	unlock_page(page);
	spin_unlock_jnode(j);

	result = overwrite_extent(uf_coord, key, &j, 1, plugged_hole);
	BUG_ON(result != 1);

	spin_lock_jnode(j);
	result = try_capture(j, ZNODE_WRITE_LOCK, 0, 1 /* can_coc */ );
	BUG_ON(result != 0);
	jnode_make_dirty_locked(j);
	spin_unlock_jnode(j);

	jput(j);

	if (get_current_context()->entd) {
		entd_context *ent = get_entd_context(j->tree->super);

		if (ent->cur_request->page == page)
			ent->cur_request->node = j;
	}

	return 0;
}

/*
  plugin->u.item.s.file.get_block
*/
int
get_block_address_extent(const coord_t * coord, sector_t block,
			 sector_t * result)
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

/*
  plugin->u.item.s.file.append_key
  key of first byte which is the next to last byte by addressed by this extent
*/
reiser4_key *append_key_extent(const coord_t * coord, reiser4_key * key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key,
		       get_key_offset(key) + extent_size(coord,
							 nr_units_extent
							 (coord)));

	assert("vs-610", get_key_offset(key)
	       && (get_key_offset(key) & (current_blocksize - 1)) == 0);
	return key;
}

/* plugin->u.item.s.file.init_coord_extension */
void init_coord_extension_extent(uf_coord_t * uf_coord, loff_t lookuped)
{
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
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
