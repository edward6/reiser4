/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../key.h"
#include "../../super.h"
#include "../../carry.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../../emergency_flush.h"
#include "../../prof.h"
#include "../../flush.h"
#include "../object.h"

#include <linux/quotaops.h>
#include <asm/uaccess.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>

static const reiser4_block_nr zero = 0;
static const reiser4_block_nr one = 1;



/* prepare structure reiser4_item_data. It is used to put one extent unit into tree */
/* Audited by: green(2002.06.13) */
reiser4_item_data *
init_new_extent(reiser4_item_data *data, void *ext_unit, int nr_extents)
{
	if (REISER4_ZERO_NEW_NODE)
		memset(data, 0, sizeof(reiser4_item_data));

	data->data = ext_unit;
	/* data->data is kernel space */
	data->user = 0;
	data->length = sizeof(reiser4_extent) * nr_extents;
	data->arg = 0;
	data->iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	return data;
}

/* how many bytes are addressed by @nr first extents of the extent item */
reiser4_block_nr
extent_size(const coord_t *coord, pos_in_item_t nr)
{
	pos_in_item_t i;
	reiser4_block_nr blocks;
	reiser4_extent *ext;

	ext = item_body_by_coord(coord);
	assert("vs-263", nr <= nr_units_extent(coord));

	blocks = 0;
	for (i = 0; i < nr; i++, ext++) {
		blocks += extent_get_width(ext);
	}

	return blocks * current_blocksize;
}

extent_state
state_of_extent(reiser4_extent *ext)
{
	switch ((int) extent_get_start(ext)) {
	case 0:
		return HOLE_EXTENT;
	case 1:
		return UNALLOCATED_EXTENT;
	case 2:
		return UNALLOCATED_EXTENT2;
	default:
		break;
	}
	return ALLOCATED_EXTENT;
}

int
extent_is_unallocated(const coord_t *item)
{
	assert("jmacd-5133", item_is_extent(item));

	return state_of_extent(extent_by_coord(item)) == UNALLOCATED_EXTENT;
}

int
extent_is_allocated(const coord_t *item)
{
	assert("jmacd-5133", item_is_extent(item));

	return state_of_extent(extent_by_coord(item)) == ALLOCATED_EXTENT;
}

/* set extent's start and width */
void
set_extent(reiser4_extent *ext, reiser4_block_nr start, reiser4_block_nr width)
{
	extent_set_start(ext, start);
	extent_set_width(ext, width);
}

/* used in split_allocate_extent, allocated2unallocated, extent_handle_relocate_in_place, plug_hole to insert 1 or 2
   extent units after the one @un_extent is set to. @un_extent itself is changed to @new_ext */
int

#if REMOTE

/* plugin->u.item.b.paste
   item @coord is set to has been appended with @data->length of free
   space. data->data contains data to be pasted into the item in position
   @coord->in_item.unit_pos. It must fit into that free space.
   @coord must be set between units.
*/
int
paste_extent(coord_t *coord, reiser4_item_data *data, carry_plugin_info *info UNUSED_ARG)
{
	unsigned old_nr_units;
	reiser4_extent *ext;
	int item_length;

	ext = extent_item(coord);
	item_length = item_length_by_coord(coord);
	old_nr_units = (item_length - data->length) / sizeof (reiser4_extent);

	/* this is also used to copy extent into newly created item, so
	   old_nr_units could be 0 */
	assert("vs-260", item_length >= data->length);

	/* make sure that coord is set properly */
	assert("vs-35", ((!coord_is_existing_unit(coord)) || (!old_nr_units && !coord->unit_pos)));

	/* first unit to be moved */
	switch (coord->between) {
	case AFTER_UNIT:
		coord->unit_pos++;
	case BEFORE_UNIT:
		coord->between = AT_UNIT;
		break;
	case AT_UNIT:
		assert("vs-331", !old_nr_units && !coord->unit_pos);
		break;
	default:
		impossible("vs-330", "coord is set improperly");
	}

	/* prepare space for new units */
	xmemmove(ext + coord->unit_pos + data->length / sizeof (reiser4_extent),
		 ext + coord->unit_pos, (old_nr_units - coord->unit_pos) * sizeof (reiser4_extent));

	/* copy new data from kernel space */
	assert("vs-556", data->user == 0);
	xmemcpy(ext + coord->unit_pos, data->data, (unsigned) data->length);

	/* after paste @coord is set to first of pasted units */
	assert("vs-332", coord_is_existing_unit(coord));
	assert("vs-333", !memcmp(data->data, extent_by_coord(coord), (unsigned) data->length));
	return 0;
}

/* plugin->u.item.b.can_shift */
int
can_shift_extent(unsigned free_space, coord_t *source,
		 znode *target UNUSED_ARG, shift_direction pend UNUSED_ARG, unsigned *size, unsigned want)
{
	*size = item_length_by_coord(source);
	if (*size > free_space)
		/* never split a unit of extent item */
		*size = free_space - free_space % sizeof (reiser4_extent);

	/* we can shift *size bytes, calculate how many do we want to shift */
	if (*size > want * sizeof (reiser4_extent))
		*size = want * sizeof (reiser4_extent);

	if (*size % sizeof (reiser4_extent) != 0)
		impossible("vs-119", "Wrong extent size: %i %i", *size, sizeof (reiser4_extent));
	return *size / sizeof (reiser4_extent);

}

