/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../tree.h"
#include "../../jnode.h"
#include "../../super.h"
#include "../../flush.h"
#include "../../carry.h"

#include <linux/pagemap.h>

/* Return either first or last extent (depending on @side) of the item
   @coord is set to. Set @pos_in_unit either to first or to last block
   of extent. */
static reiser4_extent *
extent_utmost_ext(const coord_t *coord, sideof side, reiser4_block_nr *pos_in_unit)
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

/* item_plugin->f.utmost_child */
/* Return the child. Coord is set to extent item. Find jnode corresponding
   either to first or to last unformatted node pointed by the item */
int
utmost_child_extent(const coord_t *coord, sideof side, jnode **childp)
{
	reiser4_extent *ext;
	reiser4_block_nr pos_in_unit;

	ext = extent_utmost_ext(coord, side, &pos_in_unit);

	switch (state_of_extent(ext)) {
	case HOLE_EXTENT:
		*childp = NULL;
		return 0;
	case ALLOCATED_EXTENT:
	case UNALLOCATED_EXTENT:
		break;
	case UNALLOCATED_EXTENT2:
		/* this should never happen */
		assert("vs-1417", 0);
	}

	{
		reiser4_key key;
		reiser4_tree *tree;
		unsigned long index;

		if (side == LEFT_SIDE) {
			/* get key of first byte addressed by the extent */
			item_key_by_coord(coord, &key);
		} else {
			/* get key of byte which next after last byte addressed by the extent */
			append_key_extent(coord, &key);
		}

		assert("vs-544", (get_key_offset(&key) >> PAGE_CACHE_SHIFT) < ~0ul);
		/* index of first or last (depending on @side) page addressed
		   by the extent */
		index = (unsigned long) (get_key_offset(&key) >> PAGE_CACHE_SHIFT);
		if (side == RIGHT_SIDE)
			index --;

		tree = coord->node->zjnode.tree;
		*childp = jlookup(tree, get_key_objectid(&key), index);
	}

	return 0;
}

