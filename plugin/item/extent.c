/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/*
 * prepare structure reiser4_item_data to put one extent unit into tree
 */
static reiser4_item_data * init_new_extent (reiser4_item_data * data,
					    void * ext_unit, int nr_extents)
{
	if (REISER4_DEBUG)
		memset (data, 0, sizeof (reiser4_item_data));

	data->data = ext_unit;
	/* data->data is kernel space */
	data->user = 0;
	data->length = sizeof (reiser4_extent) * nr_extents;
	data->arg = 0;
	data->iplug = item_plugin_by_id (EXTENT_POINTER_ID);
	return data;
}


/* how many bytes are addressed by @nr (or all of @nr == -1) first extents of
 * the extent item.  FIXME: Josh says this (unsigned) business is UGLY.  Make
 * it signed, since there can't be more than INT_MAX units in an extent item,
 * right? */
static reiser4_block_nr extent_size (const tree_coord * coord, unsigned nr)
{
	unsigned i;
	reiser4_block_nr blocks;
	reiser4_extent * ext;

	ext = item_body_by_coord (coord);
	if ((int)nr == -1) {
		nr = extent_nr_units (coord);
	} else {
		assert ("vs-263", nr <= extent_nr_units (coord));
	}

	blocks = 0;
	for (i = 0; i < nr; i ++, ext ++) {
		blocks += extent_get_width (ext);
	}

	return blocks * reiser4_get_current_sb ()->s_blocksize;
}


/*
 * plugin->u.item.b.max_key_inside
 */
reiser4_key * extent_max_key_inside (const tree_coord * coord, 
				     reiser4_key * key)
{
	item_key_by_coord (coord, key);
	set_key_offset (key, get_key_offset (max_key ()));
	return key;
}

/*
 * plugin->u.item.b.can_contain_key
 * this checks whether @key of @data is matching to position set by @coord
 */
int extent_can_contain_key (const tree_coord * coord, const reiser4_key * key,
			    const reiser4_item_data * data)
{
	reiser4_key item_key;


	if (item_plugin_by_coord (coord) != data->iplug)
		return 0;

	item_key_by_coord (coord, &item_key);
	if (get_key_locality (key) != get_key_locality (&item_key) ||
	    get_key_objectid (key) != get_key_objectid (&item_key))
		return 0;

	assert ("vs-458", coord->between == AFTER_UNIT);
	set_key_offset (&item_key, extent_size (coord, coord->unit_pos + 1));
	if (!keyeq (&item_key, key)) {
		info ("could not merge extent items of one file\n");
		return 0;
	}
	return 1;
}


/*
 * plugin->u.item.b.mergeable
 * first item is of extent type
 */
int extent_mergeable (const tree_coord * p1, const tree_coord * p2)
{
	reiser4_key key1, key2;

	assert ("vs-299", item_id_by_coord (p1) == EXTENT_POINTER_ID);
	/* FIXME-VS: Which is it? Assert or return 0 */
	if (item_id_by_coord (p2) != EXTENT_POINTER_ID) {
		return 0;
	}

	item_key_by_coord (p1, &key1);
	item_key_by_coord (p2, &key2);
	if (get_key_locality (&key1) != get_key_locality (&key2) ||
	    get_key_objectid (&key1) != get_key_objectid (&key2) ||
	    get_key_type (&key1) != get_key_type (&key2))
		return 0;
	if (get_key_offset (&key1) +
	    extent_size (p1, extent_nr_units (p1)) != get_key_offset (&key2))
		return 0;
	return 1;
}


/* extents in an extent item can be either holes, or unallocated or allocated
   extents */
typedef enum {
	HOLE_EXTENT,
	UNALLOCATED_EXTENT,
	ALLOCATED_EXTENT
} extent_state;


static extent_state state_of_extent (reiser4_extent * ext)
{
	switch (extent_get_start (ext)) {
	case 0:
		return HOLE_EXTENT;
	case 1:
		return UNALLOCATED_EXTENT;
	}
	return ALLOCATED_EXTENT;
}

int extent_is_allocated (tree_coord *item)
{
	assert ("jmacd-5133", item_is_extent (item)); 

	return state_of_extent (extent_item (item)) == ALLOCATED_EXTENT;
}

/*
 * plugin->u.item.b.print
 */
static const char * state2label (extent_state state)
{
	const char * label;

	label = 0;
	switch (state) {
	case HOLE_EXTENT:
		label = "hole";
		break;

	case UNALLOCATED_EXTENT:
		label = "unalloc";
		break;

	case ALLOCATED_EXTENT:
		label = "alloc";
		break;
	}
	assert ("vs-376", label);
	return label;
}

void extent_print (const char * prefix, tree_coord * coord)
{
	reiser4_extent * ext;
	unsigned i, nr;

	if (prefix)
		info ("%s:", prefix);

	nr = extent_nr_units (coord);
	ext = (reiser4_extent *)item_body_by_coord (coord);

	info ("%u: ", nr);
	for (i = 0; i < nr; i ++, ext ++) {
		info ("[%Lu (%Lu) %s]", extent_get_start (ext),
		      extent_get_width (ext),
		      state2label (state_of_extent (ext)));
	}
	info ("\n");
}


/*
 * plugin->u.item.b.check
 */


/*
 * plugin->u.item.b.nr_units
 */
unsigned extent_nr_units (const tree_coord * coord)
{
	/* length of extent item has to be multiple of extent size */
	if( (item_length_by_coord (coord) % sizeof (reiser4_extent)) != 0 )
		impossible("vs-10", "Wrong extent itrem size: %i, %i",
			   item_length_by_coord (coord), 
			   sizeof (reiser4_extent));
	return item_length_by_coord (coord) / sizeof (reiser4_extent);
}


/*
 * plugin->u.item.b.lookup
 */
lookup_result extent_lookup (const reiser4_key * key,
			     lookup_bias bias UNUSED_ARG,
			     tree_coord * coord) /* znode and item_pos are
						    set to an extent item to
						    look through */
{
	reiser4_key item_key;
	reiser4_block_nr lookuped, offset;
	unsigned i, nr_units;
 	reiser4_extent * ext;
	size_t blocksize;


	item_key_by_coord (coord, &item_key);
	offset = get_key_offset (&item_key);
	nr_units = extent_nr_units (coord);

	/*
	 * key we are looking for must be greater than key of item @coord
	 */
	assert ("vs-414", keygt (key, &item_key));

	if (keygt (key, extent_max_key_inside (coord, &item_key))) {
		/*
		 * @key is key of another file
		 */
		coord->unit_pos = nr_units - 1;
		coord->between = AFTER_UNIT;
		return CBK_COORD_NOTFOUND;
	}

	assert ("vs-11", get_key_objectid (key) == get_key_objectid (&item_key));

	ext = extent_by_coord (coord);
	blocksize = reiser4_get_current_sb ()->s_blocksize;
 
	/*
	 * offset we are looking for
	 */
	lookuped = get_key_offset (key);

	/*
	 * go through all extents until the one which address given offset
	 */
	for (i = 0; i < nr_units; i ++, ext ++) {
		offset += (blocksize * extent_get_width (ext));
		if (offset > lookuped) {
			/* desired byte is somewhere in this extent */
			coord->unit_pos = i;
			coord->between = AT_UNIT;
			return CBK_COORD_FOUND;
		}
	}

	/* 
	 * set coord after last unit
	 */
	coord->unit_pos = nr_units - 1;
	coord->between = AFTER_UNIT;
	return  CBK_COORD_FOUND;/*bias == FIND_MAX_NOT_MORE_THAN ? CBK_COORD_FOUND : CBK_COORD_NOTFOUND;*/
}


/* set extent width and convert type to on disk representation */
static void set_extent (reiser4_extent * ext, extent_state state,
			reiser4_block_nr width)
{
	reiser4_block_nr start;

	switch (state) {
	case HOLE_EXTENT:
		start = 0ull;
		break;
	case UNALLOCATED_EXTENT:
		start = 1ull;
		break;
	default:
		start = 0;
		impossible ("vs-338",
			    "do not create extents but holes and unallocated");
	}
	extent_set_start (ext, start);
	extent_set_width (ext, width);
}


/* plugin->u.item.b.init */
int extent_init (tree_coord * coord, reiser4_item_data * extent)
{
	unsigned width;
	assert ("vs-529",
		item_length_by_coord (coord) == sizeof (reiser4_extent));
	assert ("vs-530", coord->unit_pos == 0);
	assert ("vs-531", coord_of_unit (coord));
	/* extent was prepared in kernel space */
	assert ("vs-555", extent->user == 0);

	if (!extent || extent->data)
		/* body of item is provided, it will be copied */
		return 0;

	/* body of item is not set - therefore, we are inserting unallocated
	 * extent in tail2extent conversion. width of extent is in
	 * extent->arg */
	width = (unsigned)extent->arg;
	set_extent (extent_item (coord), UNALLOCATED_EXTENT,
		    (reiser4_block_nr)width);
	return 0;
}


/* plugin->u.item.b.paste 
   item @coord is set to has been appended with @data->length of free
   space. data->data contains data to be pasted into the item in position
   @coord->in_item.unit_pos. It must fit into that free space.
   @coord must be set between units.
*/
int extent_paste (tree_coord * coord, reiser4_item_data * data,
		  carry_level * todo UNUSED_ARG)
{
	unsigned old_nr_units;
	reiser4_extent * ext;
	int item_length;


	ext = extent_item (coord);
	item_length = item_length_by_coord (coord);
	old_nr_units = (item_length - data->length) / sizeof (reiser4_extent);

	/* this is also used to copy extent into newly created item, so
	   old_nr_units could be 0 */
	assert ("vs-260", item_length >= data->length);

	/* make sure that coord is set properly */
	assert ("vs-35", ((!coord_of_unit (coord)) || 
			  (!old_nr_units && !coord->unit_pos))); 
	
	/* first unit to be moved */
	switch (coord->between) {
	case AFTER_UNIT:
		coord->unit_pos ++;		
	case BEFORE_UNIT:
		coord->between = AT_UNIT;
		break;
	case AT_UNIT:
		assert ("vs-331", !old_nr_units && !coord->unit_pos);
		break;
	default:
		impossible ("vs-330", "coord is set improperly");
	}

	/* prepare space for new units */
	xmemmove (ext + coord->unit_pos + data->length / sizeof (reiser4_extent),
		 ext + coord->unit_pos,
		 (old_nr_units - coord->unit_pos) * sizeof (reiser4_extent));

	/* copy new data from kernel space */
	assert ("vs-556", data->user == 0);
	xmemcpy (ext + coord->unit_pos, data->data, (unsigned)data->length);

	/* after paste @coord is set to first of pasted units */
	assert ("vs-332", coord_of_unit (coord));
	assert ("vs-333", !memcmp (data->data, extent_by_coord (coord),
				   (unsigned)data->length));
	return 0;
}