/* plugin->u.item.b.copy_units */
void
copy_units_extent(coord_t *target, coord_t *source,
		  unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space)
{
	char *from_ext, *to_ext;

	assert("vs-217", free_space == count * sizeof (reiser4_extent));

	from_ext = item_body_by_coord(source);
	to_ext = item_body_by_coord(target);

	if (where_is_free_space == SHIFT_LEFT) {
		assert("vs-215", from == 0);

		/* At this moment, item length was already updated in the item
		   header by shifting code, hence nr_units_extent() will
		   return "new" number of units---one we obtain after copying
		   units.
		*/
		to_ext += (nr_units_extent(target) - count) * sizeof (reiser4_extent);
	} else {
		reiser4_key key;
		coord_t coord;

		assert("vs-216", from + count == coord_last_unit_pos(source) + 1);

		from_ext += item_length_by_coord(source) - free_space;

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		coord = *source;
		coord.unit_pos = from;
		unit_key_extent(&coord, &key);

		node_plugin_by_node(target->node)->update_item_key(target, &key, 0/*info */);
	}

	xmemcpy(to_ext, from_ext, free_space);
}

/* plugin->u.item.b.create_hook
   @arg is znode of leaf node for which we need to update right delimiting key */
int
create_hook_extent(const coord_t *coord, void *arg)
{
	coord_t *child_coord;
	znode *node;
	reiser4_key key;
	reiser4_tree *tree;

	if (!arg)
		return 0;

	child_coord = arg;
	tree = znode_get_tree(coord->node);

	assert("nikita-3246", znode_get_level(child_coord->node) == LEAF_LEVEL);

	WLOCK_DK(tree);
	WLOCK_TREE(tree);
	/* find a node on the left level for which right delimiting key has to
	   be updated */
	if (coord_wrt(child_coord) == COORD_ON_THE_LEFT) {
		assert("vs-411", znode_is_left_connected(child_coord->node));
		node = child_coord->node->left;
	} else {
		assert("vs-412", coord_wrt(child_coord) == COORD_ON_THE_RIGHT);
		node = child_coord->node;
		assert("nikita-3314", node != NULL);
	}

	if (node != NULL) {
		znode_set_rd_key(node, item_key_by_coord(coord, &key));

		assert("nikita-3282", check_sibling_list(node));
		/* break sibling links */
		if (ZF_ISSET(node, JNODE_RIGHT_CONNECTED) && node->right) {
			node->right->left = NULL;
			node->right = NULL;
		}
	}
	WUNLOCK_TREE(tree);
	WUNLOCK_DK(tree);
	return 0;
}

/* check inode's list of eflushed jnodes and drop those which correspond to this extent */
static void
drop_eflushed_nodes(struct inode *inode, unsigned long index, unsigned long end)
{
#if REISER4_USE_EFLUSH
	struct list_head *tmp, *next;
	reiser4_inode *info;
	reiser4_tree *tree;
	int nr;

	if (!inode)
		/* there should be no eflushed jnodes */
		return;

	nr = 0;
	tree = tree_by_inode(inode);
 repeat:
	
	spin_lock_eflush(tree->super);

	info = reiser4_inode_data(inode);
	list_for_each_safe(tmp, next, &info->eflushed_jnodes) {
		eflush_node_t *ef;
		jnode *j;

		ef = list_entry(tmp, eflush_node_t, inode_link);
		j = ef->node;
		if (index_jnode(j) >= index && end && index_jnode(j) < end) {
			jref(j);			
			spin_unlock_eflush(tree->super);
			UNDER_SPIN_VOID(jnode, j, eflush_del(j, 0));
			uncapture_jnode(j);
			jput(j);
			nr ++;
			goto repeat;
		}
	}
	spin_unlock_eflush(tree->super);
#endif
}

/* plugin->u.item.b.kill_hook
   this is called when @count units starting from @from-th one are going to be removed */
int
kill_hook_extent(const coord_t *coord, unsigned from, unsigned count, struct cut_list *p)
{
	reiser4_extent *ext;
	unsigned i;
	reiser4_block_nr start, length;
	reiser4_key key;
	loff_t offset;
	unsigned long index;
	struct inode *inode;
	reiser4_tree *tree;
	znode *left;
	znode *right;

	assert("zam-811", znode_is_write_locked(coord->node));
	assert("nikita-3315", p != NULL);

	item_key_by_coord(coord, &key);
	offset = get_key_offset(&key) + extent_size(coord, from);
	index = offset >> current_blocksize_bits;

	inode = p->inode;

	tree = current_tree;

	if (p->left != NULL) {
		assert("nikita-3316", p->right != NULL);

		left = p->left->node;
		right = p->right->node;

		UNDER_RW_VOID(tree, tree, write, 
			      link_left_and_right(left, right));

		if (right != NULL)
			UNDER_RW_VOID(dk, tree, write, 
				      update_znode_dkeys(left, right));
	}
	if (inode != NULL) {
		truncate_inode_pages(inode->i_mapping, offset);
		drop_eflushed_nodes(inode, index, 0);
	}

	ext = extent_item(coord) + from;

	for (i = 0; i < count; i++, ext++, index += length) {

		start = extent_get_start(ext);
		length = extent_get_width(ext);

		if (state_of_extent(ext) == HOLE_EXTENT)
			continue;

		if (state_of_extent(ext) == UNALLOCATED_EXTENT) {
			/* some jnodes corresponding to this unallocated extent */

			/* FIXME-VITALY: this is necessary??? */
			fake_allocated2free(extent_get_width(ext), 0 /* unformatted */, "extent_kill_item_hook: unallocated extent removed");
			continue;
		}

		assert("vs-1218", state_of_extent(ext) == ALLOCATED_EXTENT);

		/* FIXME-VS: do I need to do anything for unallocated extents */
		/* BA_DEFER bit parameter is turned on because blocks which get freed
		   are not safe to be freed immediately */
		
		reiser4_dealloc_blocks(&start, &length, 0 /* not used */, 
			BA_DEFER/* unformatted with defer */, "extent_kill_item_hook");
	}
	return 0;
}