/* item_plugin->f.utmost_child_real_block */
/* Return the child's block, if allocated. */
int
utmost_child_real_block_extent(const coord_t *coord, sideof side, reiser4_block_nr *block)
{
	reiser4_extent *ext;
	reiser4_block_nr pos_in_unit;

	ext = extent_utmost_ext(coord, side, &pos_in_unit);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		*block = extent_get_start(ext) + pos_in_unit;
		break;
	case HOLE_EXTENT:
	case UNALLOCATED_EXTENT:
		*block = 0;
		break;
	case UNALLOCATED_EXTENT2:
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

   It returns control to scan_extent, handles these terminating conditions, e.g., by
   loading the next twig.
*/
int scan_extent(flush_scan * scan, const coord_t * in_coord)
{
	coord_t coord;
	jnode *neighbor;
	unsigned long scan_index, unit_index, unit_width, scan_max, scan_dist;
	reiser4_block_nr unit_start;
	/*struct inode *ino = NULL; */
	__u64 oid;
	reiser4_key key;
	/*struct page *pg; */
	int ret = 0, allocated, incr;
	reiser4_tree *tree;

	if (!jnode_check_dirty(scan->node)) {
		scan->stop = 1;
		return 0; /* Race with truncate, this node is already
			   * truncated. */
	}

	coord_dup(&coord, in_coord);

	assert("jmacd-1404", !scan_finished(scan));
	assert("jmacd-1405", jnode_get_level(scan->node) == LEAF_LEVEL);
	assert("jmacd-1406", jnode_is_unformatted(scan->node));

	/* The scan_index variable corresponds to the current page index of the
	   unformatted block scan position. */
	scan_index = index_jnode(scan->node);

	assert("jmacd-7889", item_is_extent(&coord));

	ON_TRACE(TRACE_FLUSH_VERB, "%s scan starts %lu: %s\n",
		 (scanning_left(scan) ? "left" : "right"), scan_index, jnode_tostring(scan->node));

repeat:
	/* objectid of file */
	oid = get_key_objectid(item_key_by_coord(&coord, &key));

	ON_TRACE(TRACE_FLUSH_VERB, "%s scan index %lu: parent %p oid %llu\n",
		 (scanning_left(scan) ? "left" : "right"), scan_index, coord.node, oid);

	/* FIXME:NIKITA->* this is wrong: hole is treated as allocated extent
	   by this function---this leads to incorrect preceder update in
	   "stop-same-parent" branch below.
	*/
	allocated = !extent_is_unallocated(&coord);
	/* Get the values of this extent unit: */
	unit_index = extent_unit_index(&coord);
	unit_width = extent_unit_width(&coord);
	unit_start = extent_unit_start(&coord);

	assert("jmacd-7187", unit_width > 0);
	assert("jmacd-7188", scan_index >= unit_index);
	assert("jmacd-7189", scan_index <= unit_index + unit_width - 1);

	/* Depending on the scan direction, we set different maximum values for scan_index
	   (scan_max) and the number of nodes that would be passed if the scan goes the
	   entire way (scan_dist).  Incr is an integer reflecting the incremental
	   direction of scan_index. */
	if (scanning_left(scan)) {
		scan_max = unit_index;
		scan_dist = scan_index - unit_index;
		incr = -1;
	} else {
		scan_max = unit_index + unit_width - 1;
		scan_dist = scan_max - unit_index;
		incr = +1;
	}
	
	tree = coord.node->zjnode.tree;

	/* If the extent is allocated we have to check each of its blocks.  If the extent
	   is unallocated we can skip to the scan_max. */
	if (allocated) {
		do {
			neighbor = jlookup(tree, oid, scan_index);
			if (neighbor == NULL)
				goto stop_same_parent;

			ON_TRACE(TRACE_FLUSH_VERB, "alloc scan index %lu: %s\n",
				 scan_index, jnode_tostring(neighbor));

			if (scan->node != neighbor && !scan_goto(scan, neighbor)) {
				/* @neighbor was jput() by scan_goto(). */
				goto stop_same_parent;
			}

			ret = scan_set_current(scan, neighbor, 1, &coord);
			if (ret != 0) {
				goto exit;
			}

			/* reference to @neighbor is stored in @scan, no need
			   to jput(). */
			scan_index += incr;

		} while (incr + scan_max != scan_index);

	} else {
		/* Optimized case for unallocated extents, skip to the end. */
		neighbor = jlookup(tree, oid, scan_max/*index*/);
		if (neighbor == NULL) {
			/* Race with truncate */
			scan->stop = 1;
			ret = 0;
			goto exit;
		}

		ON_TRACE(TRACE_FLUSH_VERB, "unalloc scan index %lu: %s\n", scan_index, jnode_tostring(neighbor));

		/* XXX commented assertion out, because it is inherently
		 * racy */
		/* assert("jmacd-3551", !jnode_check_flushprepped(neighbor)
		   && same_slum_check(neighbor, scan->node, 0, 0)); */

		ret = scan_set_current(scan, neighbor, scan_dist, &coord);
		if (ret != 0) {
			goto exit;
		}
	}

	if (coord_sideof_unit(&coord, scan->direction) == 0 && item_is_extent(&coord)) {
		/* Continue as long as there are more extent units. */

		scan_index =
		    extent_unit_index(&coord) + (scanning_left(scan) ? extent_unit_width(&coord) - 1 : 0);
		goto repeat;
	}

	if (0) {
stop_same_parent:

		/* If we are scanning left and we stop in the middle of an allocated
		   extent, we know the preceder immediately.. */
		/* middle of extent is (scan_index - unit_index) != 0. */
		if (scanning_left(scan) && (scan_index - unit_index) != 0) {
			/* FIXME(B): Someone should step-through and verify that this preceder
			   calculation is indeed correct. */
			/* @unit_start is starting block (number) of extent
			   unit. Flush stopped at the @scan_index block from
			   the beginning of the file, which is (scan_index -
			   unit_index) block within extent.
			*/
			if (unit_start) {
				/* skip preceder update when we are at hole */
				scan->preceder_blk = unit_start + scan_index - unit_index;
				check_preceder(scan->preceder_blk);
			}
		}

		/* In this case, we leave coord set to the parent of scan->node. */
		scan->stop = 1;

	} else {
		/* In this case, we are still scanning, coord is set to the next item which is
		   either off-the-end of the node or not an extent. */
		assert("jmacd-8912", scan->stop == 0);
		assert("jmacd-7812", (coord_is_after_sideof_unit(&coord, scan->direction)
				      || !item_is_extent(&coord)));
	}

	ret = 0;
exit:
	return ret;
}

/* ask block allocator for some blocks */
static void
extent_allocate_blocks(reiser4_blocknr_hint *preceder,
		       reiser4_block_nr wanted_count, reiser4_block_nr *first_allocated, reiser4_block_nr *allocated, block_stage_t block_stage)
{
	*allocated = wanted_count;
	preceder->max_dist = 0;	/* scan whole disk, if needed */

	/* that number of blocks (wanted_count) is either in UNALLOCATED or in GRABBED */
	preceder->block_stage = block_stage;

	/* FIXME: we do not handle errors here now */
	check_me("vs-420", reiser4_alloc_blocks (preceder, first_allocated, allocated, BA_PERMANENT, "extent_allocate") == 0);
	/* update flush_pos's preceder to last allocated block number */
	preceder->blk = *first_allocated + *allocated - 1;
}

/* when on flush time unallocated extent is to be replaced with allocated one it may happen that one unallocated extent
   will have to be replaced with set of allocated extents. In this case insert_into_item will be called which may have
   to add new nodes into tree. Space for that is taken from inviolable reserve (5%). */
static reiser4_block_nr
reserve_replace(void)
{
	reiser4_block_nr grabbed, needed;

	grabbed = get_current_context()->grabbed_blocks;
	needed = estimate_one_insert_into_item(current_tree);
	check_me("vpf-340", !reiser4_grab_space_force(needed, BA_RESERVED));
	return grabbed;
}

static void
free_replace_reserved(reiser4_block_nr grabbed)
{
	reiser4_context *ctx;

	ctx = get_current_context();
	grabbed2free(ctx, get_super_private(ctx->super),
		     ctx->grabbed_blocks - grabbed);
}

/* if @key is glueable to the item @coord is set to */
static int
must_insert(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key last;

	if (item_id_by_coord(coord) == EXTENT_POINTER_ID && keyeq(append_key_extent(coord, &last), key))
		return 0;
	return 1;
}

/* before allocating unallocated extent we have to protect from eflushing those jnodes which are not eflushed yet and
   "unflush" jnodes which are already eflushed. However there can be too many eflushed jnodes so, we limit number of
   them with this macro */
#define JNODES_TO_UNFLUSH (16)

static int
unprotect_extent_nodes(oid_t oid, unsigned long ind, __u64 count)
{
#if REISER4_USE_EFLUSH
	__u64         i;
	reiser4_tree *tree;
	int	      unprotected;

	tree = current_tree;

	unprotected = 0;
	for (i = 0 ; i < count; ++ i, ++ ind) {
		jnode  *node;

		node = jlookup(tree, oid, ind);
		assert("nikita-3088", node != NULL);

		junprotect(node);
		jput(node);
		unprotected ++;
	}
	return unprotected;
#else
/* !REISER4_USE_EFLUSH */
	return count;
#endif
}

#if REISER4_DEBUG

static int
jnode_is_of_the_same_atom(jnode *node)
{
	int result;
	reiser4_context *ctx;
	txn_atom *atom;

	ctx = get_current_context();
	atom = get_current_atom_locked();

	LOCK_JNODE(node);
	LOCK_TXNH(ctx->trans);		
	assert("nikita-3304", ctx->trans->atom == atom);
	result = (node->atom == atom);
	UNLOCK_TXNH(ctx->trans);
	UNLOCK_JNODE(node);
	if (atom != NULL)
		UNLOCK_ATOM(atom);
	return result;
}

#endif


#if defined(OLD_FLUSH)

/* helper for extent_handle_overwrite_and_copy. It appends last item in the @node with @data if @data and last item are
   mergeable, otherwise insert @data after last item in @node. Have carry to put new data in available space only (that
   it to not allocate new nodes if target does not have enough space). This is because we are in squeezing.

   FIXME-VS: me might want to try to union last extent in item @left and @data
*/
static int
put_unit_to_end(znode *node, reiser4_key *key, reiser4_item_data *data)
{
	int result;
	coord_t coord;
	cop_insert_flag flags;

	/* set coord after last unit in an item */
	coord_init_last_unit(&coord, node);
	coord.between = AFTER_UNIT;

	flags = COPI_DONT_SHIFT_LEFT | COPI_DONT_SHIFT_RIGHT | COPI_DONT_ALLOCATE;
	if (must_insert(&coord, key)) {
		result = insert_by_coord(&coord, data, key, 0 /*lh */ , 0 /*ra */ ,
					 0 /*ira */ , flags);

	} else {
		/* try to append new extent unit */
		/* FIXME: put an assertion here that we can not merge last unit in @node and new unit */
		result = insert_into_item(&coord, 0 /*lh */ , key, data, flags);
	}

	assert("vs-438", result == 0 || result == -E_NODE_FULL);
	return result;
}

/* unallocated extent of width @count is going to be allocated. Protect all unformatted nodes from e-flushing. If
   unformatted node is eflushed already - it gets un-eflushed. Note, that it does not un-eflush more than JNODES_TO_UNFLUSH
   jnodes. All jnodes corresponding to this extent must exist */
static int
protect_extent_nodes(oid_t oid, unsigned long ind, __u64 count, __u64 *protected, int *ef, reiser4_extent *ext)
{
#if REISER4_USE_EFLUSH
	__u64           i;
	__u64           j;
	int             result;
	reiser4_tree   *tree;
	int             eflushed;
	jnode          *buf[JNODES_TO_UNFLUSH];

	tree = current_tree;

	eflushed = 0;
	if (ef)
		/*XXXX*/*ef = 0;
	*protected = 0;
	for (i = 0; i < count; ++i) {
		jnode  *node;

		node = jlookup(tree, oid, ind + i);
		/*
		 * all jnodes of unallocated extent should be in
		 * place. Truncate removes extent item together with jnodes
		 */
		assert("nikita-3087", node != NULL);

		LOCK_JNODE(node);

		assert("zam-836", !JF_ISSET(node, JNODE_EPROTECTED));
		assert("vs-1216", jnode_is_unformatted(node));
		if (ext) {
			if (state_of_extent(ext) == ALLOCATED_EXTENT)
				assert("", node->blocknr == extent_get_start(ext) + i);
			else
				assert("", blocknr_is_fake(jnode_get_block(node)));
		}
	
		JF_SET(node, JNODE_EPROTECTED);

		if (JF_ISSET(node, JNODE_EFLUSH)) {
			if (eflushed == JNODES_TO_UNFLUSH) {
				JF_CLR(node, JNODE_EPROTECTED);
				UNLOCK_JNODE(node);
				jput(node);
				break;
			}
			buf[eflushed] = node;
			eflushed ++;
			if (ef)
				(*ef) ++;
			UNLOCK_JNODE(node);
			jstartio(node);
		} else {
			UNLOCK_JNODE(node);
			jput(node);
		}

		(*protected) ++;
	}
	result = 0;
	for (j = 0 ; j < eflushed ; ++ j) {
		if (result == 0) {
			result = emergency_unflush(buf[j]);
			if (result != 0) {
				warning("nikita-3179",
					"unflush failed: %i", result);
				print_jnode("node", buf[j]);
			}
		}
		jput(buf[j]);
	}
	if (result != 0) {
		/* unprotect all the jnodes we have protected so far */
		unprotect_extent_nodes(oid, ind, i);
	}
	return result;
#else
/* !REISER4_USE_EFLUSH */
	*protected = count;
	return 0;
#endif
}

/* find slum starting from position set by @start and @pos_in_unit */
int
find_extent_slum_size(const coord_t *start, unsigned pos_in_unit)
{
	reiser4_tree *tree;
	oid_t oid;
	unsigned long index, first;
	jnode *node;
	unsigned slum_size;
	int slum_done;
	unsigned i; /* position within an unit */
	coord_t coord;
	reiser4_key key;
	reiser4_extent *ext;
	reiser4_block_nr width;

	tree = current_tree;

	assert ("vs-1387", item_is_extent(start));
	coord_dup(&coord, start);

	oid = get_key_objectid(item_key_by_coord(&coord, &key));
	index = extent_unit_index(&coord) + pos_in_unit;
	first = index;

	ON_TRACE(TRACE_BUG, "find_extent_slum_size: start from page %lu, [item %d, unit %d, pos_in_unit %u] ext:[%llu/%llu] of oid %llu\n",
		 index, coord.item_pos, coord.unit_pos, pos_in_unit, extent_unit_start(&coord), extent_unit_width(&coord), oid);

	assert("vs-1407", jnode_is_of_the_same_atom(ZJNODE(coord.node)));

	slum_size = 0;
	slum_done = 0;
	do {
		if (item_id_by_coord(&coord) == FROZEN_EXTENT_POINTER_ID)
			break;

		ext = extent_by_coord(&coord);
		width = extent_get_width(ext);
		switch (state_of_extent(ext)) {
		case ALLOCATED_EXTENT:
			for (i = pos_in_unit; i < width; i ++) {
				node = jlookup(tree, oid, index);
				if (!node) {
					slum_done = 1;
					break;
				}
				if (jnode_check_flushprepped(node)) {
					jput(node);
					slum_done = 1;
					break;
				}

				assert("vs-1408", jnode_is_of_the_same_atom(node));
				jput(node);
				slum_size ++;
				index ++;
			}
			break;

		case HOLE_EXTENT:
			/* slum does not break at hole */
			assert("vs-1384", pos_in_unit == 0);
			index += extent_get_width(ext);
			break;

		case UNALLOCATED_EXTENT:
			assert("vs-1388", pos_in_unit == 0);
			ON_DEBUG(
				for (i = 0; i < width; i ++) {
					node = jlookup(tree, oid, index + i);
					assert("vs-1426", ergo(node == 0, i == width - 1));
					assert("vs-1132", ergo(node, blocknr_is_fake(jnode_get_block(node))));

					/* last jnode of extent can be flushprepped (not dirty actually): because
					   write_extent first creates jnode and extent item and unlocks twig node to go
					   to page allocation. Jnode is captured and marked dirty after data are copied
					   to page. So flush may encounter jnode corresponding to unallocated extent and
					   is neither captured nor dirty */
					assert("vs-1426", ergo(i != width - 1, !jnode_check_flushprepped(node)));
					assert("vs-1408", ergo(i != width - 1, jnode_is_of_the_same_atom(node)));
					if (node)
						jput(node);
				});
			slum_size += width;			
			index += width;

			/* check last jnode of unallocated extent. It may be not a part of slum yet */
			node = jlookup(tree, oid, index - 1);
			if (!node || jnode_check_flushprepped(node)) {
				/* last jnode of unit is part of slum */
				slum_size --;
				index --;
				slum_done = 1;
			}
			if (node)
				jput(node);
			break;

		case UNALLOCATED_EXTENT2:
			assert("vs-1420", 0);
		}

		if (slum_done || coord_next_unit(&coord) || !item_is_extent(&coord))
			break;

		pos_in_unit = 0;
		if (coord.unit_pos == 0) {
			/* we switched to new item */
			assert("vs-1394", oid != get_key_objectid(item_key_by_coord(&coord, &key)));
			index = 0;
			oid = get_key_objectid(item_key_by_coord(&coord, &key));
		}

		ON_TRACE(TRACE_BUG, "find_extent_slum_size: slum size %u. Next page %lu. "
			 "Coord: [item %d, unit %d, pos_in_unit %u] ext:[%llu/%llu] of oid %llu\n",
			 slum_size, index, coord.item_pos, coord.unit_pos, pos_in_unit,
			 extent_unit_start(&coord), extent_unit_width(&coord), oid);
	} while (1);

	return slum_size;
}

/* replace allocated extent with two allocated extents */
static int
split_allocated_extent(coord_t *coord, reiser4_block_nr pos_in_unit)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent replace_ext;
	reiser4_extent append_ext;
	reiser4_key key;
	reiser4_item_data item;
	reiser4_block_nr grabbed;

	ext = extent_by_coord(coord);
	assert("vs-1410", state_of_extent(ext) == ALLOCATED_EXTENT);
	assert("vs-1411", extent_get_width(ext) > pos_in_unit);

	set_extent(&replace_ext, extent_get_start(ext), pos_in_unit);
	set_extent(&append_ext, extent_get_start(ext) + pos_in_unit, extent_get_width(ext) - pos_in_unit);

	/* insert_into_item will insert new units after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + pos_in_unit * current_blocksize));

	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node, ZNODE_WRITE_LOCK), &key, init_new_extent(&item, &append_ext, 1),
				&replace_ext, COPI_DONT_SHIFT_LEFT);
	free_replace_reserved(grabbed);
	return result;
}

/* replace allocated extent with unallocated or with unallocated and allocated */
static int
allocated2unallocated(coord_t *coord, reiser4_block_nr ue_width)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent append_ext;
	reiser4_extent replace_ext;
	reiser4_block_nr ae_first_block;
	reiser4_block_nr grabbed;
	reiser4_block_nr ae_width;
	reiser4_item_data item;
	reiser4_key key;

	ext = extent_by_coord(coord);
	ae_first_block = extent_get_start(ext);
	ae_width = extent_get_width(ext);
	
	ON_TRACE(TRACE_BUG, "allocated2unallocated: oid %llu, item_pos %d unit_pos %d. index %llu orig [%llu %llu]->",
		 get_key_objectid(item_key_by_coord(coord, &key)),
		 coord->item_pos, coord->unit_pos,
		 extent_unit_index(coord), extent_get_start(ext), extent_get_width(ext));

	if (ae_width == ue_width) {
		/* 1 */
		set_extent(ext, UNALLOCATED_EXTENT_START2, ae_width);
		znode_make_dirty(coord->node);

		ON_TRACE(TRACE_BUG, "[%llu %llu]\n", extent_get_start(ext), extent_get_width(ext));
		return 0;
	}

	/* replace ae with ue and ae unallocated extent */
	set_extent(&replace_ext, UNALLOCATED_EXTENT_START2, ue_width);
	/* unit to be inserted */
	set_extent(&append_ext, ae_first_block + ue_width, ae_width - ue_width);

	ON_TRACE(TRACE_BUG, "[%llu %llu][%llu %llu]\n",
		 extent_get_start(&replace_ext), extent_get_width(&replace_ext),
		 extent_get_start(&append_ext), extent_get_width(&append_ext));

	/* insert_into_item will insert new units after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + extent_get_width(&replace_ext) * current_blocksize));
	
	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node, ZNODE_WRITE_LOCK), &key, init_new_extent(&item, &append_ext, 1),
				&replace_ext, COPI_DONT_SHIFT_LEFT);
	free_replace_reserved(grabbed);
	return result;
}

/* changed jnode fake block numbers by real ones, make them RELOC, unprotect */
static void
assign_real_blocknrs(oid_t oid, unsigned long index, reiser4_block_nr first,
		     /* FIXME-VS: get better type for number of
			blocks */
		     reiser4_block_nr count, flush_pos_t *flush_pos)
{
	jnode *j;
	int i;
	reiser4_tree *tree = current_tree;

	for (i = 0; i < (int) count; i++, first++, index ++) {
		j = jlookup(tree, oid, index);
		assert("vs-1401", j);
		assert("vs-1412", JF_ISSET(j, JNODE_EPROTECTED));
		assert("vs-1132", blocknr_is_fake(jnode_get_block(j)));
		jnode_set_block(j, &first);
		
		/* this node can not be from overwrite set */
		assert("jmacd-61442", !JF_ISSET(j, JNODE_OVRWR));
		jnode_make_reloc(j, pos_fq(flush_pos));
		junprotect(j);
		jput(j);
	}

	return;
}

/* changed jnode real block numbers by fake ones */
static void
assign_fake_blocknrs(oid_t oid, unsigned long index, reiser4_block_nr count)
{
	jnode *j;
	int i;
	reiser4_tree *tree = current_tree;

	for (i = 0; i < count; i ++, index ++) {
		j = jlookup(tree, oid, index);
		assert("vs-1367", j);
		assert("vs-1363", !jnode_check_flushprepped(j));
		assert("vs-1412", JF_ISSET(j, JNODE_EPROTECTED));
		assert("vs-1132", !blocknr_is_fake(jnode_get_block(j)));

		j->blocknr = fake_blocknr_unformatted();
		jput(j);
	}
	return;
}

/* changed jnode real block numbers by another real ones */
static int
change_jnode_blocknrs(oid_t oid, unsigned long index, reiser4_block_nr first,
		      reiser4_block_nr count, flush_pos_t *flush_pos)
{
	jnode *j;
	int i;
	reiser4_tree *tree = current_tree;

	for (i = 0; i < (int) count; i++, first++, index ++) {
		j = jlookup(tree, oid, index);
		assert("vs-1401", j);
		jnode_set_block(j, &first);

		/* If we allocated it cannot have been wandered -- in that case extent_needs_allocation returns 0. */
		assert("jmacd-61442", !jnode_check_flushprepped(j));
		jnode_make_reloc(j, pos_fq(flush_pos));
		junprotect(j);
		jput(j);
	}

	return 0;
}


/* we are replacing extent @ext by extent @replace. Try to merge replace with previous extent of the item (if there is
 * one). Return 1 if it succeeded, 0 - otherwise */
static int
try_to_merge_with_left(coord_t *coord, reiser4_extent *ext, reiser4_extent *replace)
{
	assert("vs-1415", extent_by_coord(coord) == ext);
	assert("vs-1416", extent_get_width(ext) == extent_get_width(replace));

	if (coord->unit_pos == 0 || state_of_extent(ext - 1) != ALLOCATED_EXTENT)
		return 0;
	if (extent_get_start(ext - 1) + extent_get_width(ext - 1) == extent_get_start(replace)) {
		coord_t from, to;

		extent_set_width(ext - 1, extent_get_width(ext - 1) + extent_get_width(ext));

		coord_dup(&from, coord);
		from.unit_pos = nr_units_extent(coord) - 1;
		coord_dup(&to, &from);

		/* FIXME: this is because currently cut from extent can cut either from the beginning or from the end
		   only */
		xmemmove(ext, ext + 1, (from.unit_pos - coord->unit_pos) * sizeof(reiser4_extent));
		/* wipe part of item which is going to be cut, so that node_check will not be confused by extent
		   overlapping */
		ON_DEBUG(xmemset(extent_item(coord) + from.unit_pos, 0, sizeof (reiser4_extent)));
		cut_node(&from,
			 &to,
			 0,
			 0,
			 0,
			 0,
			 0,
			 0/*inode*/);
		coord->unit_pos --;
		return 1;
	}
	return 0;
}

/* flush_pos is set to extent unit. Slum starts from flush_pos->pos_in_unit within this unit. This function may perform
   complex extent convertion. It (extent) may be either converted to allocated or to mixture of allocated and
   unallocated extents. */
int
extent_handle_relocate_in_place(flush_pos_t *flush_pos, unsigned *slum_size)
{
	int result;
	reiser4_extent *ext;
	reiser4_block_nr first_allocated;
	reiser4_extent replace, paste;
	reiser4_item_data item;
	reiser4_key key;
	reiser4_block_nr grabbed;
	unsigned long index;
	oid_t oid;
	__u64 protected, allocated;
	coord_t *coord;
	unsigned extent_slum_size;
	reiser4_block_nr start, width;
	extent_state state;
	/*XXXX*/int eflushed;

	coord = &flush_pos->coord;
	assert("vs-1019", item_is_extent(coord));
	assert("vs-1018", coord_is_existing_unit(coord));
	assert("zam-807", znode_is_write_locked(coord->node));

	ext = extent_by_coord(coord);
	start = extent_get_start(ext);
	width = extent_get_width(ext);
	oid = get_key_objectid(item_key_by_coord(coord, &key));
	
	/* number of not flushprepped children of this extent. There must be exactly that amount of them */
	extent_slum_size = width - flush_pos->pos_in_unit;
	if (extent_slum_size > *slum_size)
		extent_slum_size = *slum_size;

	state = state_of_extent(ext);

	/* skip unit which is part of item being converted into tail because it is going to be removed soon */
	if (item_id_by_coord(coord) == FROZEN_EXTENT_POINTER_ID) {
		if (state != HOLE_EXTENT)
			*slum_size -= extent_slum_size;
		return 0;
	}

	if (state == HOLE_EXTENT)
		/* hole does not break "slum" */
		return 0;

	if (state == ALLOCATED_EXTENT && flush_pos->pos_in_unit) {
		/* first flush_pos->pos_in_unit nodes of the extent are not slum. Convert [start/width] to
		   [start/pos_in_unit][start + pos_in_unit/width - pos_in_unit] */
		result = split_allocated_extent(coord, flush_pos->pos_in_unit);
		if (result)
			return result;
		assert("vs-1404", extent_by_coord(coord) == ext);
		return 0;
	}

	assert("vs-1404", extent_by_coord(coord) == ext);
	assert("vs-1403", flush_pos->pos_in_unit == 0);
	index = extent_unit_index(coord);

	/*
	 * eflush<->extentallocation interactions.
	 *
	 * If node from unallocated extent was eflushed following bad
	 * things can happen:
	 *
	 *   . block reserved for this node in fake block space is
	 *   already used, and extent_allocate_blocks() will underflow
	 *   fake_allocated counter.
	 *
	 *   . emergency block is marked as used in bitmap, so it is
	 *   possible for extent_allocate_blocks() to just be unable
	 *   to find enough free space on the disk.
	 *
	 */
	
	/* Prevent node from e-flushing before allocating disk space for them. Nodes which were eflushed will be read
	   from their temporary locations (but not more than certain limit: JNODES_TO_UNFLUSH) and that disk space will
	   be freed. */
 	result = protect_extent_nodes(oid, index, extent_slum_size, &protected, &eflushed, ext);
	if (result)
		return result;

	if (state == ALLOCATED_EXTENT) {
		txn_atom *atom;
		reiser4_block_nr first, count;

		assert("vs-1413", extent_is_allocated(coord));

		atom = get_current_atom_locked();
		/* All additional blocks needed for safe writing of modified extent are counted in atom's flush reserved
		   counted.  Here we move that amount to "grabbed" counter for further spending it in
		   assign_fake_blocknr(). Thus we convert "flush reserved" space to "unallocated" one reflecting that
		   extent allocation status change. */
		flush_reserved2grabbed(atom, protected);

		UNLOCK_ATOM(atom);

		first = extent_get_start(ext);
		count = protected;

		/* replace allocated extent with unallocated one, assign fake blocknrs to all protected jnodes */
		result = allocated2unallocated(coord, protected);
		if (result) {
			warning("vs-1414", "failed to convert allocated extent to unallocated %d", eflushed);
			return result;
		}

		/* assign fake block numbers to all jnodes */
		assign_fake_blocknrs(oid, index, protected);

		reiser4_dealloc_blocks(&start, &count, BLOCK_ALLOCATED,
				       BA_DEFER);
	}

	extent_slum_size = protected;
	assert("vs-1422", ext == extent_by_coord(coord));
	assert("vs-1423", state_of_extent(ext) == UNALLOCATED_EXTENT || state_of_extent(ext) == UNALLOCATED_EXTENT2);
	check_me("vs-1138", extent_allocate_blocks(pos_hint(flush_pos),
						   extent_slum_size, &first_allocated, &allocated) == 0);
	assert("vs-440", allocated > 0 && allocated <= extent_slum_size);
	if (allocated < extent_slum_size)
		/* unprotect nodes which will not be allocated on this iteration */
		unprotect_extent_nodes(oid, index + allocated, extent_slum_size - allocated);

	/* find all jnodes for which blocks were allocated and assign block numbers to them, call jnode_make_reloc and
	   unprotect (they are now protected by JNODE_FLUSH_QUEUED bit) */
	assign_real_blocknrs(oid, index, first_allocated, allocated, flush_pos);
	index += allocated;

	/* compose extent which will replace current one */
	set_extent(&replace, first_allocated, allocated);
	if (allocated == extent_get_width(ext)) {
		/* whole extent is allocated */
		assert("vs-1425", allocated == extent_slum_size);
		ON_TRACE(TRACE_BUG, "extent_handle_relocate_in_place: oid %llu, item_pos %d, unit_pos %d. orig [%llu %llu]->[%llu %llu] %d\n",
			 oid, coord->item_pos, coord->unit_pos,
			 extent_unit_start(coord), extent_unit_width(coord),
			 extent_get_start(&replace), extent_get_width(&replace), eflushed);

		if (!try_to_merge_with_left(coord, ext, &replace))
			*ext = replace;

		/* no need to grab space as it is done already */
		znode_make_dirty(coord->node);

		*slum_size -= allocated;
		return 0;
	}
	assert("vs-1429", extent_get_width(ext) > allocated);

	/* set @key to key of first byte of part of extent which left unallocated */
	set_key_offset(&key, (__u64)index << PAGE_CACHE_SHIFT);
	set_extent(&paste, UNALLOCATED_EXTENT_START, extent_get_width(ext) - allocated);

	/* [u/width] ->
	   [first_allocated/allocated][u/width - allocated] */
	ON_TRACE(TRACE_BUG, "extent_handle_relocate_in_place: oid %llu, item_pos %d, unit_pos %d. orig [%llu %llu]->[%llu %llu][%llu %llu]\n",
		 oid, coord->item_pos, coord->unit_pos,
		 extent_unit_start(coord), extent_unit_width(coord),
		 extent_get_start(&replace), extent_get_width(&replace),
		 extent_get_start(&paste), extent_get_width(&paste));
	
	/* space for this operation is not reserved. reserve it from inviolable reserve */
	grabbed = reserve_replace();
	result = replace_extent(coord, &flush_pos->lock, &key,
				init_new_extent(&item, &paste, 1), &replace, COPI_DONT_SHIFT_LEFT);
	free_replace_reserved(grabbed);
	if (!result)
		*slum_size -= allocated;
	return result;
}

/* flush_pos is set to extent unit. Slum starts from flush_pos->pos_in_unit within this unit. Put all nodes of slum to
   overwrite set */
int
extent_handle_overwrite_in_place(flush_pos_t *flush_pos, unsigned *slum_size)
{
	unsigned i;
	reiser4_key key;
	oid_t oid;
	reiser4_tree *tree;
	unsigned long index;
	jnode *j;
	unsigned extent_slum_size;

	assert("vs-1019", item_is_extent(&flush_pos->coord));
	assert("vs-1018", coord_is_existing_unit(&flush_pos->coord));
	assert("zam-807", znode_is_write_locked(flush_pos->coord.node));

	oid = get_key_objectid(item_key_by_coord(&flush_pos->coord, &key));
	tree = current_tree;
	index = extent_unit_index(&flush_pos->coord) + flush_pos->pos_in_unit;

	extent_slum_size = extent_unit_width(&flush_pos->coord) - flush_pos->pos_in_unit;
	if (extent_slum_size > *slum_size)
		extent_slum_size = *slum_size;

	/* skip unit which is part of item being converted into tail because
	 * it is going to be removed soon */
	if (item_id_by_coord(&flush_pos->coord) == FROZEN_EXTENT_POINTER_ID) {
		reiser4_extent *ext;

		ext = extent_by_coord(&flush_pos->coord);
		if (state_of_extent(ext) != HOLE_EXTENT)
			*slum_size -= extent_slum_size;
		return 0;
	}
	for (i = 0; i < extent_slum_size; i ++) {
		j = jlookup(tree, oid, index + i);
		assert("vs-1396", j && !jnode_check_flushprepped(j));
		jnode_make_wander(j);
		jput(j);
	}
	*slum_size -= extent_slum_size;
	return 0;
}

static int
try_merge(znode *left, oid_t oid, reiser4_block_nr start, reiser4_block_nr width)
{
	coord_t coord;
	reiser4_key key;
	reiser4_block_nr unit_start;
	reiser4_block_nr unit_width;

	assert("vs-1401", !node_is_empty(left));
	
	coord_init_last_unit(&coord, left);
	if (!item_is_extent(&coord))
		return 0;
	if (oid != get_key_objectid(item_key_by_coord(&coord, &key)))
		return 0;
	unit_start = extent_unit_start(&coord);
	unit_width = extent_unit_width(&coord);
	if ((unit_start == 0 && start == 0) ||
	    (unit_start + width == start)) {
		extent_set_width(extent_by_coord(&coord), unit_width + width);	
		znode_make_dirty(left);
		return 1;
	}
	return 0;
}

static squeeze_result
try_copy(znode *left, oid_t oid, reiser4_block_nr start, reiser4_block_nr width, reiser4_key *key)
{
	squeeze_result result;

	result = SQUEEZE_CONTINUE;
	if (!try_merge(left, oid, start, width)) {
		reiser4_extent new_ext;
		reiser4_item_data data;
		
		extent_set_start(&new_ext, start);
		extent_set_width(&new_ext, width);
		result = put_unit_to_end(left, key, init_new_extent(&data, &new_ext, 1));
		if (result == -E_NODE_FULL)
			result = SQUEEZE_TARGET_FULL;
	}
	return result;
}

/* @right is set to extent unit. */
int
extent_handle_relocate_and_copy(znode *left, coord_t *right, flush_pos_t *flush_pos, unsigned *slum_size,
				reiser4_key *stop_key)
{
	int result;
	reiser4_extent *ext;
	reiser4_block_nr first_allocated;
	reiser4_key key;
	unsigned long index;
	oid_t oid;
	__u64 protected, allocated;
	unsigned extent_slum_size, done;
	reiser4_block_nr start, width;
	extent_state state;

	assert("vs-1019", item_is_extent(right));
	assert("vs-1018", coord_is_existing_unit(right));
	assert("zam-807", znode_is_write_locked(right->node));
	assert("zam-807", znode_is_write_locked(left));

	ext = extent_by_coord(right);
	start = extent_get_start(ext);
	width = extent_get_width(ext);
	oid = get_key_objectid(item_key_by_coord(right, &key));
	
	/* number of not flushprepped children of this extent. There must be exactly that amount of them */
	extent_slum_size = width;
	if (extent_slum_size > *slum_size)
		extent_slum_size = *slum_size;

	state = state_of_extent(ext);

	/* skip unit which is part of item being converted into tail because it is going to be removed soon */
	if (item_id_by_coord(right) == FROZEN_EXTENT_POINTER_ID) {
		if (state != HOLE_EXTENT)
			*slum_size -= extent_slum_size;
		return SQUEEZE_TARGET_FULL;
	}

	if (state == HOLE_EXTENT)
		return try_copy(left, oid, 0, width, &key);

	if (state == ALLOCATED_EXTENT) {
		txn_atom *atom;

		atom = get_current_atom_locked();
		/* All additional blocks needed for safe writing of modified extent are counted in atom's flush reserved
		   counted.  Here we move that amount to "grabbed" counter for further spending it in
		   assign_fake_blocknr(). Thus we convert "flush reserved" space to "unallocated" one reflecting that
		   extent allocation status change. */
		flush_reserved2grabbed(atom, extent_slum_size);
		UNLOCK_ATOM(atom);
	}

	index = extent_unit_index(right);
	done = 0;
	do {
		/* Prevent node from e-flushing before allocating disk space for them. Nodes which were eflushed will be
		   read from their temporary locations (but not more than certain limit: JNODES_TO_UNFLUSH) and that
		   disk space will be freed. */
		result = protect_extent_nodes(oid, index, extent_slum_size, &protected, 0, 0);
		if (result)
			break;
		do {
			check_me("vs-1137", extent_allocate_blocks(pos_hint(flush_pos),
								   protected, &first_allocated, &allocated) == 0);
			if (try_copy(left, oid, first_allocated, allocated, &key) == SQUEEZE_TARGET_FULL) {
				if (state == ALLOCATED_EXTENT)
					grabbed2flush_reserved(extent_slum_size);
				unprotect_extent_nodes(oid, index, protected);
				reiser4_dealloc_blocks(&first_allocated, &allocated, BLOCK_ALLOCATED, 0);
				return SQUEEZE_TARGET_FULL;
			}

			/* FIXME: add error handling */
			check_me("vs-1400", reiser4_dealloc_blocks(&start, &allocated,
								   BLOCK_ALLOCATED, BA_DEFER) == 0);
			change_jnode_blocknrs(oid, index, first_allocated, allocated, flush_pos);
			index += allocated;
			protected -= allocated;
			extent_slum_size -= allocated;
			done += allocated;
			set_key_offset(&key, (__u64)index << PAGE_CACHE_SHIFT);

			/* update stop key */
			*stop_key = key;
			set_key_offset(stop_key, ((__u64)index << PAGE_CACHE_SHIFT) - 1);
		} while (protected);
	} while (extent_slum_size);

	*slum_size -= done;
	return SQUEEZE_CONTINUE;
}

/* @right is set to allocated extent. Its first @extent_slum_size (calculated in this function) children are not
   flushprepped. Move all those children to overwrite set (by jnode_make_wander) and copy unit of width @extent_slum_size
   to the end of left neighbor. Update stop_key to new biggest key on @left */
int
extent_handle_overwrite_and_copy(znode *left, coord_t *right, flush_pos_t *flush_pos, unsigned *slum_size,
				 reiser4_key *stop_key)
{
	int result;
	unsigned i;
	reiser4_key key;
	oid_t oid;
	reiser4_tree *tree;
	unsigned long index;
	jnode *j;
	unsigned extent_slum_size;
	reiser4_item_data data;
	reiser4_extent ext;

	assert("vs-1019", item_is_extent(right));
	assert("vs-1018", coord_is_existing_unit(right));
	assert("zam-807", znode_is_write_locked(right->node));
	assert("zam-807", znode_is_write_locked(left));

	oid = get_key_objectid(item_key_by_coord(right, &key));
	tree = current_tree;
	index = extent_unit_index(right);
	extent_slum_size = extent_unit_width(&flush_pos->coord);
	if (extent_slum_size > *slum_size)
		extent_slum_size = *slum_size;

	for (i = 0; i < extent_slum_size; i ++) {
		j = jlookup(tree, oid, index + i);
		assert("vs-1396", j && !jnode_check_flushprepped(j));
		jnode_make_wander(j);
		jput(j);
	}

	/* unit does not require allocation, copy this unit as it is */
	extent_set_start(&ext, extent_unit_start(right));
	extent_set_width(&ext, extent_slum_size);
	result = put_unit_to_end(left, &key, init_new_extent(&data, &ext, 1));
	if (result == -E_NODE_FULL) {
		/* left->node does not have enough free space for this unit */
		ON_TRACE(TRACE_EXTENTS, "alloc_and_copy_extent: target full, !needs_allocation\n");
		return SQUEEZE_TARGET_FULL;
	}
	/* update stop key */
	*stop_key = key;
	set_key_offset(stop_key, ((__u64)(index + extent_slum_size) << PAGE_CACHE_SHIFT) - 1);

	*slum_size -= extent_slum_size;
	return SQUEEZE_CONTINUE;
}

#endif /* OLD_FLUSH */

/* Block offset of first block addressed by unit */
__u64
extent_unit_index(const coord_t *item)
{
	reiser4_key key;

	assert("vs-648", coord_is_existing_unit(item));
	unit_key_by_coord(item, &key);
	return get_key_offset(&key) >> current_blocksize_bits;
}

/* AUDIT shouldn't return value be of reiser4_block_nr type?
   Josh's answer: who knows?  Is a "number of blocks" the same type as "block offset"? */
__u64
extent_unit_width(const coord_t *item)
{
	assert("vs-649", coord_is_existing_unit(item));
	return width_by_coord(item);
}

/* Starting block location of this unit */
reiser4_block_nr
extent_unit_start(const coord_t *item)
{
	return extent_get_start(extent_by_coord(item));
}

static void
check_protected_node(jnode *node, reiser4_extent *ext, __u64 pos_in_unit)
{
#if REISER4_DEBUG
	assert("zam-836", !JF_ISSET(node, JNODE_EPROTECTED));
	assert("vs-1216", jnode_is_unformatted(node));
	if (ext) {
		if (state_of_extent(ext) == ALLOCATED_EXTENT)
			assert("vs-1463", node->blocknr == extent_get_start(ext) + pos_in_unit);
		else
			assert("vs-1464", blocknr_is_fake(jnode_get_block(node)));
	}
#endif
}


#define NEW_FLUSH
#if defined(NEW_FLUSH)

#define TRACE_FLUSH 0

/* replace allocated extent with two allocated extents */
static int
split_allocated_extent(coord_t *coord, reiser4_block_nr pos_in_unit)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent replace_ext;
	reiser4_extent append_ext;
	reiser4_key key;
	reiser4_item_data item;
	reiser4_block_nr grabbed;

	ext = extent_by_coord(coord);
	assert("vs-1410", state_of_extent(ext) == ALLOCATED_EXTENT);
	assert("vs-1411", extent_get_width(ext) > pos_in_unit);

	set_extent(&replace_ext, extent_get_start(ext), pos_in_unit);
	set_extent(&append_ext, extent_get_start(ext) + pos_in_unit, extent_get_width(ext) - pos_in_unit);

	/* insert_into_item will insert new units after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + pos_in_unit * current_blocksize));

#if TRACE_FLUSH
	printk("split [%llu %llu] -> [%llu %llu][%llu %llu]\n", extent_get_start(ext), extent_get_width(ext),
	       extent_get_start(&replace_ext), extent_get_width(&replace_ext),
	       extent_get_start(&append_ext), extent_get_width(&append_ext));
#endif

	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node, ZNODE_WRITE_LOCK), &key, init_new_extent(&item, &append_ext, 1),
				&replace_ext, COPI_DONT_SHIFT_LEFT);
	free_replace_reserved(grabbed);
	return result;
}

static int
protect_extent_nodes(oid_t oid, unsigned long index, reiser4_block_nr width, reiser4_block_nr *protected, reiser4_extent *ext)
{
	__u64           i;
	__u64           j;
	int             result;
	reiser4_tree   *tree;
	int             eflushed;
	jnode          *buf[JNODES_TO_UNFLUSH];

	tree = current_tree;

	eflushed = 0;
	*protected = 0;
	for (i = 0; i < width; ++i, ++index) {
		jnode  *node;

		node = jlookup(tree, oid, index);
		if (!node)
			break;		

		if (jnode_check_flushprepped(node)) {
			jput(node);
			break;
		}

		LOCK_JNODE(node);

		check_protected_node(node, ext, i);
	
		JF_SET(node, JNODE_EPROTECTED);

		if (JF_ISSET(node, JNODE_EFLUSH)) {
			if (eflushed == JNODES_TO_UNFLUSH) {
				JF_CLR(node, JNODE_EPROTECTED);
				UNLOCK_JNODE(node);
				jput(node);
				break;
			}
			buf[eflushed] = node;
			eflushed ++;
			UNLOCK_JNODE(node);
			jstartio(node);
		} else {
			UNLOCK_JNODE(node);
			jput(node);
		}

		(*protected) ++;
	}
	result = 0;
	for (j = 0 ; j < eflushed ; ++ j) {
		if (result == 0) {
			result = emergency_unflush(buf[j]);
			if (result != 0) {
				warning("nikita-3179",
					"unflush failed: %i", result);
				print_jnode("node", buf[j]);
			}
		}
		jput(buf[j]);
	}
	if (result != 0) {
		/* unprotect all the jnodes we have protected so far */
		unprotect_extent_nodes(oid, index, i);
	}
	return result;
}

