/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"


/* plugin->u.item.b.body_partial_copy
   plugin->u.item.b.shift
   plugin->u.item.b.estimate_shift
   plugin->u.item.b.append
   plugin->u.item.b.insert
   plugin->u.item.b.cut
   plugin->u.item.b.overwrite
   plugin->u.item.b.max_key_inside
   plugin->u.item.b.is_mergeable
   plugin->u.item.b.print
   plugin->u.item.b.check
   plugin->u.item.b.item_find_insertion_coord
*/

/* plugin->u.item.b.nr_units
 */
int tail_nr_units (item_coord * item)
{
	return length_by_coord (item);
}


/*
   plugin->u.item.b.lookup
   plugin->u.item.b.estimate_space_needed
   plugin->u.item.b.is_in_item
   plugin->u.item.b.paste
*/


/* plugin->u.item.b.can_shift
   look for description of this method in plugin/item/item.h
*/
int tail_can_shift (int free_space, new_coord * target,
		    unit_coord * source, shift_direction pend,
		    int * size, int stop)
{
	int length;
	int want;

	*size = length_by_coord (source);
	if (*size > free_space)
		*size = free_space;

	/* we can shift *size bytes, calculate how many do we want to shift */
	if (stop) {
		want = source->unit_pos + stop * pend;
		if (*size > want)
			*size = want;
	}

	/* units and bytes are the same for tail items */
	return *size;
}


/* plugin->u.item.b.copy_units
   look for description of this method in plugin/item/item.h
 */
void tail_copy_units (new_coord * target, unit_coord * source,
		      int unit_num, shift_direction pend,
		      char * new_data)
{
	item_header * target_ih;
	char * source_item;


	/* if @new_data is set - we just copy @unit_num bytes from head or end
	   of source item */
	assert ("vs-108", new_data != 0);

	target_ih = item_header_by_coord (target);
	source_item = item_body_by_coord (source);

	if (pend == append) {
		/* append target item with @copy_amount bytes of source item */
		xmemcpy (new_data, source_item, unit_num);
	} else {
		/* item should have already free space at the beginning */
		xmemcpy (new_data, source_item + length_by_coord (source) - unit_num,
			unit_num);
		assert ("vs-107",
			get_key_offset (&target_ih->key) >= unit_num);
		set_key_offset (&target_ih->key,
				get_key_offset (&target_ih->key) - unit_num);
	}
}


/* plugin->u.item.b.copy_units
   look for description of this method in plugin/item/item.h
 */
void tail_pend_units (new_coord * target, new_coord * source,
		      int from, int count,
		      shift_direction where_is_free_space, int free_space)
{
	item_header * target_ih;
	char * source_item;


	assert ("vs-177", count == free_space);

	if (target->item_pos == last_item_pos (target->node) &&
	    target->in_node.unit_pos == last_unit_pos (target) &&
	    target->between == AFTER_UNIT) {
		/* appending last item of @target */
		assert ("vs-168", 
			source->item_pos == 0 &&
			source->in_node.unit_pos == 0 &&
			source->between == AT_UNIT);
		assert ("vs-173", from == 0);
		/* make sure that free space is at the end */
		assert ("vs-174", where_is_free_space == append);

		xmemcpy (item_body_by_coord (target) + item_length_by_coord (target) - count,
			item_body_by_coord (source), count);
		return;
	}

	if (target->item_pos == 0 && target->in_node_unit_pos == 0 &&
	    target->between == BEFORE_UNIT) {
		/* prepending first item of @target */
		assert ("vs-172",
			source->item_pos == last_item_pos (source->node) &&
			source->in_node.unit_pos == last_unit_pos (source) &&
			source->between == AT_UNIT);
		assert ("vs-175", from == last_unit_pos (source) - count + 1);
		if (where_is_free_space != prepend) {
			/* free space is not prepared */
			xmemmove (item_body_by_coord (target) + count, item_body_by_coord (target),
				 item_length_by_coord (target) - count);
		}
		xmemcpy (item_body_by_coord (target), item_body_by_coord (source) + from, count);
		return;
	}

	impossible ("vs-176", "wrong target coord");
}



/* plugin->u.item.b.remove_units
   look for description of this method in plugin/item/item.h
 */
int tail_remove_units (item_coord *item, int from, int count,
		       shift_direction where_to_move_free_space)
{
	char * body;
	int length;

	length = length_by_coord (item);

	if (from + count == length && where_to_move_free_space == append)
		return count;

	if (from == 0 && where_to_move_free_space == prepend)
		return count;

	body = item_body_by_coord (item);
	xmemmove (body + from, body + from + count, length - from - count);
	if (where_to_move_free_space == prepend)
		xmemmove (body + count, body, length - count);

	return count;
}


/* plugin->u.item.b.unit_key
 */
void tail_unit_key (unit_coord * unit, reiser4_key * key)
{
	assert ("vs-110", unit->unit >= 0 && unit->unit < tail_nr_units (unit));

	*key = item_key_by_coord (unit);
	set_key_offset (key, get_key_offset (key) + unit->unit);
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
