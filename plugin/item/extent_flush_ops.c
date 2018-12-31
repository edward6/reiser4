/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../tree.h"
#include "../../jnode.h"
#include "../../super.h"
#include "../../flush.h"
#include "../../carry.h"
#include "../object.h"

#include <linux/pagemap.h>

static reiser4_block_nr extent_unit_start(const coord_t * item);

/* Return either first or last extent (depending on @side) of the item
   @coord is set to. Set @pos_in_unit either to first or to last block
   of extent. */
static reiser4_extent *extent_utmost_ext(const coord_t * coord, sideof side,
					 reiser4_block_nr * pos_in_unit)
{
	reiser4_extent *ext;

	if (side == LEFT_SIDE) {
		/* get first extent of item */
		ext = extent_item(coord);
		*pos_in_unit = 0;
	} else {
		/* get last extent of item and last position within it */
		assert("vs-363", side == RIGHT_SIDE);
		ext = extent_item(coord) + coord_last_unit_pos(coord);
		*pos_in_unit = extent_get_width(ext) - 1;
	}

	return ext;
}

/**
 * item_plugin->f.utmost_child
 *
 * Return the child. Coord is set to extent item.
 * Find jnode corresponding either to first or to
 * last unformatted node pointed by the item
 */
int utmost_child_extent(const coord_t *coord, sideof side, jnode **childp)
{
	reiser4_extent *ext;
	reiser4_block_nr pos_in_unit;
	reiser4_subvol *data_subv;

	assert("edward-1851", item_is_extent(coord));

	data_subv = find_data_subvol(coord);
	ext = extent_utmost_ext(coord, side, &pos_in_unit);

	switch (state_of_extent(ext)) {
	case HOLE_EXTENT:
		*childp = NULL;
		return 0;
	case ALLOCATED_EXTENT:
	case UNALLOCATED_EXTENT:
		break;
	default:
		impossible("vs-1417", "Bad state of extent (%d)",
			   state_of_extent(ext));
		BUG_ON(1);
	}
	{
		reiser4_key key;
		loff_t offset;
		unsigned long index;
		/*
		 * offset of the first or next after last (depending on
		 * @side) byte addressed by the extent
		 */
		offset = get_key_offset(item_key_by_coord(coord, &key));
		if (side == RIGHT_SIDE)
			offset += reiser4_extent_size(coord);

		assert("vs-544", (offset >> PAGE_SHIFT) < ~0ul);
		/*
		 * index of first or last (depending on @side) page
		 * addressed by the extent
		 */
		index = (unsigned long)(offset >> PAGE_SHIFT);
		if (side == RIGHT_SIDE)
			index--;

		*childp = jlookup(get_key_objectid(&key), index);
	}
	return 0;
}

/* item_plugin->f.utmost_child_real_block */
/* Return the child's block, if allocated. */
int
utmost_child_real_block_extent(const coord_t * coord, sideof side,
			       reiser4_block_nr * block)
{
	reiser4_extent *ext;

	ext = extent_by_coord(coord);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		*block = extent_get_start(ext);
		if (side == RIGHT_SIDE)
			*block += extent_get_width(ext) - 1;
		break;
	case HOLE_EXTENT:
	case UNALLOCATED_EXTENT:
		*block = 0;
		break;
	default:
		/* this should never happen */
		assert("vs-1418", 0);
	}

	return 0;
}