/* we are replacing extent @ext by extent @replace. Try to merge @replace with previous extent of the item (if there is
   one). Return 1 if it succeeded, 0 - otherwise */
static int
try_to_merge_with_left(coord_t *coord, reiser4_extent *ext, reiser4_extent *replace)
{
	assert("vs-1415", extent_by_coord(coord) == ext);

	if (coord->unit_pos == 0 || state_of_extent(ext - 1) != ALLOCATED_EXTENT)
		/* @ext either does not exist or is not allocated extent */
		return 0;
	if (extent_get_start(ext - 1) + extent_get_width(ext - 1) != extent_get_start(replace))
		return 0;

	/* we can clue, update width of previous unit */
#if TRACE_FLUSH
	printk("wide previous [%llu %llu] ->", extent_get_start(ext - 1), extent_get_width(ext - 1));
#endif

	extent_set_width(ext - 1, extent_get_width(ext - 1) + extent_get_width(replace));
	znode_make_dirty(coord->node);

#if TRACE_FLUSH
	printk(" [%llu %llu] -> ", extent_get_start(ext - 1), extent_get_width(ext - 1));
#endif

	if (extent_get_width(ext) != extent_get_width(replace)) {
		/* shrink @ext */
#if TRACE_FLUSH
		printk("shrink [%llu %llu] -> ", extent_get_start(ext), extent_get_width(ext));
#endif
		if (state_of_extent(ext) == ALLOCATED_EXTENT)
			extent_set_start(ext, extent_get_start(ext) + extent_get_width(replace));
		extent_set_width(ext, extent_get_width(ext) - extent_get_width(replace));		
#if TRACE_FLUSH
		printk("[%llu %llu]\n", extent_get_start(ext), extent_get_width(ext));
#endif
	} else {
		/* remove @ext */
		coord_t from, to;
		
#if TRACE_FLUSH
		printk("delete [%llu %llu]\n", extent_get_start(ext), extent_get_width(ext));
#endif
		coord_dup(&from, coord);
		from.unit_pos = nr_units_extent(coord) - 1;
		coord_dup(&to, &from);
		
		/* FIXME: this is because currently cut from extent can cut either from the beginning or from the end
		   only */
		xmemmove(ext, ext + 1, (from.unit_pos - coord->unit_pos) * sizeof(reiser4_extent));
		/* wipe part of item which is going to be cut, so that node_check will not be confused by extent
		   overlapping */
		ON_DEBUG(xmemset(extent_item(coord) + from.unit_pos, 0, sizeof (reiser4_extent)));
		cut_node(&from, &to, 0, 0, 0, DELETE_DONT_COMPACT, 0, 0/*inode*/);
	}
	/* move coord back */
	coord->unit_pos --;
	return 1;
}