/*
 * plugin->u.item.b.can_shift
 */
int extent_can_shift (unsigned free_space, tree_coord * source,
		      znode * target UNUSED_ARG, shift_direction pend UNUSED_ARG,
		      unsigned * size, unsigned want)
{
	*size = item_length_by_coord (source);
	if (*size > free_space)
		/* never split a unit of extent item */
		*size = free_space - free_space % sizeof (reiser4_extent);

	/* we can shift *size bytes, calculate how many do we want to shift */
	if (*size > want * sizeof (reiser4_extent))
		*size = want * sizeof (reiser4_extent);
	
	if( *size % sizeof (reiser4_extent) != 0 )
		impossible ("vs-119", "Wrong extern size: %i %i",
			    *size, sizeof (reiser4_extent));
	return *size / sizeof (reiser4_extent);

}


/*
 * plugin->u.item.b.copy_units
 */
void extent_copy_units (tree_coord * target, tree_coord * source,
			unsigned from, unsigned count, shift_direction where_is_free_space,
			unsigned free_space)
{
	char * from_ext, * to_ext;


	assert ("vs-217", free_space == count * sizeof (reiser4_extent));
	
	from_ext = item_body_by_coord (source);
	to_ext = item_body_by_coord (target);

	if (where_is_free_space == SHIFT_LEFT) {
		assert ("vs-215", from == 0);

		to_ext += (extent_nr_units (target) - count);
	} else {
		reiser4_key key;
		tree_coord coord;

		assert ("vs-216", from + count == last_unit_pos (source) + 1);

		from_ext += item_length_by_coord (source) - free_space;

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		coord = *source;
		coord.unit_pos = from;
		extent_unit_key (&coord, &key);

		node_plugin_by_node (target->node)->update_item_key (target, &key, 0/*todo*/);
	}

	xmemcpy (to_ext, from_ext, free_space);
}


/* 
 * plugin->u.item.b.create_hook
 * @arg is znode of leaf node for which we need to update right delimiting key
 */
int extent_create_hook (const tree_coord * coord, void * arg)
{
	tree_coord * child_coord;
	znode * node;
	reiser4_key key;

	if (!arg)
		return 0;

	/*
	 * find a node on the left level for which right delimiting key has to
	 * be updated
	 */
	child_coord = arg;
	if (coord_wrt (child_coord) == COORD_ON_THE_LEFT) {
		assert ("vs-411", znode_is_left_connected (child_coord->node));
		node = child_coord->node->left;
	} else {
		assert ("vs-412", coord_wrt (child_coord) == COORD_ON_THE_RIGHT);
		node = child_coord->node;
	}

	if (!node)
		return 0;

	assert ("vs-413", znode_is_write_locked (node));

	spin_lock_dk (current_tree);
	*znode_get_rd_key (node) = *item_key_by_coord (coord, &key);
	spin_unlock_dk (current_tree);
	
	/* break sibling links */
	spin_lock_tree (current_tree);
	if (ZF_ISSET (node, ZNODE_RIGHT_CONNECTED) && node->right) {
		ZF_CLR (node->right, ZNODE_LEFT_CONNECTED);
		node->right->left = 0;
		ZF_CLR (node, ZNODE_RIGHT_CONNECTED);
		node->right = 0;
	}
	spin_unlock_tree (current_tree);
	return 0;
}


/*
 * FIXME-VS: no check for return value here. See the comment in
 * plugin/item/item.h near kill_hook
 */
static int free_blocks (reiser4_block_nr start, reiser4_block_nr length)
{
	space_allocator_plugin * splug;

	splug = get_current_super_private ()->space_plug;
	splug->dealloc_blocks (get_space_allocator (reiser4_get_current_sb ()),
			       start, length);
	return 0;
}


/* plugin->u.item.b.kill_item_hook
 */
int extent_kill_item_hook (const tree_coord * coord, unsigned from,
			   unsigned count, void *kill_params UNUSED_ARG)
{
 	reiser4_extent * ext;
	unsigned i;


	ext = extent_by_coord (coord) + from;
	for (i = 0; i < count; i ++, ext ++) {
		
		if (state_of_extent (ext) != ALLOCATED_EXTENT) {
			continue;
		}
		/*
		 * FIXME-VS: do I need to do anything for unallocated extents
		 */
		free_blocks (extent_get_start (ext), extent_get_width (ext));
	}
	return 0;
}


static reiser4_key *last_key_in_extent (const tree_coord * coord, 
					reiser4_key *key)
{
	item_key_by_coord (coord, key);
	set_key_offset (key, get_key_offset (key) + 
			extent_size (coord, extent_nr_units (coord)));
	return key;
}


static int cut_or_kill_units (tree_coord * coord,
			      unsigned * from, unsigned * to,
			      int cut, const reiser4_key * from_key,
			      const reiser4_key * to_key,
			      reiser4_key * smallest_removed)
{
 	reiser4_extent * ext;
	reiser4_key key;
	unsigned blocksize;
	reiser4_block_nr offset;
	unsigned count;
	__u64 cut_from_to;


	count = *to - *from + 1;

	blocksize = reiser4_get_current_sb ()->s_blocksize;

	/*
	 * make sure that we cut something but not more than all units
	 */
	assert ("vs-220", count > 0 && count <= extent_nr_units (coord));
	/*
	 * extent item can be cut either from the beginning or down to the end
	 */
	assert ("vs-298", *from == 0 || *to == last_unit_pos (coord));
	

	item_key_by_coord (coord, &key);
	offset = get_key_offset (&key);

	if (smallest_removed) {
		/* set @smallest_removed assuming that @from unit will be
		   cut */
		*smallest_removed = key;
		set_key_offset (smallest_removed, (offset +
						   extent_size (coord, *from)));
	}

	cut_from_to = 0;

	/* it may happen that extent @from will not be removed */
	if (from_key) {
		reiser4_key key_inside;

		/* when @from_key (and @to_key) are specified things become
		 * more complex. It may happen that @from-th or @to-th extent
		 * will only decrease their width */

		assert ("vs-311", to_key);

		key_inside = key;
		set_key_offset (&key_inside, (offset +
					      extent_size (coord, *from)));
		if (keygt (from_key, &key_inside)) {
			/*
			 * @from-th extent can not be removed. Its width has to
			 * be decreased in accordance with @from_key
			 */
			reiser4_block_nr new_width;
			reiser4_block_nr first;

			/* cut from the middle of extent item is not allowed,
			 * make sure that the rest of item gets cut
			 * completely */
			assert ("vs-612", *to == last_unit_pos (coord));
			assert ("vs-613",
				keyge (to_key, extent_max_key (coord, &key_inside)));

			ext = extent_item (coord) + *from;
			first = offset + extent_size (coord, *from);
			new_width = (get_key_offset (from_key) + (blocksize - 1) - first) / blocksize;
			assert ("vs-307", new_width > 0 && new_width <= extent_get_width (ext));
			if (state_of_extent (ext) == ALLOCATED_EXTENT) {
				/*
				 * free blocks that are not addressed by this
				 * extent anymore
				 */				
				free_blocks (extent_get_start (ext) + new_width,
					     extent_get_width (ext) - new_width);
			}
			extent_set_width (ext, new_width);
			(*from) ++;
			count --;
			if (smallest_removed) {
				set_key_offset (smallest_removed, get_key_offset (from_key));
			}
		}

		set_key_offset (&key_inside, (offset +
					      extent_size (coord, *to + 1)));
		set_key_offset (&key_inside, get_key_offset (&key_inside) - 1);
		if (keylt (to_key, &key_inside)) {
			/* @to-th unit can not be removed completely */
			
			reiser4_block_nr new_width;

			/* cut from the middle of extent item is not allowed,
			 * make sure that head of item gets cut and cut is
			 * aligned to block boundary */
			assert ("vs-614", *from == 0);
			assert ("vs-615", keyle (from_key, &key));
			assert ("vs-616", ((get_key_offset (to_key) + 1) & 
					   (blocksize - 1)) == 0);

			ext = extent_item (coord) + *to;

			new_width = (get_key_offset (&key_inside) -
				     get_key_offset (to_key)) / blocksize;

			cut_from_to = (extent_get_width (ext) - new_width) * blocksize;

			assert ("vs-617", new_width > 0 && new_width < extent_get_width (ext));
			if (state_of_extent (ext) == ALLOCATED_EXTENT) {
				/*
				 * free blocks that are not addressed by this
				 * extent anymore
				 */
				free_blocks (extent_get_start (ext),
					     extent_get_width (ext) - new_width);
			}
			extent_set_width (ext, new_width);
			(*to) --;
			count --;
		}
	}

	if (!cut)
		/*
		 * call kill hook for all extents removed completely
		 */
		extent_kill_item_hook (coord, *from, count, NULL/*FIXME!!!*/);

	if (*from == 0 && count != last_unit_pos (coord) + 1) {
		/*
		 * part of item is removed from item beginning, update item key
		 * therefore
		 */
		item_key_by_coord (coord, &key);
		set_key_offset (&key, (get_key_offset (&key) +
				       extent_size (coord, count) +
				       cut_from_to));
		node_plugin_by_node (coord->node)->update_item_key (coord, &key, 0);
	}

	if (REISER4_DEBUG) {
		/* zero space which is freed as result of cut between keys */
		ext = extent_item (coord);
		xmemset (ext + *from, 0, count * sizeof (reiser4_extent));
	}

	return count * sizeof (reiser4_extent);
}


/*
 * plugin->u.item.b.cut_units
 */
int extent_cut_units (tree_coord * item, unsigned * from, unsigned * to,
		      const reiser4_key * from_key, const reiser4_key * to_key,
		      reiser4_key * smallest_removed)
{
	return cut_or_kill_units (item, from, to,
				  1, from_key, to_key, smallest_removed);
}


/*
 * plugin->u.item.b.kill_units
 */
int extent_kill_units (tree_coord * item, unsigned * from, unsigned * to,
		       const reiser4_key * from_key, const reiser4_key * to_key,
		       reiser4_key * smallest_removed)
{
	return cut_or_kill_units (item, from, to,
				  0, from_key, to_key, smallest_removed);
}


/*
 * plugin->u.item.b.unit_key
 */
reiser4_key * extent_unit_key (const tree_coord * coord, reiser4_key * key)
{
	assert ("vs-300", coord_of_unit (coord));

	item_key_by_coord (coord, key);
	set_key_offset (key, (get_key_offset (key) +
			      extent_size (coord, (unsigned) coord->unit_pos)));

	return key;
}


/*
 * plugin->u.item.b.estimate
 * plugin->u.item.b.item_data_by_flow
 */


/*
 * union union-able extents and cut an item correspondingly
 */