static int
cut_or_kill_units(coord_t *coord,
		  unsigned *from, unsigned *to,
		  int cut, const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed, struct cut_list *cl)
{
	reiser4_extent *ext;
	reiser4_key key;
	unsigned blocksize, blocksize_bits;
	reiser4_block_nr offset;
	unsigned count;
	__u64 cut_from_to;
	struct inode *inode;

	inode = cl->inode;
	count = *to - *from + 1;

	blocksize = current_blocksize;
	blocksize_bits = current_blocksize_bits;

	/* make sure that we cut something but not more than all units */
	assert("vs-220", count > 0 && count <= nr_units_extent(coord));
	/* extent item can be cut either from the beginning or down to the end */
	assert("vs-298", *from == 0 || *to == coord_last_unit_pos(coord));

	item_key_by_coord(coord, &key);
	offset = get_key_offset(&key);

	if (smallest_removed) {
		/* set @smallest_removed assuming that @from unit will be
		   cut */
		*smallest_removed = key;
		set_key_offset(smallest_removed, (offset + extent_size(coord, *from)));
	}

	cut_from_to = 0;

	/* it may happen that extent @from will not be removed */
	if (from_key) {
		reiser4_key key_inside;
		__u64 last;

		if (!cut && inode != NULL) {
			loff_t start;
			loff_t end;
			long nr_pages;
			unsigned long index;

			/* round @start upward */
			start = round_up(get_key_offset(from_key), 
					 PAGE_CACHE_SIZE);
			/* convert to page index */
			start >>= PAGE_CACHE_SHIFT;
			/* index of last page which is to be truncated */
			end   = get_key_offset(to_key) >> PAGE_CACHE_SHIFT;
			/* number of completely removed pages */
			nr_pages = end - start + 1;
			truncate_mapping_pages_range(inode->i_mapping, 
						     start, nr_pages);
			index = start >> PAGE_CACHE_SHIFT;
			drop_eflushed_nodes(inode, start, start + nr_pages);
		}

		/* when @from_key (and @to_key) are specified things become
		   more complex. It may happen that @from-th or @to-th extent
		   will only decrease their width */
		assert("vs-311", to_key);

		key_inside = key;
		set_key_offset(&key_inside, (offset + extent_size(coord, *from)));
		last = offset + extent_size(coord, *to + 1) - 1;
		if (keygt(from_key, &key_inside)) {
			/* @from-th extent can not be removed. Its width has to
			   be decreased in accordance with @from_key */
			reiser4_block_nr new_width, old_width;
			reiser4_block_nr first;

			/* cut from the middle of extent item is not allowed,
			   make sure that the rest of item gets cut
			   completely */
#if REISER4_DEBUG
			{
				reiser4_key tmp;
				assert("vs-612", *to == coord_last_unit_pos(coord));
				append_key_extent(coord, &tmp);
				set_key_offset(&tmp, get_key_offset(&tmp) - 1);
				assert("vs-613", keyge(to_key, &tmp));
			}
#endif
			ext = extent_item(coord) + *from;
			first = offset + extent_size(coord, *from);
			old_width = extent_get_width(ext);
			new_width = (get_key_offset(from_key) + (blocksize - 1) - first) >> blocksize_bits;
			assert("vs-307", new_width > 0 && new_width <= old_width);
			if (new_width < old_width) {
				/* FIXME-VS: debugging zam-528 */
				if (state_of_extent(ext) == UNALLOCATED_EXTENT && !cut) {
					/* FIXME-VITALY: this is necessary??? */
					fake_allocated2free(old_width - new_width, 0 /* unformatted */,
							    "cut_or_kill_units: unallocated extent shortened fron its end");
				}

				if (state_of_extent(ext) == ALLOCATED_EXTENT && !cut) {
					reiser4_block_nr start, length;
					/* truncate is in progress. Some blocks
					   can be freed. As they do not get
					   immediately available, set defer
					   parameter of reiser4_dealloc_blocks
					   to 1
					*/
					start = extent_get_start(ext) + new_width;
					length = old_width - new_width;

					reiser4_dealloc_blocks(&start, &length, 0 /* not used */, 
						BA_DEFER /* unformatted with defer */, "cut_or_kill_units: from");
				}
				extent_set_width(ext, new_width);
				znode_make_dirty(coord->node);
			}
			(*from)++;
			count--;
			if (smallest_removed) {
				set_key_offset(smallest_removed, get_key_offset(from_key));
			}
		}

		/* set @key_inside to key of last byte addressed to extent @to */
		set_key_offset(&key_inside, last);

		if (keylt(to_key, &key_inside)) {
			/* @to-th unit can not be removed completely */

			reiser4_block_nr new_width, old_width;

			/* cut from the middle of extent item is not allowed,
			   make sure that head of item gets cut and cut is
			   aligned to block boundary */
			assert("vs-614", *from == 0);
			assert("vs-615", keyle(from_key, &key));
			assert("vs-616", ((get_key_offset(to_key) + 1) & (blocksize - 1)) == 0);

			ext = extent_item(coord) + *to;

			new_width = (get_key_offset(&key_inside) - get_key_offset(to_key)) >> blocksize_bits;

			old_width = extent_get_width(ext);
			cut_from_to = (old_width - new_width) * blocksize;

			assert("vs-617", new_width > 0 && new_width <= old_width);

			/* FIXME-VS: debugging zam-528 */
			if (state_of_extent(ext) == UNALLOCATED_EXTENT && !cut) {
				/* FIXME-VITALY: this is necessary??? */
				fake_allocated2free(old_width - new_width, 0 /* unformatted */,
						    "cut_or_kill_units: unallocated extent shortened from its head");
			}

			if (state_of_extent(ext) == ALLOCATED_EXTENT && !cut) {
				reiser4_block_nr start, length;
				/* extent2tail is in progress. Some blocks can
				   be freed. As they do not get immediately
				   available, set defer parameter of
				   reiser4_dealloc_blocks to 1
				*/
				start = extent_get_start(ext);
				length = old_width - new_width;
				reiser4_dealloc_blocks(&start, &length, 0 /* not used */,
					BA_DEFER/* unformatted with defer */, "cut_or_kill_units: to");
			}

			/* (old_width - new_width) blocks of this extent were
			   free, update both extent's start (for allocated
			   extent only) and width */
			if (state_of_extent(ext) == ALLOCATED_EXTENT) {
				extent_set_start(ext, extent_get_start(ext) + old_width - new_width);
			}
			extent_set_width(ext, new_width);
			znode_make_dirty(coord->node);
			(*to)--;
			count--;
		}
	}

	if (!cut) {
		cl->inode = NULL;
		/* call kill hook for all extents removed completely */
		kill_hook_extent(coord, *from, count, cl);
		cl->inode = inode;
	}

	if (*from == 0 && count != coord_last_unit_pos(coord) + 1) {
		/* part of item is removed from item beginning, update item key
		   therefore */
		item_key_by_coord(coord, &key);
		set_key_offset(&key, (get_key_offset(&key) + extent_size(coord, count) + cut_from_to));
		node_plugin_by_node(coord->node)->update_item_key(coord, &key, 0);
	}

	if (REISER4_DEBUG) {
		/* zero space which is freed as result of cut between keys */
		ext = extent_item(coord);
		xmemset(ext + *from, 0, count * sizeof (reiser4_extent));
	}

	return count * sizeof (reiser4_extent);
}