/* replace extent (unallocated or allocated) pointed by @coord with extent @replace (allocated). If @replace is shorter
   than @coord - add padding extent */
static int
conv_extent(coord_t *coord, reiser4_extent *replace)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent padd_ext;
	reiser4_block_nr start, width, new_width;
	reiser4_block_nr grabbed;
	reiser4_item_data item;
	reiser4_key key;
	extent_state state;

	ext = extent_by_coord(coord);
	state = state_of_extent(ext);
	start = extent_get_start(ext);
	width = extent_get_width(ext);
	new_width = extent_get_width(replace);

	assert("vs-1458", state == UNALLOCATED_EXTENT || state == ALLOCATED_EXTENT);
	assert("vs-1459", width >= new_width);

	if (try_to_merge_with_left(coord, ext, replace)) {
		return 0;
	}

	if (width == new_width) {
		znode_make_dirty(coord->node);
		*ext = *replace;
#if TRACE_FLUSH
		printk("replace: [%llu %llu]->[%llu %llu]\n",
		       start, width,
		       extent_get_start(replace), extent_get_width(replace));
#endif
		return 0;
	}

	/* replace @ext with @replace and padding extent */
	set_extent(&padd_ext, state == ALLOCATED_EXTENT ? (start + new_width) : UNALLOCATED_EXTENT_START,
		   width - new_width);

	/* insert_into_item will insert new units after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + new_width * current_blocksize));
	
#if TRACE_FLUSH
	printk("replace: [%llu %llu]->[%llu %llu][%llu %llu]\n",
	       start, width,
	       extent_get_start(replace), extent_get_width(replace),
	       extent_get_start(&padd_ext), extent_get_width(&padd_ext));
#endif
	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node, ZNODE_WRITE_LOCK), &key, init_new_extent(&item, &padd_ext, 1),
				replace, COPI_DONT_SHIFT_LEFT);

	free_replace_reserved(grabbed);
	return result;
}

static void
assign_real_blocknrs(oid_t oid, unsigned long index, reiser4_block_nr first,
		     reiser4_block_nr count, flush_pos_t *flush_pos, extent_state state)
{
	jnode *j;
	int i;
	reiser4_tree *tree = current_tree;

	for (i = 0; i < (int) count; i++, first++, index ++) {
		j = jlookup(tree, oid, index);
		assert("vs-1401", j);
		assert("vs-1412", JF_ISSET(j, JNODE_EPROTECTED));
		assert("vs-1460", !JF_ISSET(j, JNODE_EFLUSH));
		assert("vs-1132", ergo(state == UNALLOCATED_EXTENT, blocknr_is_fake(jnode_get_block(j))));
		jnode_set_block(j, &first);
		
		/* this node can not be from overwrite set */
		assert("jmacd-61442", !JF_ISSET(j, JNODE_OVRWR));
		jnode_make_reloc(j, pos_fq(flush_pos));
		junprotect(j);
		jput(j);
	}

	return;
}