static void optimize_extent (tree_coord * item)
{
	unsigned i, old_num, new_num;
	reiser4_extent * ext, * prev, * start;
	reiser4_block_nr width;


	ext = start = extent_item (item);
	old_num = extent_nr_units (item);
	prev = 0;
	new_num = 0;
	for (i = 0; i < old_num; i ++, ext ++) {
		width = extent_get_width (ext);
		if (!width)
			continue;
		if (prev && state_of_extent (prev) == state_of_extent (ext)) {
			/* current extent (@ext) and previous one (@prev) can
			   be unioned when they are hole or unallocated
			   extents or when they are adjacent allocated
			   extents */
			if (state_of_extent (prev) != ALLOCATED_EXTENT) {
				set_extent (prev, state_of_extent (ext),
					    extent_get_width (prev) + width);
				continue;
			} else if (extent_get_start (prev) + extent_get_width (prev) ==
				   extent_get_start (ext)) {
				extent_set_width (prev, extent_get_width (prev) + width);
				continue;
			}
		}

		/* @ext can not be joined with @prev, move @prev forward */
		if (prev)
			prev ++;
		else
			prev = start;
		*prev = *ext;
		new_num ++;
	}
	if (new_num != old_num)	{
		int result;
		tree_coord from, to;

		dup_coord (&from, item);
		from.unit_pos = new_num;
		from.between = AT_UNIT;

		dup_coord (&to, &from);
		to.unit_pos = old_num - 1;

		result = cut_node (&from, &to, 0, 0, 0, DELETE_DONT_COMPACT, 0);
		done_coord (&from);
		done_coord (&to);
		/*
		 * nothing should happen cutting
		 */
		assert ("vs-456", result == 0);
	}
}


/* @coord is set to extent after which new extent(s) (@data) have to be
   inserted. Attempt to union adjacent extents is made. So, resulting item may
   be longer, shorter or of the same length as initial item */
static int add_extents (tree_coord * coord,
			lock_handle * lh,
			reiser4_key * key,
			reiser4_item_data * data)
{
	unsigned old_num;
 	unsigned i;
	reiser4_extent * prev, * ext, * start;
	unsigned width;
	unsigned new_num;
	unsigned before;
	unsigned delta;
	unsigned count;


	assert ("vs-337", coord->between == AFTER_UNIT);
	
	prev = 0;
	ext = start = extent_item (coord);
	old_num = extent_nr_units (coord);
	new_num = 0;

	/* try to combine extents before insertion coord */
	before = coord->unit_pos + 1;
	for (i = 0; i < before; i ++, ext ++) {
		width = extent_get_width (ext);
		if (!width)
			continue;
		if (prev && state_of_extent (prev) == state_of_extent (ext)) {
			/* current extent (@ext) and previous one (@prev) can
			   be unioned when they are hole or unallocated
			   extents or when they are adjacent allocated
			   extents */
			if (state_of_extent (prev) != ALLOCATED_EXTENT) {
				set_extent (prev, state_of_extent (ext),
					    extent_get_width (prev) + width);
				continue;
			} else if (extent_get_start (prev) + extent_get_width (prev) ==
				   extent_get_start (ext)) {
				extent_set_width (prev, extent_get_width (prev) + width);
				continue;
			}
		}

		/* @ext can not be joined with @prev, move @prev forward */
		if (prev)
			prev ++;
		else
			prev = start;
		*prev = *ext;
		new_num ++;
	}

	/* position of insertion might change */
	coord->unit_pos -= (before - new_num);

	/* try to combine extents after insertion coord */
	if (prev)
		start = prev + 1;
	prev = 0;
	for ( ; i < old_num; i ++, ext ++) {
		width = extent_get_width (ext);
		if (!width)
			continue;
		if (prev && state_of_extent (prev) == state_of_extent (ext)) {
			/* current extent (@ext) and previous one (@prev) can
			   be unioned when they are hole or unallocated
			   extents or when they are adjacent allocated
			   extents */
			if (state_of_extent (prev) != ALLOCATED_EXTENT) {
				set_extent (prev, state_of_extent (ext),
					    extent_get_width (prev) + width);
				continue;
			} else if (extent_get_start (prev) + extent_get_width (prev) ==
				   extent_get_start (ext)) {
				extent_set_width (prev, extent_get_width (prev) + width);
				continue;
			}
		}

		/* @ext can not be joined with @prev, move @prev forward */
		if (prev)
			prev ++;
		else
			prev = start;
		*prev = *ext;
		new_num ++;
	}

	delta = old_num - new_num;
	/* item length decreased by delta * sizeof (reiser4_extent) */

	/* number of extents to be added */
	count = data->length / sizeof (reiser4_extent);

	/* new data has to be added after position coord->unit_pos */
	xmemmove (extent_item (coord) + coord->unit_pos + 1 + min (count, delta),
		 extent_item (coord) + coord->unit_pos + 1,
		 (new_num - coord->unit_pos - 1) *
		 sizeof (reiser4_extent));
	/* copy part of new data into space freed by "optimizing" */
	assert ("vs-557", data->user == 0);
	xmemcpy (extent_item (coord) + coord->unit_pos + 1, data->data,
		min (count, delta) * sizeof (reiser4_extent));

	if (delta < count) {
		/* item length has to be increased */
		coord->unit_pos += delta;
		for (i = 0; i < delta; i ++)			
			set_key_offset (key, get_key_offset (key) +
					reiser4_get_current_sb ()->s_blocksize *
					extent_get_width ((reiser4_extent *)data->data + i));
		data->data += delta * sizeof (reiser4_extent);

		{
			reiser4_key tmp_key;

			item_key_by_coord (coord, &tmp_key);
			set_key_offset (&tmp_key, get_key_offset (&tmp_key) +
					extent_size (coord,
						     coord->unit_pos + 1));
			assert ("vs-619", keyeq (key, &tmp_key));
		}

		return resize_item (coord, data, key, lh, 0/*flags*/);
	}

	if (delta == count) {
		/* item remains of the same size, new data is in already */
		coord->unit_pos ++;
		coord->between = AT_UNIT;
		return 0;
	}

	/* item length has to be decreased, new data is in already */
	{
		tree_coord from, to;

		from = *coord;
		from.unit_pos = new_num + count;
		from.between = AT_UNIT;
		to = *coord;
		to.unit_pos = old_num - 1;
		to.between = AT_UNIT;
		return cut_node (&from, &to, 0, 0, 0, 0/*flags*/, 0);
	}
}


/*
 * position within extent pointed to by @coord to block containing given offset
 * @off
 */
static reiser4_block_nr in_extent (const tree_coord * coord,
			reiser4_block_nr off)
{
        reiser4_key key;
        reiser4_block_nr cur;
        reiser4_extent * ext;


        assert ("vs-266", coord_of_unit (coord));

        item_key_by_coord (coord, &key);
        cur = get_key_offset (&key) + extent_size (coord, (unsigned) coord->unit_pos);

        ext = extent_by_coord (coord);
	/*
	 * make sure that @off is within current extent
	 */
	assert ("vs-390", 
		off >= cur && 
		off < (cur + extent_get_width (ext) * 
		       reiser4_get_current_sb ()->s_blocksize));

        return (off - cur) / reiser4_get_current_sb ()->s_blocksize;
        
}


/* insert extent item (containing one unallocated extent of width 1) to place
   set by @coord */
static int insert_first_block (tree_coord * coord, lock_handle * lh,
			       reiser4_key * key, struct buffer_head * bh)
{
	int result;
	reiser4_extent ext;
	reiser4_item_data unit;
	reiser4_key first_key;


	assert ("vs-240",
		(get_key_offset (key) &
		 ~(reiser4_get_current_sb ()->s_blocksize - 1)) == 0);

	first_key = *key;
	set_key_offset (&first_key, 0ull);

	set_extent (&ext, UNALLOCATED_EXTENT, 1ull);
	result = insert_extent_by_coord (coord, init_new_extent (&unit, &ext, 1),
					 &first_key, lh);
	if (result) {
		return result;
	}

	/* do whatever is needed to be done with new block of a file */
	bh->b_dev = reiser4_get_current_sb ()->s_dev;
	mark_buffer_new (bh);
	mark_buffer_mapped (bh);
	mark_buffer_unallocated (bh);
	return 0;
}


/* @coord is set to the end of extent item. Append it with pointer to one
   block - either by expanding last unallocated extent or by appending a new
   one of width 1 */
static int append_one_block (tree_coord * coord,
			     lock_handle *lh, struct buffer_head * bh)
{
	int result;
	reiser4_extent * ext, new_ext;
	reiser4_item_data unit;
	reiser4_key key;

	assert ("vs-228",
		(coord->unit_pos == last_unit_pos (coord) &&
		 coord->between == AFTER_UNIT) ||
		coord->between == AFTER_ITEM);

	ext = extent_by_coord (coord);	
	switch (state_of_extent (ext)) {
	case UNALLOCATED_EXTENT:
		set_extent (ext, UNALLOCATED_EXTENT,
			    extent_get_width (ext) + 1);
		break;

	case HOLE_EXTENT:
	case ALLOCATED_EXTENT:
		/* we have to append one extent because last extent either not
		   unallocated extent or is full */
		set_extent (&new_ext, UNALLOCATED_EXTENT, 1ull);
		result = add_extents (coord, lh, last_key_in_extent (coord, &key),
				      init_new_extent (&unit, &new_ext, 1));
		if (result)
			return result;
		break;
	}

	/* do whatever is needed to be done with new block of a file */
	bh->b_dev = reiser4_get_current_sb ()->s_dev;
	mark_buffer_mapped (bh);
	mark_buffer_new (bh);
	mark_buffer_unallocated (bh);

	coord->unit_pos = last_unit_pos (coord);
	coord->between = AFTER_UNIT;
	return 0;
}


/* @coord is set to hole extent, replace it with unallocated extent of length
   1 and correct amount of hole extents around it */
static int plug_hole (tree_coord * coord, lock_handle * lh,
		      reiser4_block_nr off)
{
	reiser4_extent * ext,
		new_exts [2];
	reiser4_block_nr width, pos_in_unit;
	reiser4_key key;
	reiser4_item_data item;
	int count;

	assert ("vs-234", coord_of_unit (coord));

	ext = extent_by_coord (coord);
	width = extent_get_width (ext);
	pos_in_unit = in_extent (coord, off);

	if (width == 1) {
		set_extent (ext, UNALLOCATED_EXTENT, 1ull);
		return 0;
	} else if (pos_in_unit == 0) {		
		set_extent (ext, UNALLOCATED_EXTENT, 1ull);
		set_extent (&new_exts[0], HOLE_EXTENT, width - 1);
		count = 1;
	} else if (pos_in_unit == width - 1) {
		set_extent (ext, HOLE_EXTENT, width - 1);
		set_extent (&new_exts[0], UNALLOCATED_EXTENT, 1ull);
		count = 1;
	} else {
		set_extent (ext, HOLE_EXTENT, pos_in_unit);
		set_extent (&new_exts[0], UNALLOCATED_EXTENT, 1ull);
		set_extent (&new_exts[1], HOLE_EXTENT, width - pos_in_unit - 1);
		count = 2;
	}

	item_key_by_coord (coord, &key);
	set_key_offset (&key, (get_key_offset (&key) + 
			       extent_size (coord,
					    (unsigned) coord->unit_pos + 1)));

	coord->between = AFTER_UNIT;
	return add_extents (coord, lh, &key, init_new_extent (&item, new_exts, count));
}