/* item_plugin->f.scan */
/* Performs leftward scanning starting from an unformatted node and its parent coordinate.
   This scan continues, advancing the parent coordinate, until either it encounters a
   formatted child or it finishes scanning this node.

   If unallocated, the entire extent must be dirty and in the same atom.  (Actually, I'm
   not sure this is last property (same atom) is enforced, but it should be the case since
   one atom must write the parent and the others must read the parent, thus fusing?).  In
   any case, the code below asserts this case for unallocated extents.  Unallocated
   extents are thus optimized because we can skip to the endpoint when scanning.

   It returns control to reiser4_scan_extent, handles these terminating conditions,
   e.g., by loading the next twig.
*/
int reiser4_scan_extent(flush_scan * scan)
{
	coord_t coord;
	jnode *neighbor;
	unsigned long scan_index, unit_index, unit_width, scan_max, scan_dist;
	reiser4_block_nr unit_start;
	__u64 oid;
	reiser4_key key;
	int ret = 0, allocated, incr;

	if (!JF_ISSET(scan->node, JNODE_DIRTY)) {
		/*
		 * Race with truncate, this node is already truncated
		 */
		scan->stop = 1;
		return 0;
	}
	coord_dup(&coord, &scan->parent_coord);

	assert("jmacd-1404", !reiser4_scan_finished(scan));
	assert("jmacd-1405", jnode_get_level(scan->node) == LEAF_LEVEL);
	assert("jmacd-1406", jnode_is_unformatted(scan->node));
	/*
	 * The scan_index variable corresponds to the current page index
	 * of the unformatted block scan position
	 */
	scan_index = index_jnode(scan->node);

	assert("jmacd-7889", item_is_extent(&coord));
	assert("edward-1870", scan->data_subv == find_data_subvol(&coord));
 repeat:
	oid = get_key_objectid(item_key_by_coord(&coord, &key));

	allocated = !extent_is_unallocated(&coord);
	/*
	 * Get the values of this extent unit:
	 */
	unit_index = extent_unit_index(&coord);
	unit_width = extent_unit_width(&coord);
	unit_start = extent_unit_start(&coord);

	assert("jmacd-7187", unit_width > 0);
	assert("jmacd-7188", scan_index >= unit_index);
	assert("jmacd-7189", scan_index <= unit_index + unit_width - 1);
	/*
	 * Depending on the scan direction, we set different maximum values
	 * for scan_index (scan_max) and the number of nodes that would be
	 * passed if the scan goes the entire way (scan_dist). Incr is an
	 * integer reflecting the incremental direction of scan_index
	 */
	if (reiser4_scanning_left(scan)) {
		scan_max = unit_index;
		scan_dist = scan_index - unit_index;
		incr = -1;
	} else {
		scan_max = unit_index + unit_width - 1;
		scan_dist = scan_max - unit_index;
		incr = +1;
	}
	/*
	 * If the extent is allocated we have to check each of its blocks.
	 * If the extent is unallocated we can skip to the scan_max
	 */
	if (allocated) {
		do {
			neighbor = jlookup(oid, scan_index);
			if (neighbor == NULL)
				goto stop_same_parent;

			if (scan->node != neighbor &&
			    !reiser4_scan_goto(scan, neighbor)) {
				/*
				 * @neighbor was jput() by reiser4_scan_goto
				 */
				goto stop_same_parent;
			}
			ret = move_scan_pos(scan, neighbor, 1, &coord);
			if (ret != 0) {
				goto exit;
			}
			/*
			 * reference to @neighbor is stored in @scan, no need
			 * to jput()
			 */
			scan_index += incr;
		} while (incr + scan_max != scan_index);
	} else {
		/*
		 * Optimized case for unallocated extents, skip to the end
		 */
		neighbor = jlookup(oid, scan_max /*index */);
		if (neighbor == NULL) {
			/*
			 * Race with truncate
			 */
			scan->stop = 1;
			ret = 0;
			goto exit;

		} else if (!reiser4_scan_goto(scan, neighbor)) {
			/*
			 * @neighbor was jput() by reiser4_scan_goto
			 */
			goto stop_same_parent;
		}
		assert("zam-1043",
		       reiser4_blocknr_is_fake(jnode_get_block(neighbor)));

		ret = move_scan_pos(scan, neighbor, scan_dist, &coord);
		if (ret != 0) {
			goto exit;
		}
	}
	if (coord_sideof_unit(&coord, scan->direction) == 0 &&
	    item_is_extent(&coord)) {
		/*
		 * Continue as long as there are more extent units
		 */
		scan_index = extent_unit_index(&coord) +
			(reiser4_scanning_left(scan) ?
			 extent_unit_width(&coord) - 1 : 0);
		goto repeat;
	}
	if (0) {
	stop_same_parent:
		/*
		 * If we are scanning left and we stop in the middle of an
		 * allocated extent, we know the preceder immediately..
		 *
		 * middle of extent is (scan_index - unit_index) != 0
		 */
		if (reiser4_scanning_left(scan) &&
		    (scan_index - unit_index) != 0) {
			/*
			 * FIXME(B): Someone should step-through and verify
			 * that this preceder calculation is indeed correct
			 *
			 * @unit_start is starting block (number) of extent
			 * unit. Flush stopped at the @scan_index block from
			 * the beginning of the file, which is (scan_index -
			 * unit_index) block within extent.
			 */
			if (unit_start) {
				/*
				 * skip preceder update when we are at hole
				 */
				scan->data_preceder_blk =
					unit_start + scan_index - unit_index;
				check_preceder(scan->data_preceder_blk,
					       scan->data_subv);
			}
		}
		/*
		 * In this case, we leave coord set to the parent of scan->node
		 */
		scan->stop = 1;
	} else {
		/*
		 * scan to be continued,
		 * coord is set to the next item which is either off-the-end
		 * of the node or not an extent
		 */
		assert("jmacd-8912", scan->stop == 0);
		assert("jmacd-7812",
		       (coord_is_after_sideof_unit(&coord, scan->direction) ||
			!item_is_extent(&coord)));
	}
	ret = 0;
 exit:
	return ret;
}

