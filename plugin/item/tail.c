/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/*
 * plugin->u.item.b.max_key_inside
 */


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
	unsigned long long lookuped, item_offset;


	/*
	 * offset we are looking for
	 */
	lookuped = get_key_offset (key);
	item_offset = get_key_offset (item_key_by_coord (coord, &item_key));

	if (lookuped < item_offset) {
		coord->unit_pos = 0;
		coord->between = BEFORE_UNIT;
		return CBK_COORD_NOTFOUND;
	}

	if (lookuped >= item_offset &&
	    lookuped < item_offset + tail_nr_units (coord)) {
		/*
		 * byte we are looking for is in this item
		 */
		coord->unit_pos = lookuped - item_offset;
		coord->between = AT_UNIT;
		return CBK_COORD_FOUND;
	}

	/*
	 * set coord after last unit
	 */
	coord->unit_pos = tail_nr_units (coord);
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
	assert ("vs-363", coord->unit_pos == 0 ||
		coord->unit_pos == old_item_length);

	item = item_body_by_coord (coord);
	if (coord->unit_pos == 0)
		/*
		 * make space for pasted data when pasting at the beginning of
		 * the item
		 */
		memmove (item + data->length, item, old_item_length);

	memcpy (item + coord->unit_pos, data->data, (unsigned)data->length);
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
int tail_can_shift (unsigned free_space, tree_coord * source,
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
		      unsigned free_space)
{
	/*
	 * make sure that item @target is expanded already
	 */
	assert ("vs-366", (unsigned)item_length_by_coord (target) >= count);
	assert ("vs-370", free_space >= count);

	if (where_is_free_space == SHIFT_APPEND) {
		/*
		 * append item @target with @count first bytes of @source
		 */
		assert ("vs-365", from == 0);

		memcpy ((char *)item_body_by_coord (target) + item_length_by_coord (target) - count,
			(char *)item_body_by_coord (source), count);
	} else {
		/*
		 * target item is moved to right already
		 */
		reiser4_key key;


		assert ("vs-367", (unsigned)item_length_by_coord (source) == from + count);

		memcpy ((char *)item_body_by_coord (target),
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
int tail_cut_units (tree_coord * item, unsigned from, unsigned count,
		    shift_direction where_to_move_free_space,
		    const reiser4_key * from_key UNUSED_ARG,
		    const reiser4_key * to_key UNUSED_ARG,
		    reiser4_key * smallest_removed)
{
	/*
	 * regarless to whether we cut from the beginning or from the end of
	 * item - we have nothing to do
	 */
	assert ("vs-374", count > 0 &&
		count < (unsigned)item_length_by_coord (item));

	if (where_to_move_free_space == SHIFT_APPEND) {
		assert ("vs-371", 
			from + count == (unsigned)item_length_by_coord (item));
	} else {
		assert ("vs-372", from == 0);
	}

	if (smallest_removed) {
		/*
		 * store smallest key removed
		 */
		item_key_by_coord (item, smallest_removed);
		set_key_offset (smallest_removed,
				get_key_offset (smallest_removed) + from);
	}
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
	CREATE_HOLE,
	APPEND_HOLE,
	FIRST_ITEM,
	OVERWRITE,
	APPEND,
	RESEARCH,
	CANT_CONTINUE
} tail_write_todo;


static tail_write_todo what_todo (struct inode * inode, tree_coord * coord,
				  reiser4_key * key)
{
	reiser4_key item_key;


	spin_lock_dk (current_tree);
	if (!znode_contains_key (coord->node, key)) {
		spin_unlock_dk (current_tree);
		return RESEARCH;
	}
	spin_unlock_dk (current_tree);


	if (inode->i_size == 0) {
		/*
		 * no items of this file in tree yet
		 */
		if (get_key_offset (key) == 0)
			return FIRST_ITEM;
		else
			return CREATE_HOLE;
	}

	if (item_plugin_id (item_plugin_by_coord (coord)) != BODY_ITEM_ID)
		return CAN_CONTINUE;

	item_key_by_coord (coord, &item_key);
	if (get_key_objectid (key) != get_key_objectid (&item_key))
		return CANT_CONTINUE;

	if (coord_of_unit (coord)) {
		/*
		 * make sure that @coord is set to proper position
		 */
		if (get_key_offset (key) ==
		    get_key_offset (unit_key_by_coord (coord, &item_key)))
			return OVERWRITE;
		else
			return RESEARCH;
	}

	assert ("vs-380", coord->between == AFTER_UNIT);
	assert ("vs-381", coord->unit_pos == last_unit_pos (coord));

	if (get_key_offset (key) == (get_key_offset (&item_key) +
				     coord->unit_pos + 1))
		return APPEND;
	return APPEND_HOLE;
}


static int create_hole (tree_coord * coord,
			reiser4_lock_handle * lh, flow * f)
{
	reiser4_item_data item;
	
	item.data = 0;
	item.length = ;
	item.arg = 0;
	item.iplug = ;
	return insert_by_coord (coord, &item, key);
}


/*
 * plugin->u.item.s.file.write
 * access to data stored in tails goes directly through formatted nodes
 */
int tail_write (struct inode * inode, tree_coord * coord,
		reiser4_lock_handle * lh, flow * f)
{
	int result;
	struct page * page;
	unsigned long long file_off; /* offset within a file we write to */
	unsigned long long blocksize;
	reiser4_tree * tree;
	int research = 0;
	char * kaddr;
	unsigned count;


	switch (what_todo (inode, coord, &f->key)) {
	case CREATE_HOLE:
		result = create_hole (coord, lh, );
		break;
	case APPEND_HOLE:
		result = append_hole (coord, lh, );
		break;
	case FIRST_ITEM:
		result = insert_first_item (coord, lh, );
		break;
	case OVERWRITE:
		result = overwrite_tail (coord, lh, f);
		break;
	case APPEND:
		result = append_tail (coord, lh, f);
		break;
	case RESEARCH:
		result = -EAGAIN;
		break;
	case CANT_CONTINUE:
	default:
		result = -EIO;
		break;
	}
	return result;
}
	/*
	 *
	 */


	tree = tree_by_inode (inode);

	blocksize = reiser4_get_current_sb ()->s_blocksize;
	file_off = get_key_offset (&f->key);

	while (f->length) {
		page = grab_cache_page (inode->i_mapping,
					(unsigned long)(file_off >> PAGE_SHIFT));
		if (IS_ERR (page))
			return PTR_ERR (page);

		kaddr = kmap (page);
		count = f->length;
		result = prepare_write (tree, coord, lh, page, &f->key, &count);
		if (result && result != -EAGAIN) {
			/* error occurred */
			kunmap (page);
			UnlockPage (page);
			page_cache_release (page);
			return result;
		}

		if (result == -EAGAIN)
			/* not entire page is prepared for writing into it */
			research = 1;

		result = __copy_from_user (kaddr + (file_off & ~PAGE_MASK),
					   f->data, count);
		flush_dcache_page (page);

		commit_write (page, file_off, count);

		kunmap (page);
		UnlockPage (page);
		page_cache_release (page);
		if (result)
			return result;

		file_off += count;
		f->data += count;
		f->length -= count;
		set_key_offset (&f->key, file_off);

		if (research)
			return -EAGAIN;
	}

	return 0;
}


/* plugin->u.item.s.file.fill_page
   this uses @reiser4_iterate_tree to find all blocks populating @page. 
 */


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