/* return value is block number pointed by the @coord */
static reiser4_block_nr blocknr_by_coord_in_extent (tree_coord * coord,
						    reiser4_block_nr off)
{
	assert ("vs-12", coord_of_unit (coord));
	assert ("vs-264", state_of_extent (extent_by_coord (coord)) ==
		ALLOCATED_EXTENT);

	return extent_get_start (extent_by_coord (coord)) + in_extent (coord, off);
}

/**
 * Return the inode and its key.
 */
int extent_get_inode_and_key (const tree_coord *item, struct inode **inode, reiser4_key *key)
{
	item_key_by_coord (item, key);

	/* FIXME: Probably not quite right. */
	set_key_type (key, KEY_SD_MINOR);
	set_key_offset (key, 0ull);
	(*inode) = reiser4_iget (reiser4_get_current_sb (), key);

	if (*inode == NULL) {
		/* Inode must be in memory.  FIXME: why? */
		return -EIO;
	}

	return 0;
}

/**
 * Return the inode.
 */
int extent_get_inode (tree_coord *item, struct inode **inode)
{
	reiser4_key key;

	return extent_get_inode_and_key (item, inode, & key);
}

/**
 * Return the reiser_extent and position within that extent.
 */
static reiser4_extent* extent_utmost_ext ( const tree_coord *coord, sideof side,
					   reiser4_block_nr *pos_in_unit )
{
	reiser4_extent * ext;

	if (side == LEFT_SIDE) {
		/*
		 * get first extent of item
		 */
		ext = extent_item (coord);
		*pos_in_unit = 0;
	} else {
		/*
		 * get last extent of item and last position within it
		 */
		assert ("vs-363", side == RIGHT_SIDE);
		ext = extent_item (coord) + last_unit_pos (coord);
		*pos_in_unit = extent_get_width (ext) - 1;
	}

	return ext;
}

/**
 * Return the child.
 */
int extent_utmost_child ( const tree_coord *coord, sideof side, jnode **childp )
{
	reiser4_extent * ext;
	reiser4_block_nr pos_in_unit;

	ext = extent_utmost_ext (coord, side, & pos_in_unit);
	
	switch (state_of_extent (ext)) {
	case HOLE_EXTENT:
		*childp = NULL;
		return 0;
	case ALLOCATED_EXTENT:
	case UNALLOCATED_EXTENT:
		break;
	}

	{
		reiser4_key key;
		reiser4_block_nr offset;
		struct inode * inode;
		struct page * pg;

		item_key_by_coord (coord, &key);

		/* FIXME: Probably not quite right. */
		set_key_type (&key, KEY_SD_MINOR);
		set_key_offset (&key, 0ull);
		inode = reiser4_iget (reiser4_get_current_sb (), &key);
		if (!inode) {
			/* inode must be in memory */
			return -EIO;
		}

		offset   = get_key_offset (&key) +
			pos_in_unit * reiser4_get_current_sb ()->s_blocksize;
		assert ("vs-544", offset >> PAGE_CACHE_SHIFT < ~0ul);
		pg = find_get_page (inode->i_mapping, (unsigned long)(offset >> PAGE_CACHE_SHIFT));

		if (pg == NULL || IS_ERR (pg)) {
			iput (inode);
			return -EIO;
		}

		*childp = jnode_of_page (pg);

		page_cache_release (pg);
		iput (inode);
	}

	return 0;
}


/**
 * Return whether the child is dirty.
 */
int extent_utmost_child_dirty ( const tree_coord *coord, sideof side, int *is_dirty )
{
	int ret;
	reiser4_extent * ext;
	reiser4_block_nr pos_in_unit;
	jnode *child;

	ext = extent_utmost_ext (coord, side, & pos_in_unit);
	
	switch (state_of_extent (ext)) {
	case ALLOCATED_EXTENT:
		break;
	case HOLE_EXTENT:
		*is_dirty = 0;
		return 0;
	case UNALLOCATED_EXTENT:
		*is_dirty = 1;
		return 0;
	}

	if ((ret = extent_utmost_child (coord, side, &child))) {
		return ret;
	}

	if (child == NULL) {
		*is_dirty = 0;
	} else {
		*is_dirty = jnode_is_dirty (child);
		jput (child);
	}
	return 0;
}

/**
 * Return the child's block, if allocated.
 */
int extent_utmost_child_real_block ( const tree_coord *coord, sideof side, reiser4_block_nr *block )
{
	reiser4_extent * ext;
	reiser4_block_nr pos_in_unit;

	ext = extent_utmost_ext (coord, side, & pos_in_unit);
	
	switch (state_of_extent (ext)) {
	case ALLOCATED_EXTENT:
		*block = extent_get_start (ext) + pos_in_unit;
		break;
	case HOLE_EXTENT:
	case UNALLOCATED_EXTENT:
		*block = 0;
		break;
	}

	return 0;
}


/* plugin->u.item.b.real_max_key_inside */
reiser4_key * extent_max_key (const tree_coord * coord, 
			      reiser4_key * key)
{
	last_key_in_extent (coord, key);
	assert ("vs-610", get_key_offset (key) &&
		(get_key_offset (key) & 
		 (reiser4_get_current_sb ()->s_blocksize - 1)) == 0);
	set_key_offset (key, get_key_offset (key) - 1);
	return key;
}


/* pointer to block for @bh exists in extent item and it is addressed by
   @coord. If it is hole - make unallocated extent for it. */
static int overwrite_one_block (tree_coord * coord, lock_handle * lh,
				struct buffer_head * bh,
				reiser4_block_nr off)
{
	reiser4_extent * ext;
	int result;


	ext = extent_by_coord (coord);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		bh->b_blocknr = blocknr_by_coord_in_extent (coord, off);
		break;

	case UNALLOCATED_EXTENT:
		assert ("vs-353", buffer_unallocated (bh));
		assert ("vs-354", buffer_mapped (bh));
		assert ("vs-355", buffer_new (bh));
		break;
		
	case HOLE_EXTENT:
		result = plug_hole (coord, lh, off);
		if (result)
			return result;
		
		/* do whatever is needed to be done with new block of a file */
		mark_buffer_new (bh);
		mark_buffer_unallocated (bh);
		break;

	default:
		impossible ("vs-238", "extent of unknown type found");
		return -EIO;
	}

	bh->b_dev = reiser4_get_current_sb ()->s_dev;
	mark_buffer_mapped (bh);
	return 0;
}


typedef enum {
	EXTENT_CREATE_HOLE,
	EXTENT_APPEND_HOLE,
	EXTENT_FIRST_BLOCK,
	EXTENT_APPEND_BLOCK,
	EXTENT_OVERWRITE_BLOCK,
	EXTENT_RESEARCH,
	EXTENT_CANT_CONTINUE
} extent_write_todo;


/* @coord is set either to the end of last extent item of a file or to a place
   where first item of file has to be inserted to. Calculate size of hole to
   be inserted. If that hole is too big - only part of it is inserted */
static int add_hole (tree_coord * coord, lock_handle * lh,
		     reiser4_key * key,
		     extent_write_todo todo)
{
	reiser4_extent * ext, new_ext;
	reiser4_block_nr hole_off; /* hole starts at this offset */
	reiser4_block_nr hole_width;
	int result;
	reiser4_item_data item;
	reiser4_key last_key;


	if (todo == EXTENT_CREATE_HOLE) {
		/* there are no items of this file yet. First item will be
		   hole extent inserted here */
		hole_off = 0;
	} else {
		assert ("vs-251", todo == EXTENT_APPEND_HOLE);
		hole_off = get_key_offset (last_key_in_extent(coord, &last_key));
	}

	hole_width = (get_key_offset (key) - hole_off) /
		reiser4_get_current_sb ()->s_blocksize;

	/* compose body of hole extent */
	set_extent (&new_ext, HOLE_EXTENT, hole_width);
	/* prepare item data for insertion */
	init_new_extent (&item, &new_ext, 1);

	result = 0;
	if (todo == EXTENT_CREATE_HOLE) {
		reiser4_key hole_key;

		hole_key = *key;
		set_key_offset (&hole_key, 0ull);

		result = insert_extent_by_coord (coord, &item, &hole_key, lh);
	} else {
 		/* @coord points to last extent of the item and to its last block */
		assert ("vs-29",
			coord->unit_pos == last_unit_pos (coord) &&
			coord->between == AFTER_UNIT);
		/* last extent in the item */
		ext = extent_by_coord (coord);
		switch (state_of_extent (ext)) {
		case HOLE_EXTENT:
			/* last extent of a file is hole extent. Try to put at
			   least part of hole being appended into file */
			/* existing hole gets expanded with @to_hole blocks */
			set_extent (ext, HOLE_EXTENT, extent_get_width (ext) + hole_width);
			break;

		case ALLOCATED_EXTENT:
		case UNALLOCATED_EXTENT:
			/* append hole extent */
			result = add_extents (coord, lh,
					      last_key_in_extent (coord, &last_key),
					      &item);

			coord->unit_pos = last_unit_pos (coord);
			coord->between = AFTER_UNIT;
		}
	}

	return result;

}


/*
 * does extent @coord contain @key
 */
static int key_in_extent (tree_coord * coord, reiser4_key * key)
{
	reiser4_extent * ext;
	reiser4_key ext_key;

	unit_key_by_coord (coord, &ext_key);
	ext = extent_by_coord (coord);

	return get_key_offset (key) >= get_key_offset (&ext_key) &&
		get_key_offset (key) < get_key_offset (&ext_key) +
		extent_get_width (ext) * reiser4_get_current_sb ()->s_blocksize;
}


/*
 * does item @coord contain @key
 */
static int key_in_item (tree_coord * coord, reiser4_key * key)
{
	reiser4_key item_key;


	return ((keyle (item_key_by_coord (coord, &item_key), key)) &&
		(keylt (key, last_key_in_extent (coord, &item_key))));
}


/* this is todo for regular reiser4 files which are made of extents
   only. Extents represent every byte of file body. One node may not have more
   than one extent item of one file */