/**
 * When on flush time unallocated extent is to be replaced with allocated one
 * it may happen that one unallocated extent will have to be replaced with set
 * of allocated extents. In this case insert_into_item will be called which may
 * have to add new nodes into tree. Space for that is taken from inviolable
 * reserve (5%).
 */
static reiser4_block_nr reserve_replace(reiser4_subvol *subv)
{
	reiser4_block_nr grabbed, needed;

	grabbed = ctx_subvol_grabbed(get_current_context(), subv->id);
	needed = estimate_one_insert_into_item(&subv->tree);
	check_me("vpf-340",
		 !reiser4_grab_space_force(needed, BA_RESERVED, subv));
	return grabbed;
}

/* Block offset of first block addressed by unit */
__u64 extent_unit_index(const coord_t * item)
{
	reiser4_key key;

	assert("vs-648", coord_is_existing_unit(item));
	unit_key_by_coord(item, &key);
	return get_key_offset(&key) >> current_blocksize_bits;
}

/* AUDIT shouldn't return value be of reiser4_block_nr type?
   Josh's answer: who knows?  Is a "number of blocks" the same type as "block offset"? */
__u64 extent_unit_width(const coord_t * item)
{
	assert("vs-649", coord_is_existing_unit(item));
	return width_by_coord(item);
}

/* Starting block location of this unit */
static reiser4_block_nr extent_unit_start(const coord_t * item)
{
	return extent_get_start(extent_by_coord(item));
}

/**
 * split_allocated_extent -
 * @coord:
 * @pos_in_unit:
 *
 * replace allocated extent with two allocated extents
 */
int split_allocated_extent(coord_t *coord, reiser4_block_nr pos_in_unit,
			   int return_inserted_pos)
{
	int result;
	struct replace_handle *h;
	item_id extent_id;
	reiser4_extent *ext;
	reiser4_block_nr was_grabbed;

	ext = extent_by_coord(coord);
	extent_id = item_id_by_coord(coord);

	assert("vs-1410", state_of_extent(ext) == ALLOCATED_EXTENT);
	assert("vs-1411", extent_get_width(ext) > pos_in_unit);

	h = kmalloc(sizeof(*h), reiser4_ctx_gfp_mask_get());
	if (h == NULL)
		return RETERR(-ENOMEM);
	h->coord = coord;
	h->lh = znode_lh(coord->node);
	h->pkey = &h->key;
	unit_key_by_coord(coord, h->pkey);
	set_key_offset(h->pkey,
		       (get_key_offset(h->pkey) +
			pos_in_unit * current_blocksize));
	reiser4_set_extent(&h->overwrite, extent_get_start(ext),
			   pos_in_unit);
	reiser4_set_extent(&h->new_extents[0],
			   extent_get_start(ext) + pos_in_unit,
			   extent_get_width(ext) - pos_in_unit);
	h->nr_new_extents = 1;
	h->flags = COPI_DONT_SHIFT_LEFT;
	h->paste_key = h->key;

	/* reserve space for extent unit paste, @grabbed is reserved before */
	was_grabbed = reserve_replace(get_meta_subvol());
	result = reiser4_replace_extent(extent_id, h, return_inserted_pos);
	/* restore reserved */
	grabbed2free_mark(was_grabbed, get_meta_subvol());
	kfree(h);
	return result;
}

/**
 * replace unallocated extent with two unallocated extents
 */