static void
mark_jnodes_overwrite(flush_pos_t *flush_pos, oid_t oid, unsigned long index, reiser4_block_nr width)
{
	unsigned long i;
	reiser4_tree *tree;
	jnode *node;

	tree = current_tree;

	for (i = flush_pos->pos_in_unit; i < width; i ++, index ++) {
		node = jlookup(tree, oid, index);
		if (!node) {
			flush_pos->state = POS_INVALID;
#if TRACE_FLUSH
			printk("node not found: (oid %llu, index %lu)\n", oid, index);
#endif
			break;
		}
		if (jnode_check_flushprepped(node)) {
			flush_pos->state = POS_INVALID;
			jput(node);
#if TRACE_FLUSH
			printk("flushprepped: (oid %llu, index %lu)\n", oid, index);
#endif
			break;
		}
		/* FIXME: way to optimize: take atom lock once */
		jnode_make_wander(node);
		jput(node);
	}
}

int
alloc_extent(flush_pos_t *flush_pos)
{
	coord_t *coord;
	reiser4_extent *ext;
	reiser4_extent replace_ext;
	oid_t oid;
	reiser4_block_nr protected;
	reiser4_block_nr start;
	__u64 index;
	__u64 width;
	extent_state state;
	int result;
	txn_atom *atom;
	reiser4_block_nr first_allocated;
	__u64 allocated;
	reiser4_key key;
	block_stage_t block_stage;

	coord = &flush_pos->coord;
	if (item_id_by_coord(coord) == FROZEN_EXTENT_POINTER_ID) {
		/* extent is going to be removed soon */
		flush_pos->state = POS_INVALID;
		return 0;
	}

	item_key_by_coord(coord, &key);
	oid = get_key_objectid(&key);
	ext = extent_by_coord(coord);
	index = extent_unit_index(coord) + flush_pos->pos_in_unit;
	start = extent_unit_start(coord);
	width = extent_unit_width(coord);
	state = state_of_extent(ext);

	if (state == HOLE_EXTENT) {
		flush_pos->state = POS_INVALID;
		return 0;
	}

	assert("vs-1457", width > flush_pos->pos_in_unit);

	if (flush_pos->leaf_relocate || state == UNALLOCATED_EXTENT) {
		/* relocate */
		if (flush_pos->pos_in_unit) {
			/* split extent unit into two */
			result = split_allocated_extent(coord, flush_pos->pos_in_unit);
			flush_pos->pos_in_unit = 0;
			return result;
		}
#if TRACE_FLUSH
		printk("(atom %u) ALLOC: relocate: (oid %llu, index %llu) [%llu %llu] - ", coord->node->zjnode.atom->atom_id,
		       oid, index, start, width);
#endif

		/* Prevent node from e-flushing before allocating disk space for them. Nodes which were eflushed will be
		   read from their temporary locations (but not more than certain limit: JNODES_TO_UNFLUSH) and that
		   disk space will be freed. */
		result = protect_extent_nodes(oid, index, extent_get_width(ext), &protected, ext);
		if (result) {
  			warning("vs-1469", "Failed to protect extent. Should not happen\n");
			return result;
		}
		if (protected == 0) {
#if TRACE_FLUSH
			printk("nothing todo\n");
#endif
			flush_pos->state = POS_INVALID;
			flush_pos->pos_in_unit = 0;
			return 0;
		}
		
		if (state == ALLOCATED_EXTENT) {
			/* all protected nodes are not flushprepped, therefore they are counted as flush_reserved */
			atom = get_current_atom_locked();
 			flush_reserved2grabbed(atom, protected);
			block_stage = BLOCK_GRABBED;
			UNLOCK_ATOM(atom);
		} else
			block_stage = BLOCK_UNALLOCATED;
		
		
		/* allocate new block numbers for protected nodes */
		extent_allocate_blocks(pos_hint(flush_pos), protected, &first_allocated, &allocated, block_stage);
#if TRACE_FLUSH
		printk("allocated: (first %llu, cound %llu) - ", first_allocated, allocated);
#endif
		if (allocated != protected) {
			/* unprotect nodes which will not be allocated/relocated on this iteration */
			if (state == ALLOCATED_EXTENT) {
				atom = get_current_atom_locked();
				grabbed2flush_reserved_nolock(atom, protected - allocated, "");
				UNLOCK_ATOM(atom);
			}
			unprotect_extent_nodes(oid, index + allocated, protected - allocated);
		}
		if (state == ALLOCATED_EXTENT) {
			/* on relocating - free nodes which are being relocated */
			reiser4_dealloc_blocks(&start, &allocated, BLOCK_ALLOCATED, BA_DEFER, "");
		}

		/* assign new block numbers to protected nodes */
		assign_real_blocknrs(oid, index, first_allocated, allocated, flush_pos, state);

		/* prepare extent which will replace current one */
		set_extent(&replace_ext, first_allocated, allocated);

		/* adjust extent item */
		result = conv_extent(coord, &replace_ext);
		if (result) {
  			warning("vs-1461", "Failed to allocate extent. Should not happen\n");
			return result;
		}
		node_check(coord->node, 0);
	} else {
#if TRACE_FLUSH
		printk("(atom %u) ALLOC: overwrite: (oid %llu, index %llu) [%llu %llu]\n", coord->node->zjnode.atom->atom_id,
		       oid, index, start, width);
#endif
		/* overwrite */
		mark_jnodes_overwrite(flush_pos, oid, index, width);
	}
	flush_pos->pos_in_unit = 0;
	return 0;
}