static extent_write_todo extent_what_todo (tree_coord * coord, reiser4_key * key,
					   int to_block)
{
	reiser4_key coord_key;
	tree_coord left, right;
	__u64 fbb_offset; /* offset of First Byte of Block */


	if (ZF_ISSET (coord->node, ZNODE_HEARD_BANSHEE))
		/* znode is not in tree already */
		return EXTENT_RESEARCH;

	/* offset of First Byte of Block key->offset falls to */
	fbb_offset = get_key_offset (key) & ~(reiser4_get_current_sb ()->s_blocksize - 1);

	if (!znode_contains_key_lock (coord->node, key)) {
		assert ("vs-602", !node_is_empty (coord->node));
		coord_first_unit (&left, coord->node);
		item_key_by_coord (&left, &coord_key);

		if (keylt (key, &coord_key)) {
			/* if left neighbor of coord->node is unformatted node
			 * of another file we can get here even if coord->node
			 * does not contain key we are looking for */
			if (znode_get_level (coord->node) == LEAF_LEVEL &&
			    coord_is_before_item (coord, 0)) {
				if (fbb_offset == 0)
					return EXTENT_FIRST_BLOCK;
				else
					return EXTENT_CREATE_HOLE;
			}
			return EXTENT_RESEARCH;
		} else {
			/* key we were looking for is greater that right
			 * delimiting key */
			return EXTENT_RESEARCH;			
		}
	}

	if (coord_of_unit (coord))
		return key_in_extent (coord, key) ? EXTENT_OVERWRITE_BLOCK : EXTENT_RESEARCH;

	if (coord_between_items (coord)) {
		if (node_is_empty (coord->node)) {
			if (fbb_offset == 0)
				return EXTENT_FIRST_BLOCK;
			else
				return EXTENT_CREATE_HOLE;
		}

		item_key_by_coord (coord, &coord_key);

		/* look at the item to the right of @coord*/
		right = *coord;
		if (coord_set_to_right (&right) == 0) {
			reiser4_key right_key;

			/* there is an item to the right of @coord */
			item_key_by_coord (&right, &right_key);
			if (get_key_objectid (key) == get_key_objectid (&right_key)) {
				/* this looks like a tail2extent in
				 * progress. Make sure that writing @to_block
				 * bytes we do not overlap with tail item of
				 * this file which is in tree yet */
				if (get_key_offset (key) + to_block <= get_key_offset (&right_key)) {
					assert ("",
						get_key_objectid (&coord_key) == get_key_objectid (key) &&
						get_key_locality (&coord_key) == get_key_locality (key));
					if (znode_get_level (coord->node) == TWIG_LEVEL)
						return EXTENT_APPEND_BLOCK;
					else {
						assert ("", get_key_offset (key) == 0);
						return EXTENT_FIRST_BLOCK;
					}
				}
				print_key ("RIGHT", &right_key);
				print_key ("FLOW KEY", key);
				info ("extent_what_todo: "
				      "there is item of this file to the right "
				      "of insertion coord\n");
				return EXTENT_CANT_CONTINUE;
			}
		} else {
			spin_lock_dk (current_tree);
			if (get_key_objectid (znode_get_rd_key (coord->node)) ==
			    get_key_objectid (&coord_key)) {
				/* this looks like a tail2extent in
				 * progress. Make sure that writing @to_block
				 * bytes we do not overlap with tail item of
				 * this file which is in tree yet */
				if (get_key_offset (key) + to_block <= get_key_offset (znode_get_rd_key (coord->node))) {
					assert ("",
						get_key_objectid (&coord_key) == get_key_objectid (key) &&
						get_key_locality (&coord_key) == get_key_locality (key));
					spin_unlock_dk (current_tree);
					if (znode_get_level (coord->node) == TWIG_LEVEL)
						return EXTENT_APPEND_BLOCK;
					else {
						assert ("", get_key_offset (key) == 0);
						return EXTENT_FIRST_BLOCK;
					}
				}
				info ("extent_what_todo: there is item of this file "
				      "in right neighbor\n");
				spin_unlock_dk (current_tree);
				return EXTENT_CANT_CONTINUE;
			}
			spin_unlock_dk (current_tree);
		}


		/* look at the item to the left of @coord*/
		left = *coord;
		if (coord_set_to_left (&left)) {
			info ("extent_what_todo: "
			      "no item to the left of insertion coord\n");
			return EXTENT_RESEARCH;
		}

		item_key_by_coord (&left, &coord_key);
		if (get_key_objectid (key) != get_key_objectid (&coord_key) ||
		    item_id_by_coord (&left) != EXTENT_POINTER_ID) {
			/* @coord is set between items of other files */
			if (fbb_offset == 0)
				return EXTENT_FIRST_BLOCK;
			else
				return EXTENT_CREATE_HOLE;
		}

		/* item to the left of @coord is extent item of this file */
		if (fbb_offset < get_key_offset (&coord_key) +
		    extent_size (&left, (unsigned)-1))
			return EXTENT_RESEARCH;
		if (fbb_offset == get_key_offset (&coord_key) +
		    extent_size (&left, (unsigned)-1))
			return EXTENT_APPEND_BLOCK;
		else
			return EXTENT_APPEND_HOLE;
	}

	return EXTENT_RESEARCH;
}


/*
 * return 1 we do not need to read block before overwriting
 */
static int overwritten_entirely (loff_t file_size,
				 reiser4_block_nr file_off,
				 int count, int block_off)
{
	if (block_off == 0 && (count == (int)reiser4_get_current_sb ()->s_blocksize ||
			       file_off + count >= (reiser4_block_nr)file_size))
		return 1;
	return 0;
}


/* map all buffers of @page the write falls to. Number of bytes which can be
   written into @page are returned via @count */
static int prepare_write (tree_coord * coord, lock_handle * lh,
			  struct page * page, reiser4_key * key,
			  unsigned * count)
{
	int result;
	int cur_off;
	struct buffer_head * bh, * being_read [2];
	int read_nr;
	unsigned long blocksize;
	int page_off, to_page;	
	int block_off, to_block;
	extent_write_todo todo;
	reiser4_key tmp_key;
	reiser4_block_nr file_off;
		

	blocksize = reiser4_get_current_sb ()->s_blocksize;

	if (!page->buffers)
		create_empty_buffers (page, blocksize);

	tmp_key = *key;
	file_off = get_key_offset (&tmp_key);

	/* offset within the page where we start writing from */
	page_off = (int)(file_off & (PAGE_SIZE - 1));
	/* amount of bytes which are to be written to this page */
	to_page = PAGE_SIZE - page_off;
	if ((unsigned)to_page > *count)
		to_page = *count;
	*count = 0;
	read_nr = 0;
	result = 0;
	for (cur_off = 0, bh = page->buffers; to_page > 0;
	     bh = bh->b_this_page) {
		cur_off += blocksize;
		if (page_off >= cur_off)
			/* we do not have to write to this buffer of the
			   page */
			continue;

		block_off = page_off & (blocksize - 1);
		to_block = blocksize - block_off;
		if (to_block > to_page)
			to_block = to_page;

		if (!buffer_mapped (bh)) {
			todo = extent_what_todo (coord, &tmp_key, to_block);
			switch (todo) {
			case EXTENT_CREATE_HOLE:
			case EXTENT_APPEND_HOLE:
				return add_hole (coord, lh, &tmp_key,
						 todo);

			case EXTENT_FIRST_BLOCK:
				/* create first item of the file */
				result = insert_first_block (coord, lh,
							     &tmp_key, bh);
				assert ("vs-252", buffer_new (bh));
				break;

			case EXTENT_APPEND_BLOCK:
				result = append_one_block (coord, lh, bh);
				assert ("vs-253", buffer_new (bh));		
				break;

			case EXTENT_OVERWRITE_BLOCK:
				/* there is found extent (possibly hole
				   one) */
				result = overwrite_one_block (coord, lh,
							      bh, file_off);
				if (buffer_new (bh)) {
					;
				}
				break;

			case EXTENT_CANT_CONTINUE:
				result = -EIO;
				break;

			case EXTENT_RESEARCH:
				/* @coord is not set to a place in a file we
				   have to write to, so, coord_by_key must be
				   called to find that place */
				result = -EAGAIN;
				reiser4_stat_file_add (write_repeats);
				break;

			default:
				impossible ("vs-334", "unexpected case "
					    "in what_todo");
			}
		}

		if (result == -EAGAIN)
			/* coord is set such that we were not able to map this
			   buffer, but we might manage to map few first
			   ones */
			break;
		if (result)
			/* error occurs filling the page */
			return result;

		/* we can write to current buffer */
		assert ("vs-340", buffer_mapped (bh));

/*		block_off = page_off & (blocksize - 1);
		to_block = blocksize - block_off;
		if (to_block > to_page)
			to_block = to_page;
*/
		if (buffer_allocated (bh) &&
		    !buffer_uptodate (bh) &&
		    !overwritten_entirely (page->mapping->host->i_size,
					   file_off, block_off, to_block)) {
			/* block contains data which do not get overwritten, so
			   we have to read the block */
			ll_rw_block (READ, 1, &bh);
			being_read [read_nr ++] = bh;
		}

		to_page -= to_block;
		*count += to_block;
		file_off += to_block;
		set_key_offset (&tmp_key, file_off);
	}

	while (read_nr --) {
		wait_on_buffer (being_read [read_nr]);
		if (!buffer_uptodate (being_read [read_nr]))
			return -EIO;
	}

	return result;
}


/*
 * mark all modified buffers dirty and update, if all buffers are uptodate -
 * mark page uptodate, update inode's i_size
 *
 * FIXME-VS: shouldn't we call generic_commit_write instead?
 */
static int commit_write (struct page * page, reiser4_block_nr file_off,
			 unsigned count)
{
	struct inode * inode;
	unsigned page_off, cur_off, blocksize, to_block;
	struct buffer_head * bh;
	int partial;


	inode = page->mapping->host;
	if ((loff_t)file_off + count > inode->i_size) {
		inode->i_size = file_off + count;
		mark_inode_dirty (inode);
	}

	blocksize = reiser4_get_current_sb ()->s_blocksize;
	page_off = file_off & ~PAGE_MASK;
	partial = 0;
	for (cur_off = 0, bh = page->buffers; count > 0; bh = bh->b_this_page) {
		cur_off += blocksize;
		if (page_off >= cur_off) {
			/*
			 * this buffer was not modified
			 */
			if (!buffer_uptodate (bh))
				partial = 1;
			continue;
		}

		make_buffer_uptodate (bh, 1);
		/*
		 * FIXME-VS: do whatever else is necessary with modified buffer
		 */
		mark_buffer_dirty (bh);
		to_block = blocksize - (page_off & (blocksize - 1));
		if (to_block > count)
			to_block = count;
		count -= to_block;
	}
	if (!partial)
		SetPageUptodate(page);

	return 0;
}


/*
 * plugin->u.item.s.file.write
 * wouldn't it be possible to modify it such that appending of a lot of bytes
 * is done in one modification of extent?
 */