/* plugin->u.item.b.cut_units */
int
cut_units_extent(coord_t *item, unsigned *from, unsigned *to,
		 const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed, struct cut_list *p)
{
	return cut_or_kill_units(item, from, to, 1, from_key, to_key, smallest_removed, p);
}

/* plugin->u.item.b.kill_units */
int
kill_units_extent(coord_t *item, unsigned *from, unsigned *to,
		  const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed, struct cut_list *p)
{
	return cut_or_kill_units(item, from, to, 0, from_key, to_key, smallest_removed, p);
}

/* plugin->u.item.b.unit_key */
reiser4_key *
unit_key_extent(const coord_t *coord, reiser4_key *key)
{
	assert("vs-300", coord_is_existing_unit(coord));

	item_key_by_coord(coord, key);
	set_key_offset(key, (get_key_offset(key) + extent_size(coord, (unsigned) coord->unit_pos)));

	return key;
}

/* Return the reiser_extent and position within that extent. */
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

		tree = current_tree;
		*childp = jlook_lock(tree, get_key_objectid(&key), index);
	}

	return 0;
}

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
	}

	return 0;
}

/* plugin->u.item.b.item_stat */
void
item_stat_extent(const coord_t *coord, void *vp)
{
	reiser4_extent *ext;
	struct extent_stat *ex_stat;
	unsigned i, nr_units;

	ex_stat = (struct extent_stat *) vp;

	ext = extent_item(coord);
	nr_units = nr_units_extent(coord);

	for (i = 0; i < nr_units; i++) {
		switch (state_of_extent(ext + i)) {
		case ALLOCATED_EXTENT:
			ex_stat->allocated_units++;
			ex_stat->allocated_blocks += extent_get_width(ext + i);
			break;
		case UNALLOCATED_EXTENT:
			ex_stat->unallocated_units++;
			ex_stat->unallocated_blocks += extent_get_width(ext + i);
			break;
		case HOLE_EXTENT:
			ex_stat->hole_units++;
			ex_stat->hole_blocks += extent_get_width(ext + i);
			break;
		}
	}
}

#if REISER4_DEBUG