/* copy extent @copy to the end of @node. It may have to either insert new item after the last one, or append last item,
   or modify last unit of last item to have greater width */
static int
put_unit_to_end(znode *node, const reiser4_key *key, reiser4_extent *copy_ext)
{
	int result;
	coord_t coord;
	cop_insert_flag flags;
	reiser4_extent *last_ext;
	reiser4_item_data data;

	/* set coord after last unit in an item */
	coord_init_last_unit(&coord, node);
	coord.between = AFTER_UNIT;

	flags = COPI_DONT_SHIFT_LEFT | COPI_DONT_SHIFT_RIGHT | COPI_DONT_ALLOCATE;
	if (must_insert(&coord, key)) {
		result = insert_by_coord(&coord, init_new_extent(&data, copy_ext, 1), key, 0 /*lh */ , 0 /*ra */ ,
					 0 /*ira */ , flags);

	} else {
		/* try to glue with last unit */
		last_ext = extent_by_coord(&coord);
		if (state_of_extent(last_ext) &&
		    extent_get_start(last_ext) + extent_get_width(last_ext) == extent_get_start(copy_ext)) {
			extent_set_width(last_ext, extent_get_width(last_ext) + extent_get_width(copy_ext));
			znode_make_dirty(node);
			return 0;
		}

		/* FIXME: put an assertion here that we can not merge last unit in @node and new unit */
		result = insert_into_item(&coord, 0 /*lh */ , key, init_new_extent(&data, copy_ext, 1), flags);
	}

	assert("vs-438", result == 0 || result == -E_NODE_FULL);
	return result;
}