int extent_write (struct inode * inode, tree_coord * coord,
		  lock_handle * lh, flow_t * f, struct page * page)
{
	int result;
	reiser4_block_nr file_off; /* offset within a file we write to */
	reiser4_block_nr blocksize;
	int research = 0;
	char * p_data;
	unsigned count;
	int written;


	blocksize = reiser4_get_current_sb ()->s_blocksize;
	file_off = get_key_offset (&f->key);
	written = 0;
	p_data = 0;
	while (f->length) {
		if (f->user == 1) {
			assert ("vs-586", !page);
			page = grab_cache_page (inode->i_mapping,
						(unsigned long)(file_off >> PAGE_CACHE_SHIFT));
			if (!page) {
				return -ENOMEM;
			}
			/* Capture the page. */
/*
			result = txn_try_capture_page (page, ZNODE_WRITE_LOCK, 0);
			if (result != 0) {
				goto capture_failed;
			}
*/
			p_data = kmap (page);
		} else {
			/* tail2extent is in progress */
		}

		count = f->length;
		result = prepare_write (coord, lh, page, &f->key, &count);
		if (result && result != -EAGAIN) {
			/* error occurred */
			if (f->user == 1) {
				kunmap (page);
				unlock_page (page);
				page_cache_release (page);
			}
			return result;
		}

		if (result == -EAGAIN)
			/* not entire page is prepared for writing into it */
			research = 1;

		if (f->user == 1) {
			result = __copy_from_user (p_data + (file_off & ~PAGE_MASK),
						   f->data, count);
			flush_dcache_page (page);
			
			commit_write (page, file_off, count);

			kunmap (page);
			unlock_page (page);
			page_cache_release (page);
			if (result)
				return result;
			/* this is to grab next page */
			page = 0;
		}

		file_off += count;
		written += count;
		move_flow_forward (f, count);
		if (research)
			return -EAGAIN;
	}

	return 0;
}


/*
 * start read on several buffers
 */
static void submit_bhs (struct buffer_head ** bhs, unsigned nr)
{
	unsigned i;

	for (i = 0; i < nr; i ++) {
		lock_buffer (bhs [i]);
		set_buffer_async_io (bhs [i]);
	}

	for (i = 0; i < nr; i ++)
		submit_bh (READ, bhs [i]);
}


/*
 * extent_readpage uses search.c:iterate_tree() to go through all
 * extents pointing to blocks a page consists of. This is because page may
 * consist of several blocks and therefore extents addressing page blocks may
 * be located in more than one node. iterate_tree will iterate all
 * those extents and call map_extent for every one. This map_extent gets
 * pointer to struct readpage_desc and records there how mapping of page
 * buffers is going
 */
struct readpage_desc {
	struct page * page;      /* page being read */
	struct buffer_head * bh; /* buffer of page which was not proceeded
				    yet */
	unsigned done_nr;        /* number of buffers in the page we have
				    proceeded */
	struct buffer_head *bhs [PAGE_SIZE / 512]; /* array of buffers which
						      have to be read */
	unsigned have_to_read;   /* number of buffers in the array in above */
	
};


/*
 * @arg is "readpage descriptor" which contains information about how many page
 * buffers have been proceeded already
 */
static int map_extent (reiser4_tree * tree UNUSED_ARG,
		       tree_coord * coord,
		       lock_handle *lh UNUSED_ARG, void * arg)
{
	reiser4_extent * ext;
	reiser4_block_nr pos_in_unit, width;
	reiser4_block_nr start;
	unsigned nr;
	struct page * page;
	struct inode * inode;
	struct buffer_head * bh;
	struct readpage_desc * desc;
	unsigned blocksize;
	unsigned i;

	
	desc = (struct readpage_desc *)arg;
	page = desc->page;
	assert ("vs-290", PageLocked (page));
	
	/*
	 * not proceeded buffer
	 */
	bh = desc->bh;
	

	inode = page->mapping->host;
	
	if (item_id_by_coord (coord) != EXTENT_POINTER_ID ||
	    !inode_file_plugin (inode)->owns_item (inode, coord)) {
		warning ("vs-283", "there should be more items of file\n");
		return -EIO;
	}
	
	ext = extent_by_coord (coord);
	blocksize = reiser4_get_current_sb ()->s_blocksize;
	pos_in_unit = in_extent (coord, (reiser4_block_nr)page->index * PAGE_SIZE + 
				 desc->done_nr * blocksize);
	start = extent_get_start (ext);
	width = extent_get_width (ext);

	/*
	 * number of buffers of @page we can proceed using this extent
	 */
	nr = PAGE_SIZE / blocksize - desc->done_nr;
	if (width - pos_in_unit < nr)
		nr = width - pos_in_unit;

	for (i = 0; i < nr;
	     i ++, bh = bh->b_this_page, desc->done_nr ++, pos_in_unit ++) {
		if (buffer_uptodate (bh))
			continue;
		if (!buffer_mapped (bh)) {
			if (state_of_extent (ext) == HOLE_EXTENT) {
				xmemset (kmap (page) +
					desc->done_nr * blocksize,
					0, blocksize);
				flush_dcache_page (page);
				kunmap (page);
				make_buffer_uptodate (bh, 1);
				continue;
			} else {
				assert ("vs-281", state_of_extent (ext) ==
					ALLOCATED_EXTENT);
				map_bh (bh, reiser4_get_current_sb (),
					start + pos_in_unit);
			}
		}
		/*
		 * one more block to be read
		 */
		desc->bhs [desc->have_to_read ++] = bh;
	}

	if (bh != page->buffers) {
		/*
		 * not all buffers of this page are done
		 */
		if ((reiser4_block_nr)round_up (inode->i_size, blocksize) !=
		    ((reiser4_block_nr)page->index * PAGE_SIZE + 
		     desc->done_nr * blocksize)) {
			/*
			 * file should continue in next item
			 */
			desc->bh = bh;
			return 1;
		}

		/*
		 * file is over, padd the rest of page with zeros
		 */
		nr = PAGE_SIZE / blocksize - desc->done_nr;
		xmemset (kmap (page) + desc->done_nr * blocksize,
			0, blocksize * nr);
		flush_dcache_page (page);
		kunmap (page);
		for (i = 0; i < nr; i ++, desc->done_nr ++, bh = bh->b_this_page)
			make_buffer_uptodate (bh, 1);
	}

	return 0;
}


/*
 * set @coord to an extent containing beginning of the @page
 */
static int reset_coord (struct page * page,
			tree_coord * coord, lock_handle * lh)
{
	int result;
	reiser4_key key;

	/*
	 * get key of first byte of the page
	 */
	item_key_by_coord (coord, &key);
	set_key_offset (&key, (reiser4_block_nr)page->index << PAGE_CACHE_SHIFT);

	if (key_in_extent (coord, &key)) {
		/*
		 * beginning of page is in extent @coord
		 */
		return 0;
	} else if (key_in_item (coord, &key)) {
		/*
		 * key is in item, but as arg->coord is set to different
		 * extent, we have to find that extent
		 */
		result = extent_lookup (&key, FIND_EXACT, coord);
	} else {
		/*
		 * we have to recall coord_by_key to find an item
		 * corresponding to the beginning of page being read
		 */
		assert ("vs-389", coord->item_pos == 0);
		done_lh (lh);
		done_coord (coord);

		init_coord (coord);
		init_lh (lh);
		result = coord_by_key (current_tree, &key, coord, lh,
				       ZNODE_READ_LOCK, FIND_EXACT,
				       LEAF_LEVEL, LEAF_LEVEL, CBK_UNIQUE);
	}
	return result;
}


/*
 * plugin->u.item.s.file.readpage
 * map all buffers of the page and start io if necessary
 */
int extent_readpage (void * p, struct page * page)
{
	int result;
	unsigned blocksize;
	struct readpage_desc desc;
	struct readpage_arg * arg;

	arg = (struct readpage_arg *)p;
	blocksize = reiser4_get_current_sb ()->s_blocksize;
	if (!page->buffers)
		create_empty_buffers (page, blocksize);

	/*
	 * set arg->coord to extent containing beginning of page
	 */
	result = reset_coord (page, arg->coord, arg->lh);
	if (result)
		return result;

	xmemset (&desc, 0, sizeof (struct readpage_desc));
	desc.page = page;
	desc.bh = page->buffers;
	/*
	 * go through extents until all buffers are mapped
	 */
	result = iterate_tree (current_tree, arg->coord, arg->lh,
			       map_extent, &desc,
			       ZNODE_READ_LOCK, 1 /* through units */);
	if (result)
		return result;

	/*
	 * all the page buffers are mapped
	 */
	assert ("vs-391", desc.done_nr == PAGE_SIZE / blocksize);
	if (!desc.have_to_read) {
		/*
		 * there is nothing to read
		 */
		SetPageUptodate (page);
		unlock_page (page);
	} else {
		/*
		 * start read for not uptodate buffers of the page
		 */
		submit_bhs (desc.bhs, desc.have_to_read);
	}

	return 0;
}


/*
 * do not read more than MAX_READAHEAD pages ahead
 */
#define MAX_READAHEAD 10

/*
 * when doing readahead mm/filemap.c:read_cache_page() is called with this
 * function as a filler. The page consists of contiguous blocks. @arg is
 * pointer to block number of first of them
 */
static int extent_readpage_ahead (void * arg, struct page * page)
{
	struct buffer_head *bhs [PAGE_SIZE / 512];
	struct buffer_head * bh;
	reiser4_block_nr block;
	unsigned nr;

	
	if (!page->buffers)
		create_empty_buffers (page, reiser4_get_current_sb ()->s_blocksize);
	bh = page->buffers;
	block = *(reiser4_block_nr *)arg;
	nr = 0;
	do {
		if (buffer_uptodate (bh))
			continue;
		if (!buffer_mapped (bh)) {
			map_bh (bh, reiser4_get_current_sb (), block);
		} else {
			assert ("vs-325", bh->b_blocknr == block);
		}
		block ++;
		bhs [nr ++] = bh;
	} while (block ++, bh = bh->b_this_page, bh != page->buffers);
	
	submit_bhs (bhs, nr);
	return 0;
}


/*
 * @coord is set to extent addressing last block of @page which . If this
 * extent is allocated one we try to use the rest of it to readahead few pages
 */