int split_unallocated_extent(coord_t *coord, reiser4_block_nr pos_in_unit,
			     int return_inserted_pos)
{
	int result;
	struct replace_handle *h;
	item_id extent_id;
	reiser4_extent *ext;
	reiser4_block_nr was_grabbed;

	ext = extent_by_coord(coord);
	extent_id = item_id_by_coord(coord);

	assert("edward-2118", state_of_extent(ext) == UNALLOCATED_EXTENT);
	assert("edward-2119", extent_get_width(ext) > pos_in_unit);

	h = kmalloc(sizeof(*h), reiser4_ctx_gfp_mask_get());
	if (h == NULL)
		return RETERR(-ENOMEM);
	h->coord = coord;
	h->lh = znode_lh(coord->node);
	h->pkey = &h->key;
	unit_key_by_coord(coord, h->pkey);
	set_key_offset(h->pkey,
		       (get_key_offset(h->pkey) +
			pos_in_unit * current_blocksize));
	reiser4_set_extent(&h->overwrite, 1, pos_in_unit);
	reiser4_set_extent(&h->new_extents[0], 1,
			   extent_get_width(ext) - pos_in_unit);
	h->nr_new_extents = 1;
	h->flags = COPI_DONT_SHIFT_LEFT;
	h->paste_key = h->key;
	/*
	 * reserve space for extent unit paste, @grabbed is reserved before
	 */
	was_grabbed = reserve_replace(get_meta_subvol());
	result = reiser4_replace_extent(extent_id, h, return_inserted_pos);
	/* restore reserved */
	grabbed2free_mark(was_grabbed, get_meta_subvol());
	kfree(h);
	return result;
}

/**
 * Split extent unit specified by @coord into 2 extent units.
 * @pos: position to split;
 * @adv_to_right: if true, then set @coord to the right extent
 * unit, otherwise, to the left one
 */
int split_extent_unit(coord_t *coord, reiser4_block_nr pos,
		      int adv_to_right)
{
	switch(state_of_extent(extent_by_coord(coord))) {
	case ALLOCATED_EXTENT:
		return split_allocated_extent(coord, pos, adv_to_right);
	case UNALLOCATED_EXTENT:
		return split_unallocated_extent(coord, pos, adv_to_right);
	default:
		impossible("edward-2120", "Bad state of extent");
	}
	return -EIO;
}

/**
 * Pre-condition: We want to replace extent @ext by extent @replace.
 * Try to merge @replace with previous extent of the item (if there is one).
 * Return 1 if merging succeeded, 0 - otherwise.
 */
static int try_to_merge_with_left(coord_t *coord, reiser4_extent *ext,
				  reiser4_extent *replace)
{
	reiser4_key key;

	assert("vs-1415", extent_by_coord(coord) == ext);

	if (coord->unit_pos == 0 ||
	    state_of_extent(ext - 1) != ALLOCATED_EXTENT)
		/*
		 * left neighbor of @ext either does not exist
		 * or is not allocated extent
		 */
		return 0;
	unit_key_by_coord(coord, &key);

	if (extent_get_start(ext - 1) + extent_get_width(ext - 1) !=
	    extent_get_start(replace))
		return 0;
	/*
	 * we can glue, widen previous unit
	 */
	extent_set_width(ext - 1,
			 extent_get_width(ext - 1) + extent_get_width(replace));

	if (extent_get_width(ext) != extent_get_width(replace)) {
		/* make current extent narrower */
		if (state_of_extent(ext) == ALLOCATED_EXTENT)
			extent_set_start(ext,
					 extent_get_start(ext) +
					 extent_get_width(replace));
		extent_set_width(ext,
				 extent_get_width(ext) -
				 extent_get_width(replace));
	} else {
		/*
		 * current extent completely glued with its left
		 * neighbor, remove it
		 */
		coord_t from, to;

		coord_dup(&from, coord);
		from.unit_pos = nr_units_extent(coord) - 1;
		coord_dup(&to, &from);
		/*
		 * Currently extent can be cut either from the
		 * beginning or from the end. Our unit can be
		 * in the middle, however. So we work around
		 * this. Move place which got freed after unit
		 * removal to end of item
		 */
		memmove(ext, ext + 1,
			(from.unit_pos -
			 coord->unit_pos) * sizeof(reiser4_extent));
		/*
		 * wipe part of item which is going to be cut, so that
		 * check_node() will not be confused
		 */
		cut_node_content(&from, &to, NULL, NULL, NULL);
	}
	znode_make_dirty(coord->node);
	/* move coord back */
	coord->unit_pos--;
	return 1;
}

/**
 * convert_extent - replace extent with 2 ones
 * @coord: coordinate of extent to be replaced
 * @replace: extent to overwrite the one @coord is set to
 *
 * Overwrites extent @coord is set to and paste one extent unit after
 * overwritten one if @replace is shorter than initial extent
 */
