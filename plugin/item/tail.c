/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/*
 * plugin->u.item.b.max_key_inside
 */
reiser4_key * tail_max_key_inside (const tree_coord * coord, 
				   reiser4_key * key)
{
	item_key_by_coord (coord, key);
	set_key_offset (key, get_key_offset (max_key ()));
	return key;
}


/*
 * plugin->u.item.b.mergeable
 * first item is of tail type
 */
int tail_mergeable (const tree_coord * p1, const tree_coord * p2)
{
	reiser4_key key1, key2;


	assert ("vs-365", item_plugin_id (item_plugin_by_coord (p1)) == BODY_ITEM_ID);

	if (item_plugin_to_plugin (item_plugin_by_coord (p2))->h.id != BODY_ITEM_ID) {
		/*
		 * second item is of another type
		 */
		return 0;
	}

	item_key_by_coord (p1, &key1);
	item_key_by_coord (p2, &key2);
	if (get_key_locality (&key1) != get_key_locality (&key2) ||
	    get_key_objectid (&key1) != get_key_objectid (&key2) ||
	    get_key_type (&key1) != get_key_type (&key2)) {
		/*
		 * items of different objects
		 */
		return 0;
	}
	if (get_key_offset (&key1) + tail_nr_units (p1) !=
	    get_key_offset (&key2)) {
		/*
		 * not adjacent items
		 */
		return 0;
	}
	return 1;
}


/*
 * plugin->u.item.b.print
 * plugin->u.item.b.check
 */


/*
 * plugin->u.item.b.nr_units
 */
unsigned tail_nr_units (const tree_coord * coord)
{
	return item_length_by_coord (coord);
}


/*
 * plugin->u.item.b.lookup
 */
lookup_result tail_lookup (const reiser4_key * key, lookup_bias bias,
			   tree_coord * coord)
{
	reiser4_key item_key;
	__u64 lookuped, offset;
	unsigned nr_units;


	item_key_by_coord (coord, &item_key);
	offset = get_key_offset (item_key_by_coord (coord, &item_key));
	nr_units = tail_nr_units (coord);

	/*
	 * key we are looking for must be greater than key of item @coord
	 */
	assert ("vs-416", keycmp (key, &item_key) == GREATER_THAN);

	if (keycmp (key, tail_max_key_inside (coord, &item_key)) ==
	    GREATER_THAN) {
		/*
		 * @key is key of another file
		 */
		coord->unit_pos = nr_units - 1;
		coord->between = AFTER_UNIT;
		return CBK_COORD_NOTFOUND;
	}

	/*
	 * offset we are looking for
	 */
	lookuped = get_key_offset (key);


	if (lookuped >= offset &&
	    lookuped < offset + nr_units) {
		/*
		 * byte we are looking for is in this item
		 */
		coord->unit_pos = lookuped - offset;
		coord->between = AT_UNIT;
		return CBK_COORD_FOUND;
	}

	/*
	 * set coord after last unit
	 */
	coord->unit_pos = nr_units - 1;
	coord->between = AFTER_UNIT;
	return bias == FIND_MAX_NOT_MORE_THAN ? CBK_COORD_FOUND : CBK_COORD_NOTFOUND;
}


/*
 * plugin->u.item.b.paste
 */