static void read_ahead (struct page * page, tree_coord * coord)
{
	int i;
	reiser4_block_nr start, can_readahead;
	int blocks_per_page;
	struct page * ra_page;
	reiser4_extent * ext;
	reiser4_block_nr pos_in_extent, offset;


	/*
	 * item @coord must belong to the file we read
	 */
	assert ("vs-392", ({
		struct inode * inode;
		inode = page->mapping->host;
		inode_file_plugin (inode)->owns_item (inode, coord);
	}));
	
	ext = extent_by_coord (coord);
	if (state_of_extent (ext) != ALLOCATED_EXTENT)
		return;

 	blocks_per_page = PAGE_SIZE / reiser4_get_current_sb ()->s_blocksize;

	/*
	 * offset of last block in the page
	 */
	offset = ((reiser4_block_nr)page->index << PAGE_CACHE_SHIFT) + PAGE_SIZE - 
		reiser4_get_current_sb ()->s_blocksize;
	/*
	 * position wihin the extent of next block
	 */
	pos_in_extent = in_extent (coord, offset) + 1;

	/*
	 * how many blocks does the rest of extent can we readahead (not looking
	 * further than current extent)
	 */
	can_readahead = ((extent_get_width (ext) - pos_in_extent) /
			 blocks_per_page);
	if (can_readahead > MAX_READAHEAD)
		can_readahead = MAX_READAHEAD;

	start = extent_get_start (ext) + pos_in_extent;

	for (i = 0; i < (int)can_readahead; i ++, start += blocks_per_page) {
		ra_page = read_cache_page (page->mapping, page->index + i + 1,
					   extent_readpage_ahead, &start);
		page_cache_release (ra_page);
	}
}


/*
 * plugin->u.item.s.file.read
 */
int extent_read (struct inode * inode, tree_coord * coord,
		 lock_handle * lh, flow_t * f)
{
	int result;
	struct page * page;
	unsigned long page_nr;
	struct readpage_arg arg;
	unsigned page_off, count;
	char * kaddr;


	page_nr = (get_key_offset (&f->key) >> PAGE_CACHE_SHIFT);
	arg.coord = coord;
	arg.lh = lh;

	/* this will return page if it exists and is uptodate, otherwise it
	 * will allocate page and call extent_readpage to fill it */
	page = read_cache_page (inode->i_mapping, page_nr, extent_readpage, &arg);

	if (IS_ERR (page)) {
		return PTR_ERR (page);
	}

	if (!Page_Uptodate (page)) {
		/* make some readahead pages of extent we stopped at and start
		 * reading them */
		read_ahead (page, arg.coord);
	}

	wait_on_page (page);
	
	if (!Page_Uptodate (page)) {
		page_cache_release (page);
		return -EIO;
	}

	/* Capture the page. */
/*
	result = txn_try_capture_page (page, ZNODE_READ_LOCK, 0);
	if (result != 0) {
		goto capture_failed;
	}
*/
	
	/* position within the page to read from */
	page_off = (get_key_offset (&f->key) & ~PAGE_MASK);

	/* number of bytes which can be read from the page */
	if (page_nr == (inode->i_size >> PAGE_CACHE_SHIFT)) {
		/* we read last page of a file. Calculate size of file tail */
		count = inode->i_size & ~PAGE_MASK;
	} else {
		count = PAGE_SIZE;
	}
	assert ("vs-388", count > page_off);
	count -= page_off;
	if (count > f->length)
		count = f->length;

	kaddr = kmap (page);
	assert ("vs-572", f->user == 1);
	result = __copy_to_user (f->data, kaddr + page_off, count);
	kunmap (page);

	page_cache_release (page);
	if (result) {
		return result;
	}
	
	move_flow_forward (f, count);
/*
	f->data.user_buf += count;
	f->length -= count;
	set_key_offset (&f->key, get_key_offset (&f->key) + count);
*/
	return 0;
	
}


/*
 * ask block allocator for some blocks
 */
static int extent_allocate_blocks (reiser4_block_nr desired_first,
				   reiser4_block_nr wanted_count,
				   reiser4_block_nr * first_allocated,
				   reiser4_block_nr * allocated)
{
	int result;
	reiser4_blocknr_hint preceder;


	preceder.blk = desired_first;
	*allocated = wanted_count;
	result = reiser4_alloc_blocks (&preceder, first_allocated, allocated);
	if (result) {
		/*
		 * no free space
		 * FIXME-VS: returning -ENOSPC is not enough
		 * here. It should not happen actully
		 */
		impossible ("vs-420", "could not allocate unallocated");
	}

	return result;
}



/*
 * unallocated extent of file with @objectid correspondign to @offset was
 * replaced allocated extent [first, count]. Look for corresponding buffers in
 * the page cache and map them properly
 */
static void map_allocated_buffers (reiser4_key * key, reiser4_block_nr first, 
				   reiser4_block_nr count)
{
	reiser4_block_nr objectid;
	reiser4_block_nr offset;
	struct inode * inode;
	struct page * page;
	struct buffer_head * bh;
	int blocksize, page_off;
	unsigned long ind;
	reiser4_key sd_key;


	objectid = get_key_objectid (key);
	offset = get_key_offset (key);
	blocksize = reiser4_get_current_sb ()->s_blocksize;

	sd_key = *key;
	set_key_type (&sd_key, KEY_SD_MINOR);
	set_key_offset (&sd_key, 0ull);
	inode = reiser4_iget (reiser4_get_current_sb (), &sd_key);
	assert ("vs-348", inode);

	while (1) {
		ind = offset >> PAGE_CACHE_SHIFT;
		page = find_lock_page (inode->i_mapping, ind);
		assert ("vs-349", page && page->buffers);
		
		bh = page->buffers;
		for (page_off = 0; offset != (ind << PAGE_CACHE_SHIFT) + page_off;
		     page_off += blocksize, bh = bh->b_this_page);

		do {
			assert ("vs-350", buffer_mapped (bh));
			assert ("vs-351", buffer_unallocated (bh));
			bh->b_blocknr = first ++;
			mark_buffer_allocated (bh);
			offset += blocksize;
			count --;
			bh = bh->b_this_page;
		} while (count && bh != page->buffers);

		unlock_page (page);
		page_cache_release (page);
		if (!count)
			break;
	}
	iput (inode);
}


#if 0 /*
       * allocate extents inflating them to right
       */

static void check_resize_result (tree_coord * coord, reiser4_key * key UNUSED_ARG)
{
	reiser4_key k;


	assert ("vs-326",
		coord->between == AT_UNIT);
	item_key_by_coord (coord, &k);
	set_key_offset (&k, (get_key_offset (&k) +
			     extent_size (coord, (unsigned) coord->unit_pos)));
	assert ("vs-329", keyeq (&k, key));
}


/* this replaces unallocated extents with allocated ones. @coord may change to
   another node */
static int allocate_unallocated_extent (tree_coord * coord,
					lock_handle * lh,
					reiser4_key * key)
{
	int result;
	unsigned i, nr_units;
	reiser4_extent * ext;
	reiser4_block_nr hint, width;
	int blocksize;
	reiser4_key tmp_key;

		
	blocksize = reiser4_get_current_sb ()->s_blocksize;

	assert ("vs-319", in_extent (coord, get_key_offset (key)) == 0);
	assert ("vs-342", (get_key_offset (key) & (blocksize - 1)) == 0);


 restart:
	hint = 0;
	nr_units = extent_nr_units (coord);
	ext = extent_item (coord) + coord->unit_pos;
	for (i = coord->unit_pos; i < nr_units; i ++, ext ++, coord->unit_pos ++) {		
		width = extent_get_width (ext);
		assert ("vs-320", width);
		switch (state_of_extent (ext)) {
		case ALLOCATED_EXTENT:
			hint = extent_get_start (ext) + extent_get_width (ext);
		case HOLE_EXTENT:
			set_key_offset (key, (get_key_offset (key) + 
					      width * blocksize));
			continue;
		case UNALLOCATED_EXTENT:
			{
				reiser4_block_nr first, needed;
				reiser4_extent new_ext;
				reiser4_item_data item;


				/* replace unallocated extent with an
				   allocated one */
				needed = width;
				first = hint;
				result = allocate_new_blocks (&first, &needed);
				if (result)
					return result;
				/* @needed is now how many blocks were
				   allocated */
				assert ("vs-321", first > 0 && needed > 0 &&
					needed <= width);
				
				hint = first + needed;
				/* store key we have done so far */
				set_key_offset (key, (get_key_offset (key) + 
						      needed * blocksize));

				/* update extent in place */
				extent_set_start (ext, first);
				extent_set_width (ext, needed);

				if (width == needed)
					item.length = 0;
				else {
					/* part of extent left unallocated,
					   insert new unallocated extent right
					   after the one just allocated */
					set_extent (&new_ext, UNALLOCATED_EXTENT,
						    width - needed);
					init_new_extent (&item, &new_ext);
				}

				coord->between = AFTER_UNIT;
				result = add_extents (coord, lh, key, &item);
				if (result)
					return result;

				/*
				 * set b_blocknrs of all buffers corresponding
				 * to newly allocated extent
				 */
				set_blocknrs (get_key_objectid (key),
					      get_key_offset (key) - needed * blocksize,
					      first, needed);

				if (!coord_of_unit (coord) ||
				    !keyeq (key, unit_key_by_coord (coord, &tmp_key)))
					/* have to research a place where we
					   stopped at */
					return 1;
				if (REISER4_DEBUG)
					/* after resize_item @coord is supposed
					   to be set to extent next to just
					   allocated one */
					check_resize_result (coord, key);
				goto restart;
			}
		}
	}

	assert ("vs-327", coord->unit_pos == extent_nr_units (coord));
	return 0;
}

#endif /*
       * allocate extents inflating them to right
       */



/*
 * return 1 if @extent unit needs allocation, 0 - otherwize. Try to update
 * preceder in parent-first order for next block which will be allocated
 * FIXME-VS: this only returns 1 for unallocated extents. It may be modified to
 * return 1 for allocated extents all unformatted nodes of which are in
 * memory. But that would require changes to allocate_extent_item as well
 */
static int extent_needs_allocation (reiser4_extent * extent, reiser4_blocknr_hint * preceder)
{
	if (state_of_extent (extent) == UNALLOCATED_EXTENT) {
		return 1;
	}
	if (state_of_extent (extent) == ALLOCATED_EXTENT) {
		/* recalculate preceder */
		preceder->blk = extent_get_start (extent) + extent_get_width (extent) - 1;
	}
	return 0;
}


/*
 * if @key is glueable to the item @coord is set to
 */
static int must_insert (tree_coord * coord, reiser4_key * key)
{
	reiser4_key last;

	if (item_id_by_coord (coord) == EXTENT_POINTER_ID &&
	    keyeq (last_key_in_extent (coord, &last), key))
		return 0;
	return 1;
}


/*
 * append last item in the @node with @data if @data and last item are
 * mergeable, otherwise insert @data after last item in @node
 *
 * FIXME-VS: me might want to try to union last extent in item @left and @data
 */
static int put_unit_to_end (znode * node, reiser4_key * key,
			    reiser4_item_data * data)
{
	int result;
	tree_coord coord;
	cop_insert_flag flags;


	init_coord (&coord);
	coord_last_unit (&coord, node);
	coord.between = AFTER_UNIT;

	flags = COPI_DONT_SHIFT_LEFT | COPI_DONT_SHIFT_RIGHT | COPI_DONT_ALLOCATE;
	if (must_insert (&coord, key)) {
		result = insert_by_coord (&coord, data, key, 0/*lh*/, 0/*ra*/,
					  0/*ira*/, flags);
					  
	} else {
		result = resize_item (&coord, data, key, 0/*lh*/, flags);
	}
	done_coord (&coord);
	assert ("vs-438", result == 0 || result == -ENOSPC);
	return result;
}