int convert_extent(coord_t *coord, reiser4_extent *replace)
{
	int result;
	struct replace_handle *h;
	reiser4_extent *ext;
	item_id extent_id;
	reiser4_block_nr start, width, new_width;
	reiser4_block_nr was_grabbed;
	extent_state state;

	ext = extent_by_coord(coord);
	extent_id = item_id_by_coord(coord);
	state = state_of_extent(ext);
	start = extent_get_start(ext);
	width = extent_get_width(ext);
	new_width = extent_get_width(replace);

	assert("vs-1458", (state == UNALLOCATED_EXTENT ||
			   state == ALLOCATED_EXTENT));
	assert("vs-1459", width >= new_width);

	if (try_to_merge_with_left(coord, ext, replace)) {
		/*
		 * @replace was merged with left neighbor.
		 * Current unit is either removed or narrowed
		 */
		if (width == new_width &&
		    coord->unit_pos < coord_last_unit_pos(coord)) {
			coord_t tcoord;
			coord_dup(&tcoord, coord);
			tcoord.unit_pos ++;
			/*
			 * Current unit has been removed and now @coord
			 * is pointing out to the unit that it was merged
			 * with. Here it can happen that the last one is
			 * mergeable with the right unit (pointed out by
			 * @tcoord). If so, then merge them.
			 */
			try_to_merge_with_left(&tcoord,
					       extent_by_coord(&tcoord),
					       extent_by_coord(&tcoord));
		}
		return 0;
	}
	if (width == new_width) {
		/*
		 * replace current extent with @replace
		 */
		*ext = *replace;
		/*
		 * After replacing it can happen that the unit is
		 * mergeable with the right unit (if there is one).
		 * If so, then merge them.
		 */
		if (coord->unit_pos < coord_last_unit_pos(coord)) {
			coord_t tcoord;
			coord_dup(&tcoord, coord);
			tcoord.unit_pos ++;

			try_to_merge_with_left(&tcoord,
					       extent_by_coord(&tcoord),
					       extent_by_coord(&tcoord));
		}
		znode_make_dirty(coord->node);
		return 0;
	}

	h = kmalloc(sizeof(*h), reiser4_ctx_gfp_mask_get());
	if (h == NULL)
		return RETERR(-ENOMEM);
	h->coord = coord;
	h->lh = znode_lh(coord->node);
	h->pkey = &h->key;
	unit_key_by_coord(coord, h->pkey);
	set_key_offset(h->pkey,
		       (get_key_offset(h->pkey) + new_width * current_blocksize));
	h->overwrite = *replace;

	/* replace @ext with @replace and padding extent */
	reiser4_set_extent(&h->new_extents[0],
			   (state == ALLOCATED_EXTENT) ?
			   (start + new_width) :
			   UNALLOCATED_EXTENT_START,
			   width - new_width);
	h->nr_new_extents = 1;
	h->flags = COPI_DONT_SHIFT_LEFT;
	h->paste_key = h->key;

	/* reserve space for extent unit paste, @grabbed is reserved before */
	was_grabbed = reserve_replace(get_meta_subvol());
	result = reiser4_replace_extent(extent_id, h, 0 /* leave @coord set
							   to overwritten
							   extent */);
	/* restore reserved */
	grabbed2free_mark(was_grabbed, get_meta_subvol());
	kfree(h);
	return result;
}

/**
 * assign_real_blocknrs
 * @flush_pos:
 * @oid: objectid of file jnodes to assign block number to belongs to
 * @index: first jnode on the range
 * @count: number of jnodes to assign block numbers to
 * @first: start of allocated block range
 *
 * Assigns block numbers to each of @count jnodes. Index of first jnode is
 * @index. Jnodes get lookuped with jlookup.
 */
void assign_real_blocknrs(flush_pos_t *flush_pos, oid_t oid,
			  unsigned long index, reiser4_block_nr count,
			  reiser4_block_nr first, reiser4_subvol *subv)
{
	unsigned long i;
	txn_atom *atom;
	int nr;

	atom = atom_locked_by_fq(flush_pos->fq);
	assert("vs-1468", atom);
	BUG_ON(atom == NULL);

	nr = 0;
	for (i = 0; i < count; ++i, ++index) {
		jnode *node;

		node = jlookup(oid, index);
		assert("", node != NULL);
		BUG_ON(node == NULL);

		spin_lock_jnode(node);
		assert("", !jnode_is_flushprepped(node));
		assert("vs-1475", node->atom == atom);
		assert("vs-1476", atomic_read(&node->x_count) > 0);

		JF_CLR(node, JNODE_FLUSH_RESERVED);
		assert("edward-2218", node->subvol != NULL);
		jnode_set_block(node, &first);
		unformatted_make_reloc(node, flush_pos->fq);
		ON_DEBUG(count_jnode(node->atom, node, NODE_LIST(node),
				     FQ_LIST, 0));
		spin_unlock_jnode(node);
		first++;

		atomic_dec(&node->x_count);
		nr ++;
	}

	spin_unlock_atom(atom);
	return;
}