/* plugin->u.item.b.check
   used for debugging, every item should have here the most complete
   possible check of the consistency of the item that the inventor can
   construct 
*/
int
check_extent(const coord_t *coord /* coord of item to check */ ,
	     const char **error /* where to store error message */ )
{
	reiser4_extent *ext, *first;
	unsigned i, j;
	reiser4_block_nr start, width, blk_cnt;
	unsigned num_units;
	reiser4_tree *tree;
	oid_t oid;
	reiser4_key key;
	coord_t scan;

	assert("vs-933", REISER4_DEBUG);

	if (znode_get_level(coord->node) != TWIG_LEVEL) {
		*error = "Extent on the wrong level";
		return -1;
	}
	if (item_length_by_coord(coord) % sizeof (reiser4_extent) != 0) {
		*error = "Wrong item size";
		return -1;
	}
	ext = first = extent_item(coord);
	blk_cnt = reiser4_block_count(reiser4_get_current_sb());
	num_units = coord_num_units(coord);
	tree = znode_get_tree(coord->node);
	item_key_by_coord(coord, &key);
	oid = get_key_objectid(&key);
	coord_dup(&scan, coord);

	for (i = 0; i < num_units; ++i, ++ext) {
		__u64 index;

		scan.unit_pos = i;
		index = extent_unit_index(&scan);

		/* check that all jnodes are present for the unallocated
		 * extent */
		if (state_of_extent(ext) == UNALLOCATED_EXTENT) {
			for (j = 0; j < extent_get_width(ext); j ++) {
				jnode *node;

				RLOCK_TREE(tree);
				node = jlook_lock(tree, oid, index + j);
				if (node == NULL) {
					BUG();
					print_coord("scan", &scan, 0);
					*error = "Jnode missing";
					RUNLOCK_TREE(tree);
					return -1;
				}
				RUNLOCK_TREE(tree);
				jput(node);
			}
		}

		start = extent_get_start(ext);
		if (start < 2)
			continue;
		/* extent is allocated one */
		width = extent_get_width(ext);
		if (start >= blk_cnt) {
			*error = "Start too large";
			return -1;
		}
		if (start + width > blk_cnt) {
			*error = "End too large";
			return -1;
		}
		/* make sure that this extent does not overlap with other
		   allocated extents extents */
		for (j = 0; j < i; j++) {
			if (state_of_extent(first + j) != ALLOCATED_EXTENT)
				continue;
			if (!((extent_get_start(ext) >= extent_get_start(first + j) + extent_get_width(first + j))
			      || (extent_get_start(ext) + extent_get_width(ext) <= extent_get_start(first + j)))) {
				*error = "Extent overlaps with others";
				return -1;
			}
		}
	}
	return 0;
}

#endif /* REISER4_DEBUG */

/* @coord is set either to the end of last extent item of a file
   (coord->node is a node on the twig level) or to a place where first
   item of file has to be inserted to (coord->node is leaf
   node). Calculate size of hole to be inserted. If that hole is too
   big - only part of it is inserted */
static int
add_hole(coord_t *coord, lock_handle *lh, const reiser4_key *key /* key of position in a file for write */)
{
	int result;
	znode *loaded;
	reiser4_extent *ext, new_ext;
	reiser4_block_nr hole_width;
	reiser4_item_data item;
	reiser4_key hole_key;

	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (result)
		return result;
	loaded = coord->node;

	if (znode_get_level(coord->node) == LEAF_LEVEL) {
		/* there are no items of this file yet. First item will be
		   hole extent inserted here */

		/* @coord must be set for inserting of new item */
		assert("vs-711", coord_is_between_items(coord));

		hole_key = *key;
		set_key_offset(&hole_key, 0ull);

		hole_width = ((get_key_offset(key) + current_blocksize - 1) >> current_blocksize_bits);
		assert("vs-710", hole_width > 0);

		/* compose body of hole extent */
		set_extent(&new_ext, HOLE_EXTENT_START, hole_width);

		result = insert_extent_by_coord(coord, init_new_extent(&item, &new_ext, 1), &hole_key, lh);
		zrelse(loaded);
		return result;
	}

	/* last item of file may have to be appended with hole */
	assert("vs-708", znode_get_level(coord->node) == TWIG_LEVEL);
	assert("vs-714", item_id_by_coord(coord) == EXTENT_POINTER_ID);

	/* make sure we are at proper item */
	assert("vs-918", keylt(key, max_key_inside_extent(coord, &hole_key)));

	/* key of first byte which is not addressed by this extent */
	append_key_extent(coord, &hole_key);

	if (keyle(key, &hole_key)) {
		/* there is already extent unit which contains position
		   specified by @key */
		zrelse(loaded);
		return 0;
	}

	/* extent item has to be appended with hole. Calculate length of that
	   hole */
	hole_width = ((get_key_offset(key) - get_key_offset(&hole_key) +
		       current_blocksize - 1) >> current_blocksize_bits);
	assert("vs-954", hole_width > 0);

	/* set coord after last unit */
	coord_init_after_item_end(coord);

	/* get last extent in the item */
	ext = extent_by_coord(coord);
	if (state_of_extent(ext) == HOLE_EXTENT) {
		/* last extent of a file is hole extent. Widen that extent by
		   @hole_width blocks. Note that we do not worry about
		   overflowing - extent width is 64 bits */
		set_extent(ext, HOLE_EXTENT_START, extent_get_width(ext) + hole_width);
		znode_make_dirty(coord->node);
		zrelse(loaded);
		return 0;
	}

	/* append item with hole extent unit */
	assert("vs-713", (state_of_extent(ext) == ALLOCATED_EXTENT || state_of_extent(ext) == UNALLOCATED_EXTENT));

	/* compose body of hole extent */
	set_extent(&new_ext, HOLE_EXTENT_START, hole_width);

	result = insert_into_item(coord, lh, &hole_key, init_new_extent(&item, &new_ext, 1), 0 /*flags */ );
	zrelse(loaded);
	return result;
}