/*
 * try to expand last extent in node @left by @allocated. Return 1 in case of
 * success and 0 otherwise
 */
static int try_to_glue (znode * left, tree_coord * right,
			reiser4_block_nr first_allocated,
			reiser4_block_nr allocated)
{
	int result;
	tree_coord last;	
	reiser4_key item_key, last_key;
	reiser4_extent * ext;

	assert ("vs-463", !node_is_empty (left));

	if (right->unit_pos != 0)
		return 0;

	init_coord (&last);
	coord_last_unit (&last, left);
	ext = extent_by_coord (&last);

	if (item_plugin_by_coord (&last) != item_plugin_by_coord (right))
		return 0;

	result = 0;
	if (keyeq (last_key_in_extent (&last, &last_key), &item_key) &&
	    (extent_get_start (ext) + extent_get_width (ext) ==
	     first_allocated)) {
		/*
		 * @first_allocated is adjacent to last block of last extent in
		 * @left
		 */
		extent_set_width (ext, extent_get_width (ext) + allocated);
		result = 1;
	}
	done_coord (&last);
	return result;
}


/*
 * @right is extent item. @left and @right are extents of one file. Copy item
 * @right to @left unit by unit. Units which do not require allocation are
 * copied as they are. Units requiring allocation are copied after destinating
 * start block number and extent size to them. Those units may get inflated due
 * to impossibility to allocate desired number of contiguous free blocks
 */
int allocate_and_copy_extent (znode * left, tree_coord * right,
			      reiser4_blocknr_hint * preceder,
			      /*
			       * biggest key which was moved, it is
			       * * maintained while shifting is in *
			       * progress and is used to cut right *
			       * node at the end
			       */
			      reiser4_key * stop_key)
{
	int result;
	reiser4_item_data data;
	reiser4_key key;
	unsigned long blocksize;
	reiser4_block_nr first_allocated;
	reiser4_block_nr to_allocate, allocated;
	reiser4_extent * ext, new_ext;


	blocksize = reiser4_get_current_sb ()->s_blocksize;

	assert ("vs-419", item_id_by_coord (right) == EXTENT_POINTER_ID);
	assert ("vs-418", right->unit_pos == 0);


	optimize_extent (right);

	result = SQUEEZE_CONTINUE;
	item_key_by_coord (right, &key);

	ext = extent_item (right);
	for (; right->unit_pos < coord_num_units (right); right->unit_pos ++, ext ++) {
		if (!extent_needs_allocation (ext, preceder)) {
			/*
			 * unit does not require allocation, copy this unit as
			 * it is
			 */
			result = put_unit_to_end (left, &key,
						  init_new_extent (&data, ext, 1));
			if (result == -ENOSPC) {
				/*
				 * left->node does not have enough free space
				 * for this unit
				 */
				result = SQUEEZE_TARGET_FULL;
				goto done;
			}
			/*
			 * update stop key
			 */
			set_key_offset (&key, get_key_offset (&key) +
					extent_get_width (ext) * blocksize);
			*stop_key = key;
			set_key_offset (stop_key, get_key_offset (&key) - 1);
			result = SQUEEZE_CONTINUE;
			continue;
		}
		/*
		 * extent must be allocated
		 */
		to_allocate = extent_get_width (ext);
		/*
		 * while whole extent is allocated
		 */
		while (to_allocate) {
			result = extent_allocate_blocks (preceder->blk, to_allocate,
							 &first_allocated,
							 &allocated);
			if (result) {
				return result;
			}

			to_allocate -= allocated;
			preceder->blk += allocated;
			if (try_to_glue (left, right, first_allocated, allocated)) {
				/*
				 * find all pages containing allocated blocks
				 * and map corresponding buffers
				 */
				map_allocated_buffers (&key, first_allocated,
						       allocated);
				/*
				 * update stop key
				 */
				set_key_offset (&key, get_key_offset (&key) +
						allocated * blocksize);
				*stop_key = key;
				set_key_offset (stop_key, get_key_offset (&key) - 1);
				result = SQUEEZE_CONTINUE;
				continue;
			}
			/*
			 * append @left with new extent
			 */
			extent_set_start (&new_ext, first_allocated);
			extent_set_width (&new_ext, (reiser4_block_nr)allocated);

			result = put_unit_to_end (left, &key,
						  init_new_extent (&data,
								   &new_ext, 1));
			if (result == -ENOSPC) {
				/*
				 * left->node is full, free blocks we grabbed
				 * because this extent will stay in
				 * right->node, and, therefore, blocks it
				 * points to have different predecer in
				 * parent-first order
				 */
				free_blocks (first_allocated, allocated);
				result = SQUEEZE_TARGET_FULL;
				goto done;
			}
			/*
			 * find all pages containing allocated blocks and map
			 * corresponding buffers
			 */
			map_allocated_buffers (&key, first_allocated,
					       allocated);
			/*
			 * update stop key
			 */
			set_key_offset (&key, get_key_offset (&key) +
					allocated * blocksize);
			*stop_key = key;
			set_key_offset (stop_key, get_key_offset (&key) - 1);
			result = SQUEEZE_CONTINUE;
		}
	}
 done:

	assert ("vs-421", result == SQUEEZE_TARGET_FULL || SQUEEZE_CONTINUE);

	coord_first_item_unit (right);
	return result;
}


/*
 * paste new unallocated extent of width after unit @coord is set to. Ask
 * paste_into_item to not try to shift anything to left
 */
static int paste_unallocated_extent (tree_coord * item, reiser4_key * key,
				     reiser4_block_nr width)
{
	int result;
	tree_coord coord;
	reiser4_item_data data;
	reiser4_extent new_ext;


	set_extent (&new_ext, UNALLOCATED_EXTENT, width);
	
	dup_coord (&coord, item);
	coord.between = AFTER_UNIT;
	/*
	 * have paste_into_item to not shift anything to left
	 */
	result = resize_item (&coord, init_new_extent (&data, &new_ext, 1), key,
			      0/*lh*/, COPI_DONT_SHIFT_LEFT);
	done_coord (&coord);
	return result;
}


/*
 * find all units of extent item which require allocation. Allocate free blocks
 * for them and replace those extents with new ones. As result of this item may
 * "inflate", so, special precautions are taken to have it to inflate to right
 * only, so items to the right of @item and part of item itself may get moved
 * to right
 */
int allocate_extent_item_in_place (tree_coord * item, reiser4_blocknr_hint * preceder)
{
	int result;
	unsigned i;
 	reiser4_extent * ext;
	reiser4_block_nr first_allocated;
	reiser4_block_nr initial_width, allocated;
	reiser4_key key;
	unsigned long blocksize;


	blocksize = reiser4_get_current_sb ()->s_blocksize;

	assert ("vs-451", item->unit_pos == 0 && coord_of_unit (item));

	ext = extent_item (item);
	for (i = 0; i < coord_num_units (item); i ++, ext ++, item->unit_pos ++) {
		if (!extent_needs_allocation (ext, preceder))
			continue;
		assert ("vs-439", state_of_extent (ext) == UNALLOCATED_EXTENT);

		/*
		 * try to get extent_width () free blocks starting from
		 * *preceder
		 */
		initial_width = extent_get_width (ext);
		result = extent_allocate_blocks (preceder->blk, initial_width,
						 &first_allocated, &allocated);
		if (result)
			return result;

		assert ("vs-440", allocated > 0);
		/*
		 * update extent's start, width and recalculate preceder
		 */
		extent_set_start (ext, first_allocated);
		extent_set_width (ext, allocated);
		preceder->blk = first_allocated + allocated - 1;

		unit_key_by_coord (item, &key);
		/*
		 * find all pages containing allocated blocks and map
		 * corresponding buffers
		 */
		map_allocated_buffers (&key, first_allocated, allocated);

		if (allocated == initial_width)
			/*
			 * whole extent is allocated
			 */
			continue;
		/*
		 * update @key offset to offset of the part of extent which was
		 * not allocated
		 */
		set_key_offset (&key, get_key_offset (&key) + allocated * blocksize);
		/*
		 * put the rest of extent into tree
		 */
		result = paste_unallocated_extent (item, &key, 
						   initial_width - allocated);
		if (result)
			return result;
		/*
		 * make sure that @item is still set to position it was set to
		 * before paste_unallocate_extent
		 */
		assert ("vs-441",
			({
				reiser4_key ext_key;

				unit_key_by_coord (item, &ext_key);
				set_key_offset (&ext_key,
						get_key_offset (&ext_key) +
						allocated * blocksize);
				keyeq (&ext_key, &key);
			}));
	}

	/*
	 * content of extent item may be optimize-able (it may have mergeable
	 * extents))
	 */
	optimize_extent (item);
	coord_last_item_unit (item);
	return 0;
}


/*
 * iterate_tree's actor is to test extent allocation
 */
int alloc_extent (reiser4_tree * tree UNUSED_ARG, tree_coord * coord,
		  lock_handle * lh UNUSED_ARG, void * arg UNUSED_ARG)
{
	reiser4_blocknr_hint preceder;

	if (item_id_by_coord (coord) != EXTENT_POINTER_ID) {
		return 1;
	}

	/*
	 * try to calculate block number of left neighbor in parent-first order
	 */
	if (coord->item_pos == 0) {
		preceder.blk = *znode_get_block (coord->node);
	} else {
		tree_coord prev;
		reiser4_block_nr da;

		assert ("vs-455", coord->unit_pos == 0);
		dup_coord (&prev, coord);
		coord_prev_unit (&prev);
		if (item_is_internal (&prev)) {
			item_plugin_by_coord (&prev)->s.internal.down_link (&prev, 0,
						       &da);
			preceder.blk = da;
		} else if (item_id_by_coord (&prev) == EXTENT_POINTER_ID) {
			reiser4_extent * ext;
			ext = extent_by_coord (&prev);
			preceder.blk = extent_get_start (ext) + extent_get_width (ext);
		} else
			impossible ("vs-454", "unknown item type");
		done_coord (&prev);
	}

	/* FIXME: used only by ulevel, but what about this return value? */
	allocate_extent_item_in_place (coord, &preceder);
	return 1;
}


/* Block offset of firts block addressed by unit */
__u64 extent_unit_index (tree_coord * item)
{
	reiser4_key key;

	assert ("vs-648", coord_of_unit (item));
	unit_key_by_coord (item, &key);
	return get_key_offset (&key) / reiser4_get_current_sb ()->s_blocksize;
}


__u64 extent_unit_width (tree_coord * item)
{
	assert ("vs-649", coord_of_unit (item));
	return width_by_coord (item);
}


/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode: linux-c
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