/**
 * Find out how many adjacent blocks of an allocated extent (specified
 * by @index and @count) belong to the atom and are not "flushprepped".
 * It is used by the flush procedure when making reallocation decisions
 */
int allocated_extent_slum_size(flush_pos_t *flush_pos, oid_t oid,
			       unsigned long index, unsigned long count)
{
	unsigned long i;
	txn_atom *atom;
	int nr;

	assert("edward-1869", flush_pos->data_subv != NULL);

	atom = atom_locked_by_fq(reiser4_pos_fq(flush_pos));
	assert("vs-1468", atom);

	nr = 0;

	for (i = 0; i < count; ++i, ++index) {
		jnode *node;

		node = jlookup(oid, index);
		if (!node)
			break;

		if (jnode_check_flushprepped(node)) {
			atomic_dec(&node->x_count);
			break;
		}
		if (node->atom != atom) {
			/*
			 * this is possible on overwrite: extent_write may
			 * capture several unformatted nodes without capturing
			 * any formatted nodes.
			 */
			atomic_dec(&node->x_count);
			break;
		}
		assert("vs-1476", atomic_read(&node->x_count) > 1);
		atomic_dec(&node->x_count);
		nr ++;
	}
	spin_unlock_atom(atom);
	return nr;
}

static inline int are_units_mergeable(reiser4_extent *left,
				      reiser4_extent *right)
{
	if (state_of_extent(left) != state_of_extent(right))
		return 0;
	switch (state_of_extent(left)) {
	case HOLE_EXTENT:
		return 1;
	case ALLOCATED_EXTENT:
		return extent_get_start(left) + extent_get_width(left) ==
			extent_get_start(right);
	default:
		impossible("edward-2092", "Bad extent state (%d)",
			   state_of_extent(left));
	}
}

/**
 * Copy an extent unit @ext at position @coord to the end of
 * node @dst.
 * @key is the key of that extent unit.
 *
 * It may have to either insert new item after the last one,
 * or append last item, or modify last unit of last item to
 * have greater width. If there is no enough spece on the @dst
 * then return -E_NODE_FULL
 */
int shift_extent_left_begin(znode *dst, const coord_t *coord,
			    const reiser4_key *key, reiser4_extent *ext)
{
	int result;
	coord_t dst_coord;
	cop_insert_flag flags;
	reiser4_item_data data;

	coord_init_last_unit(&dst_coord, dst);
	dst_coord.between = AFTER_UNIT;

	flags = COPI_DONT_SHIFT_LEFT |
		COPI_DONT_SHIFT_RIGHT | COPI_DONT_ALLOCATE;

	if (!are_items_mergeable(&dst_coord, coord))
		/*
		 * create a new item
		 */
		result = insert_by_coord(&dst_coord,
				init_new_extent(item_id_by_coord(coord),
						&data, ext, 1),
					 key, NULL /*lh */ , flags);
	else {
		/*
		 * push to existing item
		 */
		reiser4_extent *dst_ext;
		assert("edward-2091", item_is_extent(&dst_coord));

		dst_ext = extent_by_coord(&dst_coord);

		if (are_units_mergeable(dst_ext, ext)) {
			/*
			 * fast paste
			 */
			extent_set_width(dst_ext,
					 extent_get_width(dst_ext) +
					 extent_get_width(ext));
			znode_make_dirty(dst);
			return 0;
		}
		/* paste */
		result = insert_into_item(&dst_coord, NULL /*lh */, key,
				init_new_extent(item_id_by_coord(coord),
						&data, ext, 1),
					  flags);
	}
	assert("vs-438", result == 0 || result == -E_NODE_FULL);
	return result;
}

/*
 * complete shifting started by shift_extent_left_begin(). Cut the original unit.
 */
int shift_extent_left_complete(coord_t *to, reiser4_key *to_key,
			       znode *left)
{
	coord_t from;
	reiser4_key from_key;

	coord_init_first_unit(&from, to->node);
	item_key_by_coord(&from, &from_key);

	return cut_node_content(&from, to, &from_key, to_key, NULL);
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