/* used in allocate_extent_item_in_place and plug_hole to replace @un_extent
   with either two or three extents

   (@un_extent) with allocated (@star, @alloc_width) and unallocated
   (@unalloc_width). Have insert_into_item to not try to shift anything to
   left.
*/
static int

#endif REMOTE

replace_extent(coord_t *un_extent, lock_handle *lh,
	       reiser4_key *key, reiser4_item_data *data, const reiser4_extent *new_ext, unsigned flags)
{
	int result;
	coord_t coord_after;
	lock_handle lh_after;
	tap_t watch;
	znode *orig_znode;
	ON_DEBUG(reiser4_extent orig_ext);	/* this is for debugging */

	assert("vs-990", coord_is_existing_unit(un_extent));
	assert("vs-1375", znode_is_write_locked(un_extent->node));
	assert("vs-1426", extent_get_width(new_ext) != 0);
	assert("vs-1427", extent_get_width((reiser4_extent *)data->data) != 0);

	coord_dup(&coord_after, un_extent);
	init_lh(&lh_after);
	copy_lh(&lh_after, lh);
	tap_init(&watch, &coord_after, &lh_after, ZNODE_WRITE_LOCK);
	tap_monitor(&watch);

	ON_DEBUG(orig_ext = *extent_by_coord(un_extent));
	orig_znode = un_extent->node;

	/* make sure that key is set properly */
	if (REISER4_DEBUG) {
		reiser4_key tmp;

		unit_key_by_coord(un_extent, &tmp);
		set_key_offset(&tmp, get_key_offset(&tmp) + extent_get_width(new_ext) * current_blocksize);
		assert("vs-1080", keyeq(&tmp, key));
	}

	DISABLE_NODE_CHECK;

	/* set insert point after unit to be replaced */
	un_extent->between = AFTER_UNIT;
	result = insert_into_item(un_extent, (flags == COPI_DONT_SHIFT_LEFT) ? 0 : lh, key, data, flags);
	if (!result) {
		reiser4_extent *ext;

		if (coord_after.node != orig_znode) {
			coord_clear_iplug(&coord_after);
			result = zload(coord_after.node);
		}

		if (likely(!result)) {
			ext = extent_by_coord(&coord_after);

			assert("vs-987", znode_is_loaded(coord_after.node));
			assert("vs-988", !memcmp(ext, &orig_ext, sizeof (*ext)));

			*ext = *new_ext;
			znode_make_dirty(coord_after.node);

			if (coord_after.node != orig_znode)
				zrelse(coord_after.node);
			if (flags == COPI_DONT_SHIFT_LEFT) {
				/* set coord back to initial extent unit */
				*un_extent = coord_after;
				assert("vs-1375", znode_is_write_locked(un_extent->node));
			}
		}
	}
	tap_done(&watch);
	
	ENABLE_NODE_CHECK;
	return result;
}

lock_handle *
znode_lh(znode *node, znode_lock_mode mode)
{
	if (mode == ZNODE_WRITE_LOCK) {
		assert("vs-1371", znode_is_write_locked(node));
		assert("vs-1372", znode_is_wlocked_once(node));
	} else
		assert("vs-1371", znode_is_rlocked(node));
		
	return owners_list_front(&node->lock.owners);
}

static int get_reiser4_inode_by_tap (struct inode ** result, tap_t * tap)
{
	/* We cannot read unformatted data if corresponding inode is
	 * unknown or not in cache. Here we build stat data key and
	 * search in the inode cache. */

	/* FIXME(Zam). We have a problem because a reiser4 inode
	 * initialization cannot be completed.  Some inode properties
	 * are inherited from a parent directory, unlike
	 * reiser4_lookup() a parent directory is unknown here.  

	 * The reiser4 file inheritance may be fixed by implementing a
	 * copying of parent directory properties at the moment of new
	 * file creation. But, still we do not know what to do with
	 * light-weight files which have no stat data and all their
	 * properties are inherited from parent directory. 

	 * I think this problem is from bad layering of reiser4 tree
	 * access. The repacker is for dealing with reiser4 tree, not
	 * with files, but it has to take care about reading reiser4
	 * inodes and proper initialization of their fields.  A layer of
	 * accessing only tree nodes is just missing in reiser4.

	 * The solution could be in to allow read of unformatted data
	 * without having fully initialized inode. Actually no
	 * reiser4-specific fields are needed for accessing of
	 * unformatted data and repacking them.  The repacker may use
	 * not-initialized inode for its needs until somebody else
	 * completes inode initialization in reiser4_lookup().

	 * Another problem is that we cannot construct SD key from a key
	 * of arbitrary file item.  The key transformation which is done
	 * here (type := KEY_SD_MINOR, offset := 0). It is OK for
	 * current key assignment scheme but it is not valid in general
	 * case.  Using of not-initialized reiser4 inodes can solve this
	 * problem too. */

	reiser4_key sd_key;
	struct super_block * super = reiser4_get_current_sb();
	const coord_t * coord = tap->coord;
	struct inode * inode;

	unit_key_by_coord(coord, &sd_key);
	set_key_type(&sd_key, KEY_SD_MINOR);
	set_key_offset(&sd_key, (__u64) 0);

	inode = ilookup5(super, (unsigned long)get_key_objectid(&sd_key),
			 reiser4_inode_find_actor, &sd_key);
	if (inode == NULL) {
		/* Inode was not found in cache. We have to release all
		 * locks before CBK. */
		tap_done(tap);
		inode = reiser4_iget(super, &sd_key);
		if (IS_ERR(inode))
			return PTR_ERR(inode);
		if (inode->i_state & I_NEW)
			unlock_new_inode(inode);
		iput(inode);
		/* Expect the inode to be in cache when we come here
		 * next time. */
		return -E_REPEAT;
	}

	if (is_bad_inode(inode)) {
		iput(inode);
		return -EIO;
	}

	*result = inode;
	return 0;
}