int tail_paste (tree_coord * coord, reiser4_item_data * data,
		carry_level * todo UNUSED_ARG)
{
	unsigned old_item_length;
	char * item;

	/*
	 * length the item ahd before resizing has been performed
	 */
	old_item_length = item_length_by_coord (coord) - data->length;

	/*
	 * tail items never get pasted in the middle
	 */
	assert ("vs-363",
		(coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
		(coord->unit_pos == old_item_length - 1 &&
		 coord->between == AFTER_UNIT) ||
		(coord->unit_pos == 0 && old_item_length == 0 &&
		 coord->between == AT_UNIT));

	item = item_body_by_coord (coord);
	if (coord->unit_pos == 0)
		/*
		 * make space for pasted data when pasting at the beginning of
		 * the item
		 */
		xmemmove (item + data->length, item, old_item_length);

	if (coord->between == AFTER_UNIT)
		coord->unit_pos ++;

	if (data->data)
		xmemcpy (item + coord->unit_pos, data->data, (unsigned)data->length);
	else
		xmemset (item + coord->unit_pos, 0, (unsigned)data->length);
	return 0;
}


/*
 * plugin->u.item.b.fast_paste
 */


/*
 * plugin->u.item.b.can_shift
 * number of units is returned via return value, number of bytes via @size. For
 * tail items they coincide
 */
int tail_can_shift (unsigned free_space, tree_coord * source UNUSED_ARG,
		    znode * target UNUSED_ARG,
		    shift_direction direction UNUSED_ARG,
		    unsigned * size, unsigned want)
{
	/*
	 * make sure that that we do not want to shift more than we have
	 */
	assert ("vs-364", want > 0 &&
		want <= (unsigned)item_length_by_coord (source));

	*size = min (want, free_space);
	return *size;
}


/*
 * plugin->u.item.b.copy_units
 */
void tail_copy_units (tree_coord * target, tree_coord * source,
		      unsigned from, unsigned count,
		      shift_direction where_is_free_space,
		      unsigned free_space UNUSED_ARG)
{
	/*
	 * make sure that item @target is expanded already
	 */
	assert ("vs-366", (unsigned)item_length_by_coord (target) >= count);
	assert ("vs-370", free_space >= count);

	if (where_is_free_space == SHIFT_LEFT) {
		/*
		 * append item @target with @count first bytes of @source
		 */
		assert ("vs-365", from == 0);

		xmemcpy ((char *)item_body_by_coord (target) + item_length_by_coord (target) - count,
			(char *)item_body_by_coord (source), count);
	} else {
		/*
		 * target item is moved to right already
		 */
		reiser4_key key;


		assert ("vs-367", (unsigned)item_length_by_coord (source) == from + count);

		xmemcpy ((char *)item_body_by_coord (target),
			(char *)item_body_by_coord (source) + from, count);

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		item_key_by_coord (target, &key);
		assert ("vs-369", get_key_offset (&key) >= count);
		set_key_offset (&key, get_key_offset (&key) - count);

		node_plugin_by_node (target->node)->update_item_key (target, &key, 0/*todo*/);
	}
}


/*
 * plugin->u.item.b.create_hook
 * plugin->u.item.b.kill_hook
 * plugin->u.item.b.shift_hook
 */


/*
 * plugin->u.item.b.cut_units
 * plugin->u.item.b.kill_units
 */
int tail_cut_units (tree_coord * coord, unsigned * from, unsigned * to,
		    const reiser4_key * from_key UNUSED_ARG,
		    const reiser4_key * to_key UNUSED_ARG,
		    reiser4_key * smallest_removed)
{
	reiser4_key key;
	unsigned count;


	count = *to - *from + 1;
	/*
	 * regarless to whether we cut from the beginning or from the end of
	 * item - we have nothing to do
	 */
	assert ("vs-374", count > 0 &&
		count <= (unsigned)item_length_by_coord (coord));
	/*
	 * tails items are never cut from the middle of an item
	 */
	assert ("vs-396", ergo (*from != 0, *to == last_unit_pos (coord)));

	/*
	 * check @from_key and @to_key if they are set
	 */
	assert ("vs-397",
		ergo (from_key,				
		      get_key_offset (from_key) ==
		      get_key_offset (item_key_by_coord (coord, &key)) + *from)
		);
	assert ("vs-398",
		ergo (to_key,
		      get_key_offset (to_key) ==
		      get_key_offset (item_key_by_coord (coord, &key)) + *to));

	if (smallest_removed) {
		/*
		 * store smallest key removed
		 */
		item_key_by_coord (coord, smallest_removed);
		set_key_offset (smallest_removed,
				get_key_offset (smallest_removed) + *from);
	}
	if (*from == 0) {
		/*
		 * head of item is removed, update item key therefore
		 */
		item_key_by_coord (coord, &key);
		set_key_offset (&key, get_key_offset (&key) + count);
		node_plugin_by_node (coord->node)->update_item_key (coord, &key, 0/*todo*/);
	}

	if (REISER4_DEBUG)
		xmemset ((char *)item_body_by_coord (coord) + *from, 0, count);
	return count;
}


/*
 * plugin->u.item.b.unit_key
 */
reiser4_key * tail_unit_key (const tree_coord * coord, reiser4_key * key)
{
	assert ("vs-375", coord_of_unit (coord));

	item_key_by_coord (coord, key);
	set_key_offset (key, (get_key_offset (key) + coord->unit_pos));

	return key;
}


/*
 * plugin->u.item.b.estimate
 * plugin->u.item.b.item_data_by_flow
 */


typedef enum {
	TAIL_CREATE_HOLE,
	TAIL_APPEND_HOLE,
	TAIL_FIRST_ITEM,
	TAIL_OVERWRITE,
	TAIL_APPEND,
	TAIL_RESEARCH,
	TAIL_CANT_CONTINUE
} tail_write_todo;


static tail_write_todo tail_what_todo (struct inode * inode, tree_coord * coord,
				  reiser4_key * key)
{
	reiser4_key item_key;


	spin_lock_dk (current_tree);
	if (!znode_contains_key (coord->node, key)) {
		spin_unlock_dk (current_tree);
		return TAIL_RESEARCH;
	}
	spin_unlock_dk (current_tree);


	if (inode->i_size == 0) {
		/*
		 * no items of this file in tree yet
		 */
		if (get_key_offset (key) == 0)
			return TAIL_FIRST_ITEM;
		else
			return TAIL_CREATE_HOLE;
	}

	if (item_plugin_id (item_plugin_by_coord (coord)) != BODY_ITEM_ID)
		return TAIL_CANT_CONTINUE;

	item_key_by_coord (coord, &item_key);
	if (get_key_objectid (key) != get_key_objectid (&item_key))
		return TAIL_CANT_CONTINUE;

	if (coord_of_unit (coord)) {
		/*
		 * make sure that @coord is set to proper position
		 */
		if (get_key_offset (key) ==
		    get_key_offset (unit_key_by_coord (coord, &item_key)))
			return TAIL_OVERWRITE;
		else
			return TAIL_RESEARCH;
	}

	if (coord->between != AFTER_UNIT ||
	    coord->unit_pos != last_unit_pos (coord)) {
		/*
		 * FIXME-VS: we could try to adjust coord
		 */
		return TAIL_RESEARCH;
	}	

	if (get_key_offset (key) == (get_key_offset (&item_key) +
				     coord->unit_pos + 1))
		return TAIL_APPEND;
	return TAIL_APPEND_HOLE;
}


/*
 * prepare item data which will be passed down to either insert_by_coord or to
 * resize_item
 */
static void make_item_data (tree_coord * coord, reiser4_item_data * item,
			    char * data, unsigned desired_len)
{
	item->data = data;
	item->length = node_plugin_by_node (coord->node)->max_item_size ();
	if ((int)desired_len < item->length)
		item->length = (int)desired_len;
	item->arg = 0;
	item->iplug = item_plugin_by_id (BODY_ITEM_ID);
}


/*
 * insert tail item consisting of zeros only. Number of bytes appended to the
 * file is returned
 */
static int create_hole (tree_coord * coord, reiser4_lock_handle * lh, flow * f)
{
	int result;
	reiser4_key hole_key;
	reiser4_item_data item;


	hole_key = f->key;
	set_key_offset (&hole_key, 0ull);

	assert ("vs-384", get_key_offset (&f->key) <= INT_MAX);
	make_item_data (coord, &item, 0, (unsigned)get_key_offset (&f->key));
	result = insert_by_coord (coord, &item, &hole_key, lh, 0, 0, 0/*flags*/);
	if (result)
		return result;

	return item.length;
}


/*
 * append @coord item with zeros. Number of bytes appended to the file is
 * returned
 */
static int append_hole (tree_coord * coord, reiser4_lock_handle * lh, flow * f)
{
	int result;
	reiser4_key hole_key;
	reiser4_item_data item;


	item_key_by_coord (coord, &hole_key);
	set_key_offset (&hole_key,
			get_key_offset (&hole_key) + coord->unit_pos + 1);

	assert ("vs-384", (get_key_offset (&f->key) - 
			   get_key_offset (&hole_key)) <= INT_MAX);
	make_item_data (coord, &item, 0,
			(unsigned)(get_key_offset (&f->key) -
				   get_key_offset (&hole_key)));
	result = resize_item (coord, &item, &hole_key, lh, 0/**/);
	if (result)
		return result;

	return item.length;
}


/*
 * @count bytes of flow @f got written, update correspondingly f->length,
 * f->data and f->key
 */
static void move_flow_forward (flow * f, unsigned count)
{
	f->data += count;
	f->length -= count;
	set_key_offset (&f->key, get_key_offset (&f->key) + count);
}


/*
 * insert first item of file into tree. Number of bytes appended to the file is
 * returned
 */
static int insert_first_item (tree_coord * coord, reiser4_lock_handle * lh, flow * f)
{
	reiser4_item_data item;
	int result;

	assert ("vs-383", get_key_offset (&f->key) == 0);

	make_item_data (coord, &item, f->data, f->length);
	result = insert_by_coord (coord, &item, &f->key, lh, 0, 0, 0/*flags*/);
	if (result)
		return result;
	
	move_flow_forward (f, (unsigned)item.length);
	return item.length;
}


/*
 * append item @coord with flow @f's data. Number of bytes appended to the file
 * is returned
 */
static int append_tail (tree_coord * coord, reiser4_lock_handle * lh, flow * f)
{
	reiser4_item_data item;
	int result;


	make_item_data (coord, &item, f->data, f->length);
	/*
	 * FIXME-VS: we must copy data with __copy_from_user
	 */
	result = resize_item (coord, &item, &f->key, lh, 0/*flags*/);
	if (result)
		return result;

	move_flow_forward (f, (unsigned)item.length);
	return item.length;
}


/*
 * copy user data over file tail item
 */
static int overwrite_tail (tree_coord * coord, flow * f)
{
	int result;
	unsigned count;


	count = item_length_by_coord (coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	/*
	 * FIXME-ME: mark_znode_dirty ?
	 */
	result = __copy_from_user ((char *)item_body_by_coord (coord) +
				   coord->unit_pos, f->data, count);
	if (result)
		return result;
		
	move_flow_forward (f, count);
	return 0;
}


/*
 * plugin->u.item.s.file.write
 * access to data stored in tails goes directly through formatted nodes
 */
int tail_write (struct inode * inode, tree_coord * coord,
		reiser4_lock_handle * lh, flow * f)
{
	int result;


	while (f->length) {
		switch (tail_what_todo (inode, coord, &f->key)) {
		case TAIL_CREATE_HOLE:
			result = create_hole (coord, lh, f);
			break;
		case TAIL_APPEND_HOLE:
			result = append_hole (coord, lh, f);
			break;
		case TAIL_FIRST_ITEM:
			result = insert_first_item (coord, lh, f);
			break;
		case TAIL_OVERWRITE:
			result = overwrite_tail (coord, f);
			break;
		case TAIL_APPEND:
			result = append_tail (coord, lh, f);
			break;
		case TAIL_RESEARCH:
			result = -EAGAIN;
			break;
		case TAIL_CANT_CONTINUE:
		default:
			result = -EIO;
			break;
		}
		if (result < 0)
			/*
			 * error occured or research is required
			 */
			return result;

		if (result) {
			/*
			 * file became longer
			 */
			inode->i_size += result;
			mark_inode_dirty (inode);
		}
	}

	return 0;
}


/*
 * plugin->u.item.s.file.read
 */
int tail_read (struct inode * inode UNUSED_ARG, tree_coord * coord,
	       reiser4_lock_handle * lh UNUSED_ARG, flow * f)
{
	unsigned count;


	assert ("vs-387", 
	({
		reiser4_key key;

		keycmp (unit_key_by_coord (coord, &key), &f->key) == EQUAL_TO;
	}));

	/*
	 * calculate number of bytes to read off the item
	 */
	count = item_length_by_coord (coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	xmemcpy (f->data, (char *)item_body_by_coord (coord) + coord->unit_pos,
		count);

	move_flow_forward (f, count);
	return 0;
}



/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
