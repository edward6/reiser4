/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/*
 * prepare structure reiser4_item_data to put one extent unit into tree
 */
/* Audited by: green(2002.06.13) */
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
/* Audited by: green(2002.06.13) */
static reiser4_block_nr extent_size (const coord_t * coord, unsigned nr)
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

	return blocks * current_blocksize;
}


/*
 * plugin->u.item.b.max_key_inside
 */
/* Audited by: green(2002.06.13) */
reiser4_key * extent_max_key_inside (const coord_t * coord, 
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
/* Audited by: green(2002.06.13) */
int extent_can_contain_key (const coord_t * coord, const reiser4_key * key,
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
/* Audited by: green(2002.06.13) */
int extent_mergeable (const coord_t * p1, const coord_t * p2)
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


/* Audited by: green(2002.06.13) */
static extent_state state_of_extent (reiser4_extent * ext)
{
	switch ((int)extent_get_start (ext)) {
	case 0:
		return HOLE_EXTENT;
	case 1:
		return UNALLOCATED_EXTENT;
	default:
		break;
	}
	return ALLOCATED_EXTENT;
}

/* Audited by: green(2002.06.13) */
int extent_is_unallocated (const coord_t *item)
{
	assert ("jmacd-5133", item_is_extent (item)); 

	return state_of_extent (extent_by_coord (item)) == UNALLOCATED_EXTENT;
}

/*
 * plugin->u.item.b.print
 */
/* Audited by: green(2002.06.13) */
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

/* Audited by: green(2002.06.13) */
void extent_print (const char * prefix, coord_t * coord)
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
/* Audited by: green(2002.06.13) */
unsigned extent_nr_units (const coord_t * coord)
{
	/* length of extent item has to be multiple of extent size */
	if( (item_length_by_coord (coord) % sizeof (reiser4_extent)) != 0 )
		impossible("vs-10", "Wrong extent item size: %i, %i",
			   item_length_by_coord (coord), 
			   sizeof (reiser4_extent));
	return item_length_by_coord (coord) / sizeof (reiser4_extent);
}


/*
 * plugin->u.item.b.lookup
 */
/* Audited by: green(2002.06.13) */
lookup_result extent_lookup (const reiser4_key * key,
			     lookup_bias bias UNUSED_ARG,
			     coord_t * coord) /* znode and item_pos are
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
	blocksize = current_blocksize;
 
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
/* Audited by: green(2002.06.13) */
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
/* Audited by: green(2002.06.13) */
int extent_init (coord_t * coord, reiser4_item_data * extent)
{
	unsigned width;
	assert ("vs-529",
		item_length_by_coord (coord) == sizeof (reiser4_extent));
	assert ("vs-530", coord->unit_pos == 0);
	assert ("vs-531", coord_is_existing_unit (coord));
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
/* Audited by: green(2002.06.13) */
int extent_paste (coord_t * coord, reiser4_item_data * data,
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
	assert ("vs-35", ((!coord_is_existing_unit (coord)) || 
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
	assert ("vs-332", coord_is_existing_unit (coord));
	assert ("vs-333", !memcmp (data->data, extent_by_coord (coord),
				   (unsigned)data->length));
	return 0;
}


/*
 * plugin->u.item.b.can_shift
 */
/* Audited by: green(2002.06.13) */
int extent_can_shift (unsigned free_space, coord_t * source,
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
		impossible ("vs-119", "Wrong extent size: %i %i",
			    *size, sizeof (reiser4_extent));
	return *size / sizeof (reiser4_extent);

}


/*
 * plugin->u.item.b.copy_units
 */
/* Audited by: green(2002.06.13) */
void extent_copy_units (coord_t * target, coord_t * source,
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
		coord_t coord;

		assert ("vs-216", from + count == coord_last_unit_pos (source) + 1);

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
/* Audited by: green(2002.06.13) */
int extent_create_hook (const coord_t * coord, void * arg)
{
	coord_t * child_coord;
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

	/* FIXME: NIKITA->VS: Nobody knows if this assertion is right or wrong. */
	/*assert ("vs-413", znode_is_write_locked (node));*/

	spin_lock_dk (current_tree);
	*znode_get_rd_key (node) = *item_key_by_coord (coord, &key);
	spin_unlock_dk (current_tree);
	
	/* break sibling links */
	spin_lock_tree (current_tree);
	if (ZF_ISSET (node, ZNODE_RIGHT_CONNECTED) && node->right) {
		/*ZF_CLR (node->right, ZNODE_LEFT_CONNECTED);*/
		node->right->left = NULL;
		/*ZF_CLR (node, ZNODE_RIGHT_CONNECTED);*/
		node->right = NULL;
	}
	spin_unlock_tree (current_tree);
	return 0;
}


/* plugin->u.item.b.kill_item_hook
 */
/* Audited by: green(2002.06.13) */
int extent_kill_item_hook (const coord_t * coord, unsigned from,
			   unsigned count, void *kill_params UNUSED_ARG)
{
 	reiser4_extent * ext;
	unsigned i;
	reiser4_block_nr start, length;


	ext = extent_by_coord (coord) + from;
	for (i = 0; i < count; i ++, ext ++) {
		
		if (state_of_extent (ext) != ALLOCATED_EXTENT) {
			continue;
		}
		/*
		 * FIXME-VS: do I need to do anything for unallocated extents
		 */
		start = extent_get_start (ext);
		length = extent_get_width (ext);
		/* "defer" parameter is set to 1 because blocks which get freed
		 * are not safe to be freed immediately */
		reiser4_dealloc_blocks (&start, &length, 1 /* defer */, BLOCK_NOT_COUNTED);
	}
	return 0;
}


/* Audited by: green(2002.06.13) */
static reiser4_key *last_key_in_extent (const coord_t * coord, 
					reiser4_key *key)
{
	/*
	 * FIXME-VS: this actually calculates key of first byte which is the
	 * next to last byte by addressed by this extent
	 */
	item_key_by_coord (coord, key);
	set_key_offset (key, get_key_offset (key) + 
			extent_size (coord, extent_nr_units (coord)));
	return key;
}


static int cut_or_kill_units (coord_t * coord,
			      unsigned * from, unsigned * to,
			      int cut, const reiser4_key * from_key,
			      const reiser4_key * to_key,
			      reiser4_key * smallest_removed)
{
 	reiser4_extent * ext;
	reiser4_key key;
	unsigned blocksize, blocksize_bits;
	reiser4_block_nr offset;
	unsigned count;
	__u64 cut_from_to;


	count = *to - *from + 1;

	blocksize = current_blocksize;
	blocksize_bits = current_blocksize_bits;

	/*
	 * make sure that we cut something but not more than all units
	 */
	assert ("vs-220", count > 0 && count <= extent_nr_units (coord));
	/*
	 * extent item can be cut either from the beginning or down to the end
	 */
	assert ("vs-298", *from == 0 || *to == coord_last_unit_pos (coord));
	

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
			assert ("vs-612", *to == coord_last_unit_pos (coord));
			assert ("vs-613",
				keyge (to_key, extent_max_key (coord, &key_inside)));

			ext = extent_item (coord) + *from;
			first = offset + extent_size (coord, *from);
			new_width = (get_key_offset (from_key) + (blocksize - 1) - first) >> blocksize_bits;
			assert ("vs-307", new_width > 0 && new_width <= extent_get_width (ext));
			if (state_of_extent (ext) == ALLOCATED_EXTENT && !cut) {
				reiser4_block_nr start, length;
				/*
				 * truncate is in progress. Some blocks can be
				 * freed. As they do not get immediately
				 * available, set defer parameter of
				 * reiser4_dealloc_blocks to 1
				 */
				start = extent_get_start (ext) + new_width;
				length = extent_get_width (ext) - new_width;
				reiser4_dealloc_blocks (&start, &length,
							1 /* defer */, BLOCK_NOT_COUNTED);
			}
			extent_set_width (ext, new_width);
			(*from) ++;
			count --;
			if (smallest_removed) {
				set_key_offset (smallest_removed, get_key_offset (from_key));
			}
		}

		/* set @key_inside to key of last byte addrressed to extent @to */
		set_key_offset (&key_inside, (offset +
					      extent_size (coord, *to + 1) - 1));
		if (keylt (to_key, &key_inside)) {
			/* @to-th unit can not be removed completely */
 
			reiser4_block_nr new_width, old_width;

			/* cut from the middle of extent item is not allowed,
			 * make sure that head of item gets cut and cut is
			 * aligned to block boundary */
			assert ("vs-614", *from == 0);
			assert ("vs-615", keyle (from_key, &key));
			assert ("vs-616", ((get_key_offset (to_key) + 1) & 
					   (blocksize - 1)) == 0);

			ext = extent_item (coord) + *to;

			new_width = (get_key_offset (&key_inside) -
				     get_key_offset (to_key)) >> blocksize_bits;

			old_width = extent_get_width (ext);
			cut_from_to = (old_width - new_width) * blocksize;

			assert ("vs-617", new_width > 0 && new_width < old_width);

			if (state_of_extent (ext) == ALLOCATED_EXTENT && !cut) {
				reiser4_block_nr start, length;
				/*
				 * extent2tail is in progress. Some blocks can
				 * be freed. As they do not get immediately
				 * available, set defer parameter of
				 * reiser4_dealloc_blocks to 1
				 */
				start = extent_get_start (ext);
				length = old_width - new_width;
				reiser4_dealloc_blocks (&start, &length,
							1 /* defer */, BLOCK_NOT_COUNTED);
			}

			/* (old_width - new_width) blocks of this extent were
			 * free, update both extent's start and width */
			extent_set_start (ext, extent_get_start (ext) + old_width - new_width);
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

	if (*from == 0 && count != coord_last_unit_pos (coord) + 1) {
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
/* Audited by: green(2002.06.13) */
int extent_cut_units (coord_t * item, unsigned * from, unsigned * to,
		      const reiser4_key * from_key, const reiser4_key * to_key,
		      reiser4_key * smallest_removed)
{
	return cut_or_kill_units (item, from, to,
				  1, from_key, to_key, smallest_removed);
}


/*
 * plugin->u.item.b.kill_units
 */
/* Audited by: green(2002.06.13) */
int extent_kill_units (coord_t * item, unsigned * from, unsigned * to,
		       const reiser4_key * from_key, const reiser4_key * to_key,
		       reiser4_key * smallest_removed)
{
	return cut_or_kill_units (item, from, to,
				  0, from_key, to_key, smallest_removed);
}


/*
 * plugin->u.item.b.unit_key
 */
/* Audited by: green(2002.06.13) */
reiser4_key * extent_unit_key (const coord_t * coord, reiser4_key * key)
{
	assert ("vs-300", coord_is_existing_unit (coord));

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
/* Audited by: green(2002.06.13) */
static void optimize_extent (coord_t * item)
{
	unsigned i, old_num, new_num;
	reiser4_extent * ext, * prev, * start;
	reiser4_block_nr width;

	ext = start = extent_item (item);
	old_num = extent_nr_units (item);

	if (REISER4_DEBUG) {
		/* make sure that extents do not overlap */
		reiser4_block_nr next;

		next = 0;
		for (i = 0; i < old_num; i ++) {
			if (state_of_extent (&ext [i]) != ALLOCATED_EXTENT)
				continue;
			assert ("vs-775", 
				ergo (next,
				      extent_get_start (&ext [i]) >= next));
			next = extent_get_start (&ext [i]) +
				extent_get_width (&ext [i]);
		}
	}
	prev = NULL;
	new_num = 0;
	assert ("vs-765", coord_is_existing_item (item));
	assert ("vs-763", item_is_extent (item));
	item->unit_pos = 0;
	item->between = AT_UNIT;
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
		coord_t from, to;

		coord_dup (&from, item);
		from.unit_pos = new_num;
		from.between = AT_UNIT;

		coord_dup (&to, &from);
		to.unit_pos = old_num - 1;

		result = cut_node (&from, &to, 0, 0, 0, DELETE_DONT_COMPACT, 0);

		/*
		 * nothing should happen cutting
		 * FIXME: JMACD->VS: Just return the error!
		 */
		assert ("vs-456", result == 0);
	}
}


/* @coord is set to extent after which new extent(s) (@data) have to be
   inserted. Attempt to union adjacent extents is made. So, resulting item may
   be longer, shorter or of the same length as initial item */
/* Audited by: green(2002.06.13) */
static int add_extents (coord_t * coord,
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
					current_blocksize *
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
		coord_t from, to;

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
/* Audited by: green(2002.06.13) */
static reiser4_block_nr in_extent (const coord_t * coord,
				   reiser4_block_nr off)
{
        reiser4_key key;
        reiser4_block_nr cur;
        reiser4_extent * ext;

        assert ("vs-266", coord_is_existing_unit (coord));

        item_key_by_coord (coord, &key);
        cur = get_key_offset (&key) + extent_size (coord, (unsigned) coord->unit_pos);

        ext = extent_by_coord (coord);
	/*
	 * make sure that @off is within current extent
	 */
	assert ("vs-390", 
		off >= cur && 
		off < (cur + extent_get_width (ext) * 
		       current_blocksize));
        return (off - cur) >> current_blocksize_bits;
}


/* insert extent item (containing one unallocated extent of width 1) to place
   set by @coord */
/* Audited by: green(2002.06.13) */
static int insert_first_block (coord_t * coord, lock_handle * lh, jnode * j,
			       const reiser4_key * key)
{
	int result;
	reiser4_extent ext;
	reiser4_item_data unit;
	reiser4_key first_key;


	/* make sure that we really write to first block */
	assert ("vs-240",
		(get_key_offset (key) & ~(current_blocksize - 1)) == 0);
	/* extent insertion starts at leaf level */
	assert ("vs-719", znode_get_level (coord->node) == LEAF_LEVEL);
	
	first_key = *key;
	set_key_offset (&first_key, 0ull);

	set_extent (&ext, UNALLOCATED_EXTENT, 1ull);
	result = insert_extent_by_coord (coord, init_new_extent (&unit, &ext, 1),
					 &first_key, lh);
	if (result) {
		return result;
	}

	jnode_set_mapped (j);
	jnode_set_created (j);

	reiser4_stat_file_add (write_repeats);
	return -EAGAIN;
}


/* @coord is set to the end of extent item. Append it with pointer to one
   block - either by expanding last unallocated extent or by appending a new
   one of width 1 */
/* Audited by: green(2002.06.13) */
static int append_one_block (coord_t * coord,
			     lock_handle *lh, jnode * j)
{
	int result;
	reiser4_extent * ext, new_ext;
	reiser4_item_data unit;
	reiser4_key key;

	assert ("vs-228",
		(coord->unit_pos == coord_last_unit_pos (coord) &&
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

	jnode_set_mapped (j);
	jnode_set_created (j);

	coord->unit_pos = coord_last_unit_pos (coord);
	coord->between = AFTER_UNIT;
	return 0;
}


/* @coord is set to hole extent, replace it with unallocated extent of length
   1 and correct amount of hole extents around it */
/* Audited by: green(2002.06.13) */
static int plug_hole (coord_t * coord, lock_handle * lh,
		      reiser4_block_nr off)
{
	reiser4_extent * ext,
		new_exts [2];
	reiser4_block_nr width, pos_in_unit;
	reiser4_key key;
	reiser4_item_data item;
	int count;

	assert ("vs-234", coord_is_existing_unit (coord));

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
/* Audited by: green(2002.06.13) */
static reiser4_block_nr blocknr_by_coord_in_extent (coord_t * coord,
						    reiser4_block_nr off)
{
	assert ("vs-12", coord_is_existing_unit (coord));
	assert ("vs-264", state_of_extent (extent_by_coord (coord)) ==
		ALLOCATED_EXTENT);

	return extent_get_start (extent_by_coord (coord)) + in_extent (coord, off);
}

/**
 * Return the inode and its key.
 */
/* Audited by: green(2002.06.13) */
void extent_get_inode_and_key (const coord_t *item, struct inode **inode, reiser4_key *key)
{
	unsigned long ino;

	item_key_by_coord (item, key);

	set_key_type (key, KEY_SD_MINOR);
	set_key_offset (key, 0ull);

	ino = get_key_objectid (key);

	/* Note: This cannot call the usualy reiser4_iget() interface because that _may_
	 * cause a call to read_inode(), which will likely deadlock at this point.  The
	 * call to find_get_inode only gets the inode if it is found in cache. */
	/* Bad: (*inode) = reiser4_iget (reiser4_get_current_sb (), key); */
	(*inode) = find_get_inode (reiser4_get_current_sb (), ino, reiser4_inode_find_actor, key);
}

/**
 * Return the inode.
 */
/* Audited by: green(2002.06.13) */
void extent_get_inode (const coord_t *item, struct inode **inode)
{
	reiser4_key key;

	extent_get_inode_and_key (item, inode, & key);
}

/**
 * Return the reiser_extent and position within that extent.
 */
/* Audited by: green(2002.06.13) */
static reiser4_extent* extent_utmost_ext ( const coord_t *coord, sideof side,
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
		ext = extent_item (coord) + coord_last_unit_pos (coord);
		*pos_in_unit = extent_get_width (ext) - 1;
	}

	return ext;
}

/**
 * Return the child.
 */
/* Audited by: green(2002.06.13) */
int extent_utmost_child ( const coord_t *coord, sideof side, jnode **childp )
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

		extent_get_inode_and_key (coord, & inode, & key);

		if ( !inode )  {
			assert ("jmacd-1231", state_of_extent (ext) != UNALLOCATED_EXTENT);
			*childp = NULL;
			return 0;
		}

		offset = get_key_offset (&key) + pos_in_unit * current_blocksize;

		assert ("vs-544", offset >> PAGE_CACHE_SHIFT < ~0ul);

		pg = find_get_page (inode->i_mapping, (unsigned long)(offset >> PAGE_CACHE_SHIFT));

		if (pg == NULL) {
			*childp = NULL;
			iput (inode);
			return 0;
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
/* Audited by: green(2002.06.13) */
int extent_utmost_child_dirty ( const coord_t *coord, sideof side, int *is_dirty )
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
		*is_dirty = jnode_check_dirty (child);
		jput (child);
	}
	return 0;
}

/**
 * Return the child's block, if allocated.
 */
/* Audited by: green(2002.06.13) */
int extent_utmost_child_real_block ( const coord_t *coord, sideof side, reiser4_block_nr *block )
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
/* Audited by: green(2002.06.13) */
reiser4_key * extent_max_key (const coord_t * coord, 
			      reiser4_key * key)
{
	last_key_in_extent (coord, key);
	assert ("vs-610", get_key_offset (key) &&
		(get_key_offset (key) & (current_blocksize - 1)) == 0);
	set_key_offset (key, get_key_offset (key) - 1);
	return key;
}


/* plugin->u.item.b.key_in_item
 * return true if unit pointed by @coord addresses byte of file @key refers
 * to */
int extent_key_in_item (coord_t * coord, const reiser4_key * key)
{
	reiser4_key item_key;
	unsigned i, nr_units;
	__u64 offset;
	reiser4_extent * ext;


	assert ("vs-771", coord_is_existing_item (coord));

	if (keygt (key, extent_max_key (coord, &item_key)))
		/* key > max key of item */
		return 0;

	/* key of first byte pointed by item */
	item_key_by_coord (coord, &item_key);
	if (keylt (key, &item_key))
		/* key < min key of item */
		return 0;

	/* maybe coord is set already to needed unit */
	if (coord_is_existing_unit (coord) && extent_key_in_unit (coord, key))
		return 1;

	/* calculate position of unit containing key */
	ext = extent_item (coord);
	nr_units = extent_nr_units (coord);
	offset = get_key_offset (&item_key);
	for (i = 0; i < nr_units; i ++, ext ++) {
		offset += (current_blocksize * extent_get_width (ext));
		if (offset > get_key_offset (key)) {
			coord->unit_pos = i;
			coord->between = AT_UNIT;
			return 1;
		}
	}
	
	impossible ("vs-772", "key must be in item");
	return 0;
}


/* plugin->u.item.b.key_in_coord
 * return true if unit pointed by @coord addresses byte of file @key refers
 * to */
int extent_key_in_unit( const coord_t *coord, const reiser4_key *key )
{
	reiser4_extent * ext;
	reiser4_key ext_key;

	assert ("vs-770", coord_is_existing_unit (coord));

	/* key of first byte pointed by unit */
	unit_key_by_coord (coord, &ext_key);
	if (keylt (key, &ext_key))
		return 0;
	
	/* key of a byte next to the last byte pointed by unit */
	ext = extent_by_coord (coord);
	set_key_offset (&ext_key,
			get_key_offset (&ext_key) +
			extent_get_width (ext) * current_blocksize);
	return keylt (key, &ext_key);
}


/* pointer to block for @bh exists in extent item and it is addressed by
   @coord. If it is hole - make unallocated extent for it. */
/* Audited by: green(2002.06.13) */
static int overwrite_one_block (coord_t * coord, lock_handle * lh,
				jnode * j, reiser4_block_nr off)
{
	reiser4_extent * ext;
	int result;


	ext = extent_by_coord (coord);

	switch (state_of_extent(ext)) {
	case ALLOCATED_EXTENT:
		j->blocknr = blocknr_by_coord_in_extent (coord, off);
		jnode_set_mapped (j);
		break;

	case UNALLOCATED_EXTENT:
		jnode_set_mapped (j);
		break;
		
	case HOLE_EXTENT:
		result = plug_hole (coord, lh, off);
		if (result)
			return result;
		jnode_set_mapped (j);
		jnode_set_created (j);
		break;

	default:
		impossible ("vs-238", "extent of unknown type found");
		return -EIO;
	}

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
/* Audited by: green(2002.06.13) */
static int add_hole (coord_t * coord, lock_handle * lh,
		     const reiser4_key * key, /* key of position in a file for write */
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
		reiser4_key hole_key;

		assert ("vs-707", znode_get_level (coord->node) == LEAF_LEVEL);

		/* @coord must be set for inserting of new item */
		assert ("vs-711", (coord->between == AFTER_ITEM ||
				   coord->between == BEFORE_ITEM ||
				   (coord->between == EMPTY_NODE &&
				    node_is_empty (coord->node))));
		
		hole_key = *key;
		set_key_offset (&hole_key, 0ull);

		hole_off = 0;
		hole_width = (get_key_offset (key)) >> current_blocksize_bits;
		assert ("vs-710", hole_width > 0);

		/* compose body of hole extent */
		set_extent (&new_ext, HOLE_EXTENT, hole_width);

		/* prepare item data for insertion */
		init_new_extent (&item, &new_ext, 1);
		return insert_extent_by_coord (coord, &item, &hole_key, lh);
	}

	/* last item of file must be appended with hole */
	assert ("vs-708", znode_get_level (coord->node) == TWIG_LEVEL);
	assert ("vs-714", item_id_by_coord (coord) == EXTENT_POINTER_ID);
	/* @coord points to last extent of the item and to its last block */
	assert ("vs-29",
		coord->unit_pos == coord_last_unit_pos (coord) &&
		coord->between == AFTER_UNIT);
	
	hole_off = get_key_offset (last_key_in_extent(coord, &last_key));
	hole_width = (get_key_offset (key) - hole_off) >> current_blocksize_bits;
	assert ("vs-709", hole_width > 0);
	
	/* get last extent in the item */
	ext = extent_by_coord (coord);
	if (state_of_extent (ext) == HOLE_EXTENT) {
		/* last extent of a file is hole extent. Widen that extent by
		 * @hole_width blocks */
		set_extent (ext, HOLE_EXTENT,
			    extent_get_width (ext) + hole_width);
		return 0;
	}

	/* append item with hole extent unit */
	assert ("vs-713", (state_of_extent (ext) == ALLOCATED_EXTENT ||
			   state_of_extent (ext) == UNALLOCATED_EXTENT));
	
	/* compose body of hole extent */
	set_extent (&new_ext, HOLE_EXTENT, hole_width);
		
	/* prepare item data for insertion */
	init_new_extent (&item, &new_ext, 1);
	result = add_extents (coord, lh,
			      last_key_in_extent (coord, &last_key),
			      &item);
	if (!result) {
		/* this should re-set @coord such that no re-search will be
		 * required to continue writing to a file */
		coord->unit_pos = coord_last_unit_pos (coord);
		coord->between = AFTER_UNIT;
	}
	return result;
}


/*
 * does extent @coord contain @key
 */
/* Audited by: green(2002.06.13) */
static int key_in_extent (const coord_t * coord, const reiser4_key * key)
{
	reiser4_extent * ext;
	reiser4_key ext_key;

	unit_key_by_coord (coord, &ext_key);
	ext = extent_by_coord (coord);

	return get_key_offset (key) >= get_key_offset (&ext_key) &&
		get_key_offset (key) < get_key_offset (&ext_key) +
		extent_get_width (ext) * current_blocksize;
}


/*
 * does item @coord contain @key
 */
/* Audited by: green(2002.06.13) */
static int key_in_item (coord_t * coord, reiser4_key * key)
{
	reiser4_key item_key;


	return ((keyle (item_key_by_coord (coord, &item_key), key)) &&
		(keylt (key, last_key_in_extent (coord, &item_key))));
}


/* this is todo for regular reiser4 files which are made of extents
   only. Extents represent every byte of file body. One node may not have more
   than one extent item of one file */
/* Audited by: green(2002.06.13) */
static extent_write_todo extent_what_todo (coord_t * coord,
					   const reiser4_key * key,
					   int to_block)
{
	reiser4_key coord_key;
	coord_t left, right;
	__u64 fbb_offset; /* offset of First Byte of Block */


	if (ZF_ISSET (coord->node, ZNODE_HEARD_BANSHEE))
		/* znode is not in tree already */
		return EXTENT_RESEARCH;

	/* FIXME-VS: do not check value returned by coord_set_properly, because
	 * it is possible that znode does not contain key but still */
	coord_set_properly (key, coord);

	/* offset of First Byte of Block key->offset falls to */
	fbb_offset = get_key_offset (key) & ~(current_blocksize - 1);

	if (!znode_contains_key_lock (coord->node, key)) {
		assert ("vs-602", !node_is_empty (coord->node));
		coord_init_first_unit (&left, coord->node);
		item_key_by_coord (&left, &coord_key);

		if (keylt (key, &coord_key)) {
			/* if left neighbor of coord->node is unformatted node
			 * of another file we can get here even if coord->node
			 * does not contain key we are looking for */
			if (znode_get_level (coord->node) == LEAF_LEVEL &&
			    coord_is_before_leftmost (coord)) {
				if (fbb_offset == 0)
					return EXTENT_FIRST_BLOCK;
				else {
					if (get_key_locality (key) == get_key_locality (&coord_key) &&
					    get_key_objectid (key) == get_key_objectid (&coord_key)) {
						/*
						 * FIXME-VS: tail2extent
						 */
						assert ("vs-682", item_id_by_coord (coord) == TAIL_ID);
						return EXTENT_RESEARCH;
					}

					return EXTENT_CREATE_HOLE;
				}
			}
			return EXTENT_RESEARCH;
		} else {
			/* key we were looking for is greater that right
			 * delimiting key */
			return EXTENT_RESEARCH;			
		}
	}

	if (coord_is_existing_unit (coord))
		return key_in_extent (coord, key) ? EXTENT_OVERWRITE_BLOCK : EXTENT_RESEARCH;

	if (coord_is_between_items (coord)) {
		if (node_is_empty (coord->node) ||
		    !coord_is_existing_item (coord)) {
			assert ("vs-722", znode_get_level (coord->node) == LEAF_LEVEL);
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


static int extent_get_create_block (coord_t * coord, lock_handle * lh,
				    const reiser4_key * key, jnode * j, int to_block)
{
	int result;
	extent_write_todo todo;


	todo = extent_what_todo (coord, key, to_block);
	switch (todo) {
	case EXTENT_CREATE_HOLE:
	case EXTENT_APPEND_HOLE:
		result = add_hole (coord, lh, key, todo);
		if (!result) {
			reiser4_stat_file_add (write_repeats);
			result = -EAGAIN;
		}
		return result;

	case EXTENT_FIRST_BLOCK:
		/* create first item of the file */
		reiser4_stat_file_add (pointers);
		return insert_first_block (coord, lh, j, key);
		
	case EXTENT_APPEND_BLOCK:
		reiser4_stat_file_add (pointers);
		return append_one_block (coord, lh, j);

	case EXTENT_OVERWRITE_BLOCK:
		/* there is found extent (possibly hole one) */
		return overwrite_one_block (coord, lh, j, get_key_offset (key));
		
	case EXTENT_CANT_CONTINUE:
		/* unexpected structure of file found */
		warning ("jmacd-81263", "extent_get_create_block: can't continue");
		return  -EIO;
				
	case EXTENT_RESEARCH:
		/* @coord is not set to a place in a file we have to write to,
		   so, coord_by_key must be called to find that place */
		reiser4_stat_file_add (write_repeats);
		return -EAGAIN;

	default:
		impossible ("vs-334", "unexpected case "
			    "in what_todo");
	}
	return -EIO;
}


/* return true if writing at offset @file_off @count bytes to file should block
 * be read before writing into it */
static int have_to_read_block (struct inode * inode, jnode * j,
		loff_t file_off, int count)
{
	/* file_off fits into block jnode @j refers to */
	assert ("vs-705", j->pg);
	assert ("vs-706", current_blocksize == (unsigned)PAGE_CACHE_SIZE);
	assert ("vs-704", ((file_off & ~(current_blocksize - 1)) ==
			   ((loff_t)j->pg->index << PAGE_CACHE_SHIFT)));


	if (j->blocknr == 0)
		/* jnode of unallocated unformatted node */
		return 0;
	if (JF_ISSET (j, ZNODE_LOADED))
		/* buffer uptodate ? */
		return 0;
	if (count == (int)current_blocksize)
		/* all content of block will be overwritten */
		return 0;
	if (file_off & (current_blocksize - 1)) {
		/* start of block is not overwritten */
		assert ("vs-699",
			inode->i_size > (file_off & ~(current_blocksize - 1)));
		return 1;
	}
	if (file_off + count >= inode->i_size) {
		/* all old content of file is overwritten */
		return 0;
	}
	return 1;
}


/* make sure that all blocks of the page which are to be modified have pointers
 * from extent item. Copy user data into page, mark jnodes of modifed blocks
 * dirty */
static int write_flow_to_page (coord_t * coord, lock_handle * lh, flow_t * f,
			       struct page * page)
{
	int result;
	loff_t file_off;
	int page_off,    /* offset within a page where write starts */
		block_off,
		to_block;
	int left;
	unsigned written; /* number of bytes written to page */
	int block, first; /* blocks in a page to be modified */
	char * b_data;
	jnode * j;


	result = 0;
	kmap (page);

	/* write position */
	file_off = get_key_offset (&f->key);

	/* offset within the page where we start writing from */
	page_off = (int)(file_off & (PAGE_CACHE_SIZE - 1));

	/* amount of bytes which are to be written to this page */
	left = PAGE_CACHE_SIZE - page_off;
	if (left > (int)f->length)
		left = f->length;

	first = page_off / current_blocksize;
	written = 0;
	b_data = page_address (page);
	block_off = page_off & (current_blocksize - 1);

	for (block = first, j = nth_jnode (page, first);
	     left > 0;
	     block ++, j = next_jnode (j)) {
		/* number if bytes to written into this block */
		to_block = current_blocksize - block_off;
		if (to_block > left)
			to_block = left;
		assert ("vs-698", j);
		if (!jnode_mapped (j)) {
			assert ("vs-734", znode_is_loaded (coord->node));
			result = extent_get_create_block (coord, lh, &f->key,
							  j, to_block);
			if (result) {
				if (!(result == -EAGAIN && page->index == 0))
					break;
				assert ("vs-777", result == -EAGAIN && page->index == 0);
			}
		}
		assert ("vs-702", jnode_mapped (j));
		if (f->data) {
			if (have_to_read_block (page->mapping->host, j,
						file_off + written, to_block)) {
				page_io (page, READ, GFP_NOIO);
				wait_on_page_locked (page);
				if (!PageUptodate (page)) {
					warning ("jmacd-61238", "write_flow_to_page: page not up to date");
					result = -EIO;
					break;
				}
			}
			
			if (jnode_created (j)) {
				int padd;
				
				/* new block added. Zero block content which is
				 * not covered by write */
				if (block_off)
					/* zero beginning of block */
					memset (b_data, 0, (unsigned)block_off);
				/* zero end of block */
				padd = current_blocksize - block_off - to_block;
				assert ("vs-721", padd >= 0);
				memset (b_data + block_off + to_block, 0, (unsigned)padd);
				/* FIXME: JMACD->VS: Are you sure this doesn't cause excess zero-ing? */
				/*jnode_clear_new (j);*/
			}
			/* copy data into page */
			if (unlikely (__copy_from_user (b_data + block_off,
							f->data + written,
						(unsigned)to_block))) {
				/* FIXME-VS: undo might be necessary */
				return -EFAULT;
			}
		}
		jnode_set_loaded (j);
		jnode_set_dirty (j);
		written += to_block;
		left -= to_block;
		block_off = 0;
		b_data += current_blocksize;
		if (result)
			/* after inserting first item of a file - research is
			 * required */
			break;
	}

	if (first == 0 && block == (int)(PAGE_CACHE_SIZE / current_blocksize)) {
		/* all blocks of page were modifed */
		SetPageUptodate (page);
	}

	move_flow_forward (f, written);
	kunmap (page);
	return result;
}


/*
 * plugin->u.item.s.file.write
 * wouldn't it be possible to modify it such that appending of a lot of bytes
 * is done in one modification of extent?
 */
/* Audited by: green(2002.06.13) */
int extent_write (struct inode * inode, coord_t * coord,
		  lock_handle * lh, flow_t * f, struct page * page)
{
	int result;

	result = 0;
	while (f->length) {
		if (f->data) {
			assert ("vs-586", !page);
			assert ("vs-700", f->user == 1);

			page = grab_cache_page (
				inode->i_mapping,
				(unsigned long)(get_key_offset (&f->key) >>
						PAGE_CACHE_SHIFT));
			if (!page) {
				return -ENOMEM;
			}
		} else {
			/* tail2extent is in progress. Page contains data
			 * already */
		}

		assert ("vs-701", PageLocked (page));

		/* Capture the page. Jnodes get created for that page */
		result = txn_try_capture_page (page, ZNODE_WRITE_LOCK, 0);
		if (!result) {
			result = write_flow_to_page (coord, lh, f, page);
		}
		if (f->data) {
			unlock_page (page);
			page_cache_release (page);
			page = 0;
		}
		if (result)
			break;
	}
	return result;
}


/*
 * FIXME-VS: comment, cleanups are needed here
 */
int extent_readpage (void * vp, struct page * page)
{
	struct inode * inode;
	coord_t * coord;
	reiser4_extent * ext;
	reiser4_key page_key, unit_key;
	__u64 pos_in_extent;
	reiser4_block_nr block;
	jnode * j;


	assert ("vs-761", page && page->mapping && page->mapping->host);
	inode = page->mapping->host;

	coord = (coord_t *)vp;
	/* no jnode yet */
	assert ("vs-757", page->private == 0);
	assert ("vs-758", item_is_extent (coord));
	assert ("vs-759", coord_is_existing_unit (coord));
	ext = extent_by_coord (coord);
	assert ("vs-760", state_of_extent (ext) == ALLOCATED_EXTENT);

	/* calculate key of page */
	inode_file_plugin (inode)->key_by_inode (inode, (loff_t)page->index << PAGE_CACHE_SHIFT,
						 &page_key);
	/* make sure that extent is found properly */
	assert ("vs-762", extent_key_in_unit (coord, &page_key));
	unit_key_by_coord (coord, &unit_key);
	pos_in_extent = (get_key_offset (&page_key) - get_key_offset (&unit_key)) >>
		current_blocksize_bits;

	j = jnode_of_page (page);
	if (!j)
		return -ENOMEM;

	block = extent_get_start (ext) + pos_in_extent;
	jnode_set_block (j, &block);
	page_io (page, READ, GFP_NOIO);
	jput (j);
	return 0;
}


/*
 * Implements plugin->u.item.s.file.read operation for extent items.
 */
int extent_read (struct inode * inode, coord_t * coord,
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
	count = 0;

	/* this will return page if it exists and is uptodate, otherwise it
	 * will allocate page and call extent_readpage to fill it */
	page = read_cache_page (inode->i_mapping, page_nr, extent_readpage, coord);

	if (IS_ERR (page)) {
		return PTR_ERR (page);
	}

	/*
	 * FIXME-VS: there should be jnode attached to page
	 */
	assert ("vs-756", page->private);
	wait_on_page_locked (page);
	if (!PageUptodate (page)) {
		page_detach_jnode (page);
		page_cache_release (page);
		warning ("jmacd-97178", "extent_read: page is not up to date");
		return -EIO;
	}

	/* Capture the page. */
	result = txn_try_capture_page (page, ZNODE_READ_LOCK, 0);
	if (result != 0) {
		page_detach_jnode (page);
		page_cache_release (page);
		return result;
	}
	/* position within the page to read from */
	page_off = (get_key_offset (&f->key) & ~PAGE_CACHE_MASK);

	/* number of bytes which can be read from the page */
	if (page_nr == (inode->i_size >> PAGE_CACHE_SHIFT)) {
		/* we read last page of a file. Calculate size of file tail */
		count = inode->i_size & ~PAGE_CACHE_MASK;
	} else {
		count = PAGE_CACHE_SIZE;
	}
	assert ("vs-388", count > page_off);
	count -= page_off;
	if (count > f->length)
		count = f->length;

	/* AUDIT: We must page-in/prepare user area first to avoid deadlocks */
	kaddr = kmap (page);
	assert ("vs-572", f->user == 1);
	assert( "green-6", lock_counters() -> spin_locked == 0 );
	result = __copy_to_user (f->data, kaddr + page_off, count);
	kunmap (page);

	page_cache_release (page);
	if (result) {
		/* AUDIT: that should return -EFAULT */
		return result;
	}

	move_flow_forward (f, count);
	return 0;
	
}


struct ra_page_range {
	struct list_head next; /* list of page ranges */
	struct list_head pages; /* list of newly created, contiguous, belonging
				 * to one extent pages. Number of pages in this
				 * list is not mmore than max number of pages
				 * in bio */
	int extent;
	int nr_pages;
};

/* return true if number of pages in a range is maximal possible number of
 * pages in one bio */
static int range_is_full (struct ra_page_range * range)
{
	return ((unsigned)range->nr_pages == (BIO_MAX_SIZE / PAGE_CACHE_SIZE)) ? 1 : 0;
}


/* Implements plugin->u.item.s.file.readahead operation for extent items.
 *
 * scan extent item @coord starting from position addressing @start_page and
 * create list of page ranges. Page range is a list of contiguous pages
 * addressed by the same extent. Page gets into page range only if it is not
 * yet attached to address space (this is checked by radix_tree_lookup). When
 * page is found in address space - it is skipped and all the next pages get
 * into different page range (even pages addressed by the same
 * extent). Scanning stops either at the end of item or if number of pages for
 * readahead @intrafile_readahead_amount is over.
 *
 * For every page range of the list: for every page from a range: add page into
 * page cache, create bio and submit i/o. Note, that page range can be split
 * into several bio-s as well when adding page into address space fails
 * (because page is there already)
 * 
 */
int extent_page_cache_readahead (struct file * file, coord_t * coord,
				 lock_handle * lh UNUSED_ARG,
				 unsigned long start_page,
				 unsigned long intrafile_readahead_amount)
{
	struct address_space * mapping;
	reiser4_extent * ext;
	unsigned i, j, nr_units;
	__u64 pos_in_unit;
	LIST_HEAD (range_list);
	struct ra_page_range * range;
	struct page * page;
	unsigned long left;
	__u64 pages;


	assert ("vs-789", file && file->f_dentry && file->f_dentry->d_inode &&
		file->f_dentry->d_inode->i_mapping);
	mapping = file->f_dentry->d_inode->i_mapping;

	assert ("vs-779", current_blocksize == PAGE_CACHE_SIZE);
	assert ("vs-782", intrafile_readahead_amount);
	/* make sure that unit, @coord is set to, addresses @start page */
	assert ("vs-786", in_extent (coord, (__u64)start_page << PAGE_CACHE_SHIFT));

	
	nr_units = extent_nr_units (coord);
	/* position in item matching to @start_page */
	ext = extent_by_coord (coord);
	pos_in_unit = in_extent (coord, (__u64)start_page << PAGE_CACHE_SHIFT);

	/* number of pages to readahead */
	left = intrafile_readahead_amount;

	/* while not all pages are allocated and item is not over */
	for (i = 0; left && (coord->unit_pos + i < nr_units); i ++) {
		/* how many pages from current extent to read ahead */
		pages = extent_get_width (&ext [i]) - (i == 0 ? pos_in_unit : 0);
		if (pages > left)
			pages = left;

		if (state_of_extent (&ext [i]) == UNALLOCATED_EXTENT) {
			/* these pages are in memory */
			start_page += pages;
			left -= pages;
			continue;
		}

		range = 0;
		for (j = 0; j < pages; j ++) {
			page = radix_tree_lookup (&mapping->page_tree,
						  start_page + j);
			if (page) {
				/* page is already in address space */
				if (range) {
					/* contiguousness is broken */
					range = 0;
				}
				continue;
			}
			if (range && range_is_full (range)) {
				/* range is too big for one bio */
				range = 0;
			}
			if (!range) {
				/* create new range of contiguous pages
				 * belonging to one extent */
				/*
				 * FIXME-VS: maybe this should be after
				 * read_unlock (&mapping->page_lock)
				 */
				range = kmalloc (sizeof (struct ra_page_range), GFP_KERNEL);
				if (!range)
					break;
				memset (range, 0, sizeof (struct ra_page_range));
				range->nr_pages = 0;
				range->extent = coord->unit_pos + i;
				INIT_LIST_HEAD (&range->pages);
				list_add (&range->next, &range_list);
			}

			read_unlock (&mapping->page_lock);
			page = page_cache_alloc (mapping);
			read_lock (&mapping->page_lock);
			if (!page)
				break;
			page->index = start_page;
			list_add (&page->list, &range->pages);
			range->nr_pages ++;
		}

		left -= pages;
		start_page += j;
	}

	/* submit bio for all ranges */
	{
		struct list_head * cur, * tmp;
		struct bio * bio = NULL;
		struct bio_vec * bvec;

		list_for_each_safe (cur, tmp, &range_list) {
			coord_t unit;
			extent_state state;
			struct list_head * cur2, * tmp2;


			range = list_entry (cur, struct ra_page_range, pages);
			
			/* get extent unit which points to all pages of this range */
			coord_dup (&unit, coord);
			coord->unit_pos = range->extent;
			coord->between = AT_UNIT;
			state = state_of_extent (extent_by_coord (&unit));
			assert ("vs-787", state != UNALLOCATED_EXTENT);

			list_del (&range->next);
			
			list_for_each_safe (cur2, tmp2, &range->pages) {

				page = list_entry (cur2, struct page, list);
				list_del (&page->list);

				if (add_to_page_cache_unique (page, mapping, page->index)) {
					/* page is in address space already,
					 * skip it, and submit bio we have so
					 * far */
					page_cache_release (page);
					if (bio) {
						bio->bi_vcnt = bio->bi_idx;
						submit_bio (READ, bio);
						bio = 0;
						range->nr_pages -= bio->bi_vcnt;
					}
					range->nr_pages --;
					continue;
				}
				
				if (state == HOLE_EXTENT) {
					memset (kmap (page), 0, PAGE_CACHE_SIZE);
					flush_dcache_page (page);
					kunmap (page);
					SetPageUptodate (page);
					unlock_page (page);
					page_cache_release (page);
					range->nr_pages --;
				}
				if (!bio) {
					coord_t unit;
					
					bio = bio_alloc (GFP_KERNEL, range->nr_pages);
					if (!bio) {
						page_cache_release (page);
						range->nr_pages --;
						continue;
					}
					bio->bi_bdev = mapping->host->i_bdev;
					bio->bi_vcnt = range->nr_pages;
					bio->bi_idx = 0;
					bio->bi_size = 0;
					bio->bi_sector = extent_get_start (extent_by_coord (&unit)) +
						in_extent (&unit, (__u64)page->index << PAGE_CACHE_SHIFT);
					bio->bi_io_vec[0].bv_page = NULL;
				}
				bvec = &bio->bi_io_vec [bio->bi_idx];
				bvec->bv_page = page;
				bvec->bv_len = current_blocksize;
				bvec->bv_offset = 0;
				bio->bi_size += bvec->bv_len;
				bio->bi_idx ++;
				page_cache_release (page);
			}
			bio->bi_vcnt = bio->bi_idx;
			submit_bio (READ, bio);
			bio = NULL;
			range->nr_pages -= bio->bi_vcnt;
			assert ("vs-783", list_empty (&range->pages));
			assert ("vs-788", !range->nr_pages);
			kfree (range);
		}
		assert ("vs-785", list_empty (&range_list));
	}
	/* return number of pages readahead was performed for. It is not
	 * necessary pages i/o was submitted for. Pages from readahead range
	 * which were in cache are skipped here but they are included into
	 * returned value */
	return intrafile_readahead_amount - left;
}

/*
 * ask block allocator for some blocks
 */
/* Audited by: green(2002.06.13) */
static int extent_allocate_blocks (reiser4_blocknr_hint *preceder,
				   reiser4_block_nr wanted_count,
				   reiser4_block_nr * first_allocated,
				   reiser4_block_nr * allocated)
{
	int result;

	*allocated = wanted_count;
	result = reiser4_alloc_blocks (preceder, first_allocated, allocated);
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
 * unallocated extent of file with @objectid corresponding to @offset was
 * replaced allocated extent [first, count]. Look for corresponding buffers in
 * the page cache and map them properly
 * FIXME-VS: this needs changes if blocksize != pagesize is needed
 */
/* Audited by: green(2002.06.13) */
static int assign_jnode_blocknrs (reiser4_key * key,
				  reiser4_block_nr first, 
				  /* FIXME-VS: get better type for number of
				   * blocks */
				  reiser4_block_nr count,
				  flush_position *flush_pos)
{
	loff_t offset;
	struct inode * inode;
	struct page * page;
	unsigned long blocksize;
	unsigned long ind;
	reiser4_key sd_key;
	jnode * j;
	int i, ret;


	blocksize = current_blocksize;
	assert ("vs-749", blocksize == PAGE_CACHE_SIZE);


	/* find inode of file for which extents were allocated */
	sd_key = *key;
	set_key_type (&sd_key, KEY_SD_MINOR);
	set_key_offset (&sd_key, 0ull);
	inode = reiser4_iget (reiser4_get_current_sb (), &sd_key);
	assert ("vs-348", inode);

	/* offset of first byte addressed by block for which blocknr @first is
	 * allocated */
	offset = get_key_offset (key);
	assert ("vs-750", ((offset & (blocksize - 1)) == 0));

	for (i = 0; i < (int)count; i ++, first ++) {
		ind = offset >> PAGE_CACHE_SHIFT;

		page = find_lock_page (inode->i_mapping, ind);
		/* 
		 * FIXME:NIKITA->VS saw this failing with pg == NULL, after
		 * mkfs, mount, cp /etc/passwd, umount.
		 */
		assert ("vs-349", page != NULL);
		assert ("vs-350", page->private != 0);

		j = jnode_of_page (page);
		if (! j) {
			return -ENOMEM;
		}
		jnode_set_block (j, &first);

		/* FIXME: JMACD, VS?  We need to figure out relocation. */
		JF_SET (j, ZNODE_RELOC);

		/* Submit I/O and set the jnode clean. */
		ret = flush_enqueue_jnode_page_locked (j, flush_pos, page);
		/* page_detach_jnode (page); */
		page_cache_release (page);
		jput (j);
		if (ret) {
			return ret;
		}

		offset += blocksize;
	}
 	iput (inode);
		
	return 0;
}


/*
 * return 1 if @extent unit needs allocation, 0 - otherwise. Try to update
 * preceder in parent-first order for next block which will be allocated
 * FIXME-VS: this only returns 1 for unallocated extents. It may be modified to
 * return 1 for allocated extents all unformatted nodes of which are in
 * memory. But that would require changes to allocate_extent_item as well
 */
/* Audited by: green(2002.06.13) */
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
/* Audited by: green(2002.06.13) */
static int must_insert (coord_t * coord, reiser4_key * key)
{
	reiser4_key last;

	if (item_id_by_coord (coord) == EXTENT_POINTER_ID &&
	    keyeq (last_key_in_extent (coord, &last), key))
		return 0;
	return 1;
}


/*
 * helper for allocate_and_copy_extent
 * append last item in the @node with @data if @data and last item are
 * mergeable, otherwise insert @data after last item in @node. Have carry to
 * put new data in available space only. This is because we are in squeezing.
 *
 * FIXME-VS: me might want to try to union last extent in item @left and @data
 */
/* Audited by: green(2002.06.13) */
static int put_unit_to_end (znode * node, reiser4_key * key,
			    reiser4_item_data * data)
{
	int result;
	coord_t coord;
	cop_insert_flag flags;

	/* set coord after last unit in an item */
	coord_init_last_unit (&coord, node);
	coord.between = AFTER_UNIT;

	flags = COPI_DONT_SHIFT_LEFT | COPI_DONT_SHIFT_RIGHT | COPI_DONT_ALLOCATE;
	if (must_insert (&coord, key)) {
		result = insert_by_coord (&coord, data, key, 0/*lh*/, 0/*ra*/,
					  0/*ira*/, flags);
					  
	} else {
		result = resize_item (&coord, data, key, 0/*lh*/, flags);
	}

	assert ("vs-438", result == 0 || result == -ENOSPC);
	return result;
}


/*
 * try to expand last extent in node @left by @allocated. Return 1 in case of
 * success and 0 otherwise
 */
/* Audited by: green(2002.06.13) */
static int try_to_glue (znode * left, coord_t * right,
			reiser4_block_nr first_allocated,
			reiser4_block_nr allocated)
{
	coord_t last;	
	reiser4_key item_key, last_key;
	reiser4_extent * ext;

	assert ("vs-463", !node_is_empty (left));

	if (right->unit_pos != 0)
		return 0;

	coord_init_last_unit (&last, left);

	ext = extent_by_coord (&last);

	if (item_plugin_by_coord (&last) != item_plugin_by_coord (right))
		return 0;

	if (state_of_extent (ext) &&
	    keyeq (last_key_in_extent (&last, &last_key), &item_key) &&
	    (extent_get_start (ext) + extent_get_width (ext) ==
	     first_allocated)) {
		/*
		 * @first_allocated is adjacent to last block of last extent in
		 * @left
		 */
		extent_set_width (ext, extent_get_width (ext) + allocated);
		return 1;
	}
	return 0;
}


/*
 * @right is extent item. @left is left neighbor of @right->node. Copy item
 * @right to @left unit by unit. Units which do not require allocation are
 * copied as they are. Units requiring allocation are copied after destinating
 * start block number and extent size to them. Those units may get inflated due
 * to impossibility to allocate desired number of contiguous free blocks
 * @flush_pos - needs comment
 */
/* Audited by: green(2002.06.13) */
int allocate_and_copy_extent (znode * left, coord_t * right,
			      flush_position *flush_pos,
			      /*
			       * biggest key which was moved, it is maintained
			       * while shifting is in progress and is used to
			       * cut right node at the end
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
	ON_DEBUG (int allocate_times);

	blocksize = current_blocksize;

	optimize_extent (right);

	/* make sure that we start from first unit of the item */
	assert ("vs-419", item_id_by_coord (right) == EXTENT_POINTER_ID);
	assert ("vs-418", right->unit_pos == 0);
	assert ("vs-794", right->between == AT_UNIT);

	result = SQUEEZE_CONTINUE;
	item_key_by_coord (right, &key);

	ext = extent_item (right);
	for (; right->unit_pos < coord_num_units (right); right->unit_pos ++, ext ++) {
		trace_on (TRACE_EXTENTS, "alloc_and_copy_extent: unit %u/%u\n", right->unit_pos, coord_num_units (right));
		if (!extent_needs_allocation (ext, flush_pos_hint (flush_pos))) {
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
				trace_on (TRACE_EXTENTS, "alloc_and_copy_extent: target full, !needs_allocation\n");
				/* set coord @right such that this unit does
				 * not get cut because it was not moved */
				right->between = BEFORE_UNIT;
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
		 * until whole extent is allocated and there is space in left
		 * neighbor
		 */
		ON_DEBUG (allocate_times = 0);
		while (to_allocate) {
			result = extent_allocate_blocks (flush_pos_hint (flush_pos), to_allocate,
							 &first_allocated,
							 &allocated);
			if (result) {
				return result;
			}

			trace_on (TRACE_EXTENTS, "alloc_and_copy_extent: to_allocate = %llu got %llu\n", to_allocate, allocated);
			ON_DEBUG (allocate_times += 1);

			to_allocate -= allocated;

			/* FIXME: JMACD->ZAM->VS: I think the block allocator should do this. */
			flush_pos_hint (flush_pos)->blk += allocated;

			if (!try_to_glue (left, right, first_allocated, allocated)) {
				/*
				 * could not copy current extent by just
				 * increasing width of last extent in left
				 * neighbor, add new extent to the end of
				 * @left
				 */
				extent_set_start (&new_ext, first_allocated);
				extent_set_width (&new_ext, (reiser4_block_nr)allocated);

				result = put_unit_to_end (left, &key,
							  init_new_extent (&data,
									   &new_ext, 1));
				if (result == -ENOSPC) {
					/*
					 * @left is full, free blocks we
					 * grabbed because this extent will
					 * stay in right->node, and, therefore,
					 * blocks it points to have different
					 * predecer in parent-first order. Set
					 * "defer" parameter of
					 * reiser4_dealloc_block to 0 because
					 * these blocks can be made allocable
					 * again immediately.
					 * FIXME-VS: set target state to grabbed? 
					 */
					ON_DEBUG (if (allocate_times > 1) {
						info ("stop here!\n");
					});

					reiser4_dealloc_blocks (&first_allocated, &allocated,
								0 /* defer */, BLOCK_GRABBED);
					result = SQUEEZE_TARGET_FULL;
					trace_on (TRACE_EXTENTS, "alloc_and_copy_extent: target full, to_allocate = %llu\n", to_allocate);
					/**/
					if (to_allocate == extent_get_width (ext)) {
						/* nothing of this unit were
						 * allocated and copied. Take
						 * care that it does not get
						 * cut */
						right->between = BEFORE_UNIT;
					} else {
						/* part of extent was allocated
						 * and copied to left
						 * neighbor. Leave coord @right
						 * at this unit so that it will
						 * be cut properly by
						 * squalloc_right_twig_cut */
					}
					
					goto done;
				}
			}
			/*
			 * find all pages for which blocks were allocated and
			 * assign block numbers to jnodes of those pages
			 */
			if ((result = assign_jnode_blocknrs (&key, first_allocated, allocated, flush_pos))) {
				goto done;
			}
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

	assert ("vs-421",
		result < 0 || result == SQUEEZE_TARGET_FULL || SQUEEZE_CONTINUE);

	assert ("vs-678", item_is_extent (right));

	if (right->unit_pos == coord_num_units (right)) {
		/* whole item was allocated and copied, set coord after item */
		right->unit_pos = 0;
		right->between = AFTER_ITEM;
	}
	return result;
}


/*
 * helper for allocate_extent_item_in_place.
 * paste new unallocated extent of @width after unit @coord is set to. Ask
 * insert_into_item to not try to shift anything to left
 */
/* Audited by: green(2002.06.13) */
static int paste_unallocated_extent (coord_t * item, reiser4_key * key,
				     reiser4_block_nr width)
{
	int result;
	coord_t coord;
	reiser4_item_data data;
	reiser4_extent new_ext;


	set_extent (&new_ext, UNALLOCATED_EXTENT, width);
	
	coord_dup (&coord, item);
	coord.between = AFTER_UNIT;
	/*
	 * have insert_into_item to not shift anything to left
	 */
	result = resize_item (&coord, init_new_extent (&data, &new_ext, 1), key,
			      0/*lh*/, COPI_DONT_SHIFT_LEFT);

	return result;
}


/*
 * find all units of extent item which require allocation. Allocate free blocks
 * for them and replace those extents with new ones. As result of this item may
 * "inflate", so, special precautions are taken to have it to inflate to right
 * only, so items to the right of @item and part of item itself may get moved
 * to right
 */
/* Audited by: green(2002.06.13) */
int allocate_extent_item_in_place (coord_t * item, flush_position *flush_pos)
{
	int result;
	unsigned i;
 	reiser4_extent * ext;
	reiser4_block_nr first_allocated;
	reiser4_block_nr initial_width, allocated;
	reiser4_key key;
	unsigned long blocksize;


	blocksize = current_blocksize;

	assert ("vs-451", item->unit_pos == 0 && coord_is_existing_unit (item));
	assert ("vs-773", item_is_extent (item));

	ext = extent_item (item);
	for (i = 0; i < coord_num_units (item); i ++, ext ++, item->unit_pos ++) {
		if (!extent_needs_allocation (ext, flush_pos_hint (flush_pos)))
			continue;
		assert ("vs-439", state_of_extent (ext) == UNALLOCATED_EXTENT);

		/*
		 * try to get extent_width () free blocks starting from
		 * *preceder
		 */
		initial_width = extent_get_width (ext);
		result = extent_allocate_blocks (flush_pos_hint (flush_pos), initial_width,
						 &first_allocated, &allocated);
		if (result)
			return result;

		assert ("vs-440", allocated > 0);
		/*
		 * update extent's start, width and recalculate preceder
		 */
		extent_set_start (ext, first_allocated);
		extent_set_width (ext, allocated);
		flush_pos_hint (flush_pos)->blk = first_allocated + allocated - 1;

		unit_key_by_coord (item, &key);
		/*
		 * find all pages for which blocks were allocated and assign
		 * block numbers to jnodes of those pages
		 */
		if ((result = assign_jnode_blocknrs (&key, first_allocated, allocated, flush_pos))) {
			return result;
		}

		if (allocated == initial_width)
			/*
			 * whole extent is allocated
			 */
			continue;
		/* set @key to key of first byte of part of extent whihc left
		 * unallocated */
		set_key_offset (&key, get_key_offset (&key) + allocated * blocksize);

		/* put remaining unallocated extent into tree right after newly
		 * overwritten one */
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

	/* set coord after last unit in the item */
	assert ("vs-679", item_is_extent (item));
	item->unit_pos = coord_last_unit_pos (item);
	item->between = AFTER_UNIT;

	return 0;
}


/* Block offset of first block addressed by unit */
/* Audited by: green(2002.06.13) */
/* AUDIT shouldn't return value be of reiser4_block_nr type?
 * Josh's answer: who knows?  This returns the same type of information as "struct page->index", which is currently an unsigned long. */
__u64 extent_unit_index (const coord_t * item)
{
	reiser4_key key;

	assert ("vs-648", coord_is_existing_unit (item));
	unit_key_by_coord (item, &key);
	return get_key_offset (&key) >> current_blocksize_bits;
}


/* Audited by: green(2002.06.13) */
/* AUDIT shouldn't return value be of reiser4_block_nr type?
 * Josh's answer: who knows?  Is a "number of blocks" the same type as "block offset"? */
__u64 extent_unit_width (const coord_t * item)
{
	assert ("vs-649", coord_is_existing_unit (item));
	return width_by_coord (item);
}

/* Starting block location of this unit. */
reiser4_block_nr extent_unit_start (const coord_t *item)
{
	return extent_get_start (extent_by_coord (item));
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