static jnode * get_jnode_by_mapping (struct inode * inode, long index)
{
	struct page * page;
	jnode * node;

	page = grab_cache_page(inode->i_mapping, index);
	if (page == NULL)
		return ERR_PTR(-ENOMEM);
	node = jnode_of_page(page);
	unlock_page(page);
	page_cache_release(page);
	return node;
}

/* 
   Mark jnodes of given extent for repacking.
   @tap : lock, coord and load status for the tree traversal position,
   @max_nr_marked: a maximum number of nodes which can be marked for repacking,
   @return: error code if < 0, number of marked nodes otherwise. 
*/
int mark_extent_for_repacking (tap_t * tap, int max_nr_marked)
{
	coord_t * coord = tap->coord;
	reiser4_extent *ext;
	int nr_marked;
	struct inode * inode;
	unsigned long index, i;
	reiser4_block_nr width, start;
	int ret;

	ext = extent_by_coord(coord);

	if (state_of_extent(ext) == HOLE_EXTENT)
		return 0;

	width = extent_get_width(ext);
	start = extent_get_start(ext);
	index = extent_unit_index(coord);

	ret = get_reiser4_inode_by_tap(&inode, tap);
	if (ret)
		return ret;

	for (nr_marked = 0, i = 0; nr_marked < max_nr_marked && i < width; i++) {
		jnode * node;

		node = get_jnode_by_mapping(inode, index + i);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			break;
		}

		/* Freshly created jnode has no block number set. */
		if (node->blocknr == 0) {
			reiser4_block_nr block;
			block = start + index;
			jnode_set_block(node, &block);
		}

		if (!JF_ISSET(node, JNODE_REPACK)) {
			do {
				/* Check whether the node is already read. */
				if (!JF_ISSET(node, JNODE_PARSED)) {
					ret = jstartio(node);
					if (ret)
						break;
				}

				/* Add to the transaction */
				ret = try_capture(node, ZNODE_WRITE_LOCK, 0);
				if (ret)
					break;

				jnode_make_dirty_locked(node);
				JF_SET(node, JNODE_REPACK);

				nr_marked ++;
			} while (0);
		}
		jput(node);
		if (ret)
			break;
	}

	iput(inode);
	if (ret)
		return ret;
	return nr_marked;
}

/* Check should the repacker relocate this node. */
static int relocatable (jnode * check)
{
	return !JF_ISSET(check, JNODE_OVRWR) && !JF_ISSET(check, JNODE_RELOC);
}

static int replace_end_of_extent (coord_t * coord, reiser4_block_nr end_part_start, 
				  reiser4_block_nr end_part_width, int * all_replaced)
{
	reiser4_extent * ext;
	reiser4_block_nr ext_start;
	reiser4_block_nr ext_width;

	reiser4_item_data item;
	reiser4_extent new_ext, replace_ext;
	reiser4_block_nr replace_ext_width;
	reiser4_key key;

	assert ("zam-959", item_is_extent(coord));

	ext = extent_by_coord(coord);
	ext_start = extent_get_start(ext);
	ext_width = extent_get_width(ext);

	assert ("zam-960", end_part_width <= ext_width);

	replace_ext_width = ext_width - end_part_width;
	if (replace_ext_width == 0) {
		set_extent(ext, end_part_start, end_part_width);
		znode_make_dirty(coord->node);
		/* End part of extent is equal to the whole extent. */
		* all_replaced = 1;
		return 0;
	}

	set_extent(&replace_ext, ext_start, replace_ext_width);
	set_extent(&new_ext, end_part_start, end_part_width);

	unit_key_by_coord(coord, &key);
	set_key_offset(&key, get_key_offset(&key) + replace_ext_width * current_blocksize);

	return replace_extent(
		coord, znode_lh(coord->node, ZNODE_WRITE_LOCK), &key, 
		init_new_extent(&item, &new_ext, 1), &replace_ext, COPI_DONT_SHIFT_LEFT);
}

static int make_new_extent_at_end (coord_t * coord, reiser4_block_nr width, int * all_replaced)
{
	reiser4_extent * ext;
	reiser4_block_nr ext_start;
	reiser4_block_nr ext_width;
	reiser4_block_nr new_ext_start;

	assert ("zam-961", item_is_extent(coord));

	ext = extent_by_coord(coord);
	ext_start = extent_get_start(ext);
	ext_width = extent_get_width(ext);

	assert ("zam-962", width < ext_width);

	if (state_of_extent(ext) == ALLOCATED_EXTENT)
		new_ext_start = ext_start + ext_width - width;
	else
		new_ext_start = ext_start;

	return replace_end_of_extent(coord, new_ext_start, width, all_replaced);
}

static void parse_extent(coord_t * coord, reiser4_block_nr * start, reiser4_block_nr * width, long * ind)
{
	reiser4_extent * ext;

	ext   = extent_by_coord(coord);
	*start = extent_get_start(ext);
	*width = extent_get_width(ext);
	*ind   = extent_unit_index(coord);
}