/* @coord is set to extent unit */
squeeze_result
squalloc_extent(znode *left, const coord_t *coord, flush_pos_t *flush_pos, reiser4_key *stop_key)
{
	reiser4_extent *ext;
	__u64 index;
	__u64 width;
	reiser4_block_nr start;
	extent_state state;
	oid_t oid;
	txn_atom *atom;
	reiser4_block_nr first_allocated;
	__u64 allocated;
	__u64 protected;
	reiser4_extent copy_extent;
	reiser4_key key;
	int result;
	block_stage_t block_stage;

	assert("vs-1457", flush_pos->pos_in_unit == 0);
	assert("vs-1467", coord_is_leftmost_unit(coord));
	assert("vs-1467", item_is_extent(coord));

	if (item_id_by_coord(coord) == FROZEN_EXTENT_POINTER_ID) {
		/* extent is going to be removed soon */
		return SQUEEZE_TARGET_FULL;
	}

	ext = extent_by_coord(coord);
	index = extent_unit_index(coord);
	start = extent_unit_start(coord);
	width = extent_unit_width(coord);
	state = state_of_extent(ext);
	unit_key_by_coord(coord, &key);
	oid = get_key_objectid(&key);

	if (flush_pos->leaf_relocate || state == UNALLOCATED_EXTENT) {

#if TRACE_FLUSH
		printk("SQUALLOC: relocate: (oid %llu, index %llu) [%llu %llu] - ", oid, index, start, width);
#endif

		/* relocate */
		result = protect_extent_nodes(oid, index, extent_get_width(ext), &protected, ext);
		if (result) {
  			warning("vs-1469", "Failed to protect extent. Should not happen\n");
			return result;
		}
		if (protected == 0) {
			flush_pos->state = POS_INVALID;
			return 0;
		}

		if (state == ALLOCATED_EXTENT) {
			/* all protected nodes are not flushprepped, therefore they are counted as flush_reserved */
			atom = get_current_atom_locked();
 			flush_reserved2grabbed(atom, protected);
			block_stage = BLOCK_GRABBED;
			UNLOCK_ATOM(atom);
		} else
			block_stage = BLOCK_UNALLOCATED;

		/* allocate new block numbers for protected nodes */
		extent_allocate_blocks(pos_hint(flush_pos), protected, &first_allocated, &allocated, block_stage);
#if TRACE_FLUSH
		printk("allocated: (first %llu, cound %llu) - ", first_allocated, allocated);
#endif
		if (allocated != protected) {
			if (state == ALLOCATED_EXTENT) {
				atom = get_current_atom_locked();
				grabbed2flush_reserved_nolock(atom, protected - allocated, "");
				UNLOCK_ATOM(atom);
			}
			unprotect_extent_nodes(oid, index + allocated, protected - allocated);
		}

		/* prepare extent which will be copied to left */
		set_extent(&copy_extent, first_allocated, allocated);

		result = put_unit_to_end(left, &key, &copy_extent);
		if (result == -E_NODE_FULL) {
#if TRACE_FLUSH
			printk("left is full, free (first %llu, count %llu)\n", first_allocated, allocated);
#endif
			if (state == ALLOCATED_EXTENT)
				reiser4_dealloc_blocks(&first_allocated, &allocated, BLOCK_FLUSH_RESERVED, 0, "");
			else
				reiser4_dealloc_blocks(&first_allocated, &allocated, BLOCK_UNALLOCATED, 0, "");
			unprotect_extent_nodes(oid, index, allocated);
			return SQUEEZE_TARGET_FULL;
		}

		/* free nodes which were relocated */
		if (state == ALLOCATED_EXTENT) {
			reiser4_dealloc_blocks(&start, &allocated, BLOCK_ALLOCATED, BA_DEFER, "");
		}

		/* assign new block numbers to protected nodes */
		assign_real_blocknrs(oid, index, first_allocated, allocated, flush_pos, state);
		set_key_offset(&key, get_key_offset(&key) + (allocated << current_blocksize_bits));
#if TRACE_FLUSH
		printk("copied to left: [%llu %llu]\n", first_allocated, allocated);
#endif
		node_check(left, 0);
	} else {
#if TRACE_FLUSH
		printk("SQUALLOC: overwrite: (oid %llu, index %llu) [%llu %llu] - ", oid, index, start, width);
#endif

		/* overwrite: try to copy unit as it is to left neighbor and make all first not flushprepped nodes
		   overwrite nodes */
		set_extent(&copy_extent, start, width);
		result = put_unit_to_end(left, &key, &copy_extent);
		if (result == -E_NODE_FULL) {
#if TRACE_FLUSH
			printk("left is full\n");
#endif
			return SQUEEZE_TARGET_FULL;
		}
		mark_jnodes_overwrite(flush_pos, oid, index, width);
		set_key_offset(&key, get_key_offset(&key) + (width << current_blocksize_bits));
#if TRACE_FLUSH
		printk("copied to left\n");
#endif
	}
	*stop_key = key;
	return SQUEEZE_CONTINUE;
}
#endif /* NEW_FLUSH */

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
