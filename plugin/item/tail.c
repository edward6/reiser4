/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
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
 * plugin->u.item.b.can_contain_key
 */
int tail_can_contain_key (const tree_coord * coord, const reiser4_key * key,
			  const reiser4_item_data * data)
{
	reiser4_key item_key;


	if (item_plugin_by_coord (coord) != data->iplug)
		return 0;

	item_key_by_coord (coord, &item_key);
	if (get_key_locality (key) != get_key_locality (&item_key) ||
	    get_key_objectid (key) != get_key_objectid (&item_key))
		return 0;

	assert ("vs-459",
		(coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
		(coord->unit_pos == last_unit_pos (coord) &&
		 coord->between == AFTER_UNIT));

	if (coord->between == BEFORE_UNIT) {
		if (get_key_offset (key) + data->length !=
		    get_key_offset (&item_key)) {
			info ("could not merge tail items of one file\n");
			return 0;
		} else
			return 1;

	} else {
		if (get_key_offset (key) != (get_key_offset (&item_key) +
					     item_length_by_coord (coord))) {
			info ("could not append tail item with "
			      "a tail item of the same file\n");
			return 0;
		} else
			return 1;
	}
}


/*
 * plugin->u.item.b.mergeable
 * first item is of tail type
 */
int tail_mergeable (const tree_coord * p1, const tree_coord * p2)
{
	reiser4_key key1, key2;


	assert ("vs-535",
		item_type_by_coord (p1) == ORDINARY_FILE_METADATA_TYPE);
	assert ("vs-365", item_id_by_coord (p1) == TAIL_ID);

	if (item_id_by_coord (p2) != TAIL_ID) {
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
	assert ("vs-416", keygt (key, &item_key));

	if (keygt (key, tail_max_key_inside (coord, &item_key))) {
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

	if (data->data) {
		assert ("vs-554", data->user == 0 || data->user == 1);
		if (data->user)
			/* copy from user space */
			__copy_from_user (item + coord->unit_pos, data->data,
					  (unsigned)data->length);
		else
			/* copy from kernel space */
			xmemcpy (item + coord->unit_pos, data->data,
				 (unsigned)data->length);
	} else {
		xmemset (item + coord->unit_pos, 0, (unsigned)data->length);
	}
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


	if (!znode_contains_key_lock (coord->node, key)) {
		return TAIL_RESEARCH;
	}


	if (inode->i_size == 0) {
		/*
		 * no items of this file in tree yet
		 */
		if (get_key_offset (key) == 0)
			return TAIL_FIRST_ITEM;
		else
			return TAIL_CREATE_HOLE;
	}

	if (!inode_file_plugin (inode)->owns_item (inode, coord)) {
		/* extent2tail conversion is in progress */
		if (get_key_offset (key))
			return TAIL_CANT_CONTINUE;
		return TAIL_FIRST_ITEM;
	}

	/* found item is tail item of the file we write to */
	assert ("vs-580", item_id_by_coord (coord) == TAIL_ID);

	item_key_by_coord (coord, &item_key);
	assert ("vs-581",
		get_key_objectid (key) == get_key_objectid (&item_key));


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
			    char * data, int user, unsigned desired_len)
{
	item->data = data;
	item->user = user;
	item->length = node_plugin_by_node (coord->node)->max_item_size ();
	if ((int)desired_len < item->length)
		item->length = (int)desired_len;
	item->arg = 0;
	item->iplug = item_plugin_by_id (TAIL_ID);
}


/*
 * insert tail item consisting of zeros only. Number of bytes appended to the
 * file is returned
 */
static int create_hole (tree_coord * coord, lock_handle * lh, flow_t * f)
{
	int result;
	reiser4_key hole_key;
	reiser4_item_data item;


	hole_key = f->key;
	set_key_offset (&hole_key, 0ull);

	assert ("vs-384", get_key_offset (&f->key) <= INT_MAX);
	assert ("vs-575", f->user == 1);
	make_item_data (coord, &item, 0, 0/*user*/,
			(unsigned)get_key_offset (&f->key));
	result = insert_by_coord (coord, &item, &hole_key, lh, 0, 0, 0/*flags*/);
	if (result)
		return result;

	return item.length;
}


/*
 * append @coord item with zeros. Number of bytes appended to the file is
 * returned
 */
static int append_hole (tree_coord * coord, lock_handle * lh, flow_t * f)
{
	int result;
	reiser4_key hole_key;
	reiser4_item_data item;


	item_key_by_coord (coord, &hole_key);
	set_key_offset (&hole_key,
			get_key_offset (&hole_key) + coord->unit_pos + 1);

	assert ("vs-384", (get_key_offset (&f->key) - 
			   get_key_offset (&hole_key)) <= INT_MAX);
	assert ("vs-576", f->user == 1);
	make_item_data (coord, &item, 0, 0/*user*/,
			(unsigned)(get_key_offset (&f->key) -
				   get_key_offset (&hole_key)));
	result = resize_item (coord, &item, &hole_key, lh, 0/*flags*/);
	if (result)
		return result;

	return item.length;
}


/*
 * insert first item of file into tree. Number of bytes appended to the file is
 * returned
 */
static int insert_first_item (tree_coord * coord, lock_handle * lh, flow_t * f)
{
	reiser4_item_data item;
	int result;

	assert ("vs-383", get_key_offset (&f->key) == 0);

	make_item_data (coord, &item, f->data, f->user, f->length);

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
static int append_tail (tree_coord * coord, lock_handle * lh, flow_t * f)
{
	reiser4_item_data item;
	int result;


	make_item_data (coord, &item, f->data, f->user, f->length);

	result = resize_item (coord, &item, &f->key, lh, 0/*flags*/);
	if (result)
		return result;

	move_flow_forward (f, (unsigned)item.length);
	return item.length;
}


/*
 * copy user data over file tail item
 */
static int overwrite_tail (tree_coord * coord, flow_t * f)
{
	int result;
	unsigned count;


	count = item_length_by_coord (coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	/*
	 * FIXME-ME: mark_znode_dirty ?
	 */
	assert ("vs-570", f->user == 1);
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
		lock_handle * lh, flow_t * f, struct page * page UNUSED_ARG)
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

		if (result && !page) {
			/*
			 * file became longer and his write is not part of
			 * extent2tail
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
	       lock_handle * lh UNUSED_ARG, flow_t * f)
{
	unsigned count;


	assert ("vs-387", 
	({
		reiser4_key key;

		keyeq (unit_key_by_coord (coord, &key), &f->key);
	}));

	/*
	 * calculate number of bytes to read off the item
	 */
	count = item_length_by_coord (coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	assert ("vs-571", f->user == 1);
	__copy_to_user (f->data,  (char *)item_body_by_coord (coord) + coord->unit_pos,
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