static int skip_not_relocatable_extent(struct inode * inode, coord_t * coord, int * done)
{
	reiser4_block_nr ext_width, ext_start;
	long ext_index, reloc_start;
	jnode * check = NULL;
	int ret = 0;


	parse_extent(coord, &ext_start, &ext_width, &ext_index);

	for (reloc_start = ext_width - 1; reloc_start >= 0; reloc_start --) {
		check = get_jnode_by_mapping(inode, reloc_start + ext_index);
		if (IS_ERR(check))
			return PTR_ERR(check);

		if (relocatable(check)) {
			jput(check);
			if (reloc_start < ext_width - 1)
				ret = make_new_extent_at_end(coord, ext_width - reloc_start, done);
			return ret;
		}
		jput(check);
	}
	*done = 1;
	return 0;
}


static int relocate_extent (struct inode * inode, coord_t * coord, reiser4_blocknr_hint * hint, 
			    int *done, reiser4_block_nr * len)
{
	reiser4_block_nr ext_width, ext_start;
	long ext_index, reloc_ind;
	reiser4_block_nr new_ext_width, new_ext_start, new_block;
	int unallocated_flg;
	int ret = 0;

	parse_extent(coord, &ext_start, &ext_width, &ext_index);
	assert("zam-974", *len != 0);

	unallocated_flg = (state_of_extent(extent_by_coord(coord)) == UNALLOCATED_EXTENT);
	hint->block_stage = unallocated_flg ? BLOCK_UNALLOCATED : BLOCK_FLUSH_RESERVED;
		
	new_ext_width = *len;
	ret = reiser4_alloc_blocks(hint, &new_ext_start, &new_ext_width, BA_PERMANENT | BA_FORMATTED, __FUNCTION__);
	if (ret)
		return ret;

	hint->blk = new_ext_start;
	if (!unallocated_flg) {
		reiser4_block_nr dealloc_ext_start;

		dealloc_ext_start = ext_start + ext_width - *len;
		ret = reiser4_dealloc_blocks(&dealloc_ext_start, len, 0, BA_DEFER | BA_PERMANENT, __FUNCTION__);
		if (ret)
			return ret;
	}

	new_block = new_ext_start;
	for (reloc_ind = ext_width - new_ext_width; reloc_ind < ext_width; reloc_ind ++)
	{
		jnode * check;
		check = get_jnode_by_mapping(inode, ext_index + reloc_ind);
		if (IS_ERR(check))
			return PTR_ERR(check);
		assert("zam-975", relocatable(check));
		jnode_set_block(check, &new_block);
		new_block ++;

		JF_SET(check, JNODE_RELOC);
		JF_SET(check, JNODE_REPACK);

		jput(check);
	}

	ret = replace_end_of_extent(coord, new_ext_start, new_ext_width, done);
	*len = new_ext_width;
	return 0;
}

static int find_relocatable_extent (struct inode * inode, coord_t * coord,
				    int * nr_reserved, reiser4_block_nr * len)
{
	reiser4_block_nr ext_width, ext_start;
	long ext_index, reloc_end;
	jnode * check = NULL;
	int ret = 0;

	*len = 0;
	parse_extent(coord, &ext_start, &ext_width, &ext_index);

	for (reloc_end = ext_width - 1;
	     reloc_end >= 0 && *nr_reserved > 0; reloc_end --) 
	{
		check = get_jnode_by_mapping(inode, reloc_end + ext_index);
		if (IS_ERR(check))
			return PTR_ERR(check);
		if (!relocatable(check)) {
			assert("zam-973", reloc_end < ext_width - 1);
			goto out;
		}
		/* add node to transaction. */
		ret = try_capture(check, ZNODE_WRITE_LOCK, 0);
		if (ret)
			goto out;
		UNDER_SPIN_VOID(jnode, check, jnode_make_dirty_locked(check));
		jput(check);

		(*len) ++;
		(*nr_reserved) --;
	}
	if (0) {
	out:
		jput(check);
	}
	return ret;
}

static int find_and_relocate_end_of_extent (struct inode * inode, coord_t * coord,
					    int * nr_reserved, reiser4_blocknr_hint *hint, int * done)
{
	reiser4_block_nr len;
	int ret;

	ret = skip_not_relocatable_extent(inode, coord, done);
	if (ret || (*done))
		return ret;

	ret = find_relocatable_extent(inode, coord, nr_reserved, &len);
	if (ret)
		return ret;

	ret = relocate_extent(inode, coord, hint, done, &len);
	if (ret)
		return ret;
	return (int)len;
}

/* process (relocate) unformatted nodes in backward direction: from the end of extent to the its start.  */
int process_extent_backward_for_repacking (tap_t * tap, int max_nr_processed, reiser4_blocknr_hint * hint)
{
	coord_t * coord = tap->coord;
	reiser4_extent *ext;
	int nr_reserved = max_nr_processed;
	struct inode * inode = NULL;
	int done = 0;
	int ret;


	ext = extent_by_coord(coord);
	if (state_of_extent(ext) == HOLE_EXTENT)
		return 0;

	ret = get_reiser4_inode_by_tap(&inode, tap);

	while (!done && !ret)
		ret = find_and_relocate_end_of_extent(inode, coord, &nr_reserved, hint, &done);

	iput(inode);
	return ret;
}

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
