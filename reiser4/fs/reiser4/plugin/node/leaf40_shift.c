/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

#include "defines.h"


struct shift_params {
	append_prepend pend; /* when @pend == append - we are shifting
				to left, when @pend == prepend - to
				right */
	tween_coord wish_stop; /* when shifting to left this is last unit we
				  want shifted, when shifting to right - this
				  is set to unit we want to start shifting
				  from */
	znode * target;

	/* these are set by estimate_shift */
	tween_coord real_stop; /* this will be set to last unit which will be
				  really shifted */
	int merging_units; /* number of units of first item which have to be
			      merged with last item of target node */
	int merging_bytes; /* number of bytes in those units */

	int entire; /* items shifted in their entirety */
	int entire_bytes; /* number of bytes in those items */

	int part_units; /* number of units of partially copied item */
	int part_bytes; /* number of bytes in those units */

	int shift_bytes; /* total number of bytes in items shifted
			    (item headers not included) */
	
};


static int item_creation_overhead (tween_coord * item)
{
	return node_plugin_by_coord (item) -> item_overhead ();
}


/* how many units of @source do we want to copy if we do not want to shift
   further than @stop_point */
int wanted_units (tween_coord * source, tween_coord * stop_point,
		  append_prepend pend)
{
	if (source->item_pos != stop_point->item_pos)
		/* @source item is not stop item, so we want all its units
		   shifted */
		return last_unit_pos (source) + 1;

	if (pend == append) {
		assert ("vs-181", source->in_item.unit_pos == 0);
		return stop_point->in_item.unit_pos + 1;
	} else {
		assert ("vs-182", source->in_item.unit_pos == last_unit_pos (stop_point));
		return source->in_item.unit_pos - stop_point->in_item.unit_pos + 1;
	}
}


/* this calculates what can be copied from @shift->wish_stop.node to
   @shift->target */
static void leaf40_estimate_shift (struct shift_params * shift)
{
	int target_free_space, size;
	int item_overhead;
	int stop_item; /* item which estimating should not consider */
	int want; /* number of units of item we want shifted */
	tween_coord source; /* item being estimated */


	/* shifting to left/right starts from first/last units of
	   @shift->wish_stop.node */
	source.node = shift->wish_stop.node;
	(shift->pend == append) ? coord_first_unit (&source) : 
		coord_last_unit (&source);


	/* free space in target node and number of items in source */
	target_free_space = znode_free_space (shift->target);


	if (!is_empty_node (shift->target)) {
		/* target node is not empty, check for boundary items
		   mergeability */
		tween_coord to;

		/* item we try to merge @source with */
		to.node = shift->target;
		(shift->pend == append) ? coord_last_unit (&to) : 
			coord_first_unit (&to);

		if (are_items_mergeable (&source, &to)) {
			/* how many units of @source do we want to merge to
			   item @to */
			want = wanted_units (&source, &shift->wish_stop, shift->pend);

			/* how many units of @source we can merge to item
			   @to */
			shift->merging_units = item_op_can_shift (target_free_space, &source,
								  shift->target, shift->pend, &size,
								  want);
			shift->merging_bytes = size;
			shift->shift_bytes += size;
			/* update stop point to be set to last unit of @source
			   we can merge to @target */
			shift->real_stop = source;
			shift->real_stop.in_item.unit_pos = (shift->merging_units - source.in_item.unit_pos - 1) * shift->pend;

			if (shift->merging_units != want) {
				/* we could not copy as many as we want, so,
				   there is no reason for estimating any
				   longer */
				return;
			}

			target_free_space -= size;
			source.item_pos += shift->pend;
		}
	}


	/* number of item nothing of which we want to shift */
	stop_item = shift->wish_stop.item_pos + shift->pend;

	/* calculate how many items can be copied into given free
	   space as whole */
	for (; source.item_pos != stop_item; source.item_pos += shift->pend) {
		if (shift->pend == prepend)
			source.in_item.unit_pos = last_unit_pos (&source);

		/* how many units of @source do we want to copy */
		want = wanted_units (&source, &shift->wish_stop, shift->pend);

		if (want == last_unit_pos (&source) + 1) {
			/* we want this item to be copied entirely */
			size = item_length_by_coord (&source) + item_creation_overhead (&source);
			if (size <= target_free_space) {
				/* item fits into target node as whole */
				target_free_space -= size;
				shift->shift_bytes += size;
				shift->entire_bytes = size;
				shift->entire ++;
				
				/* update shift->real_stop point to be set to
				   last unit of @source we can merge to
				   @target */
				(shift->pend == append) ? coord_last_unit (&shift->real_stop) : 
					coord_first_unit (&shift->real_stop);
				continue;
			}
		}

		/* we reach here only for an item which does not fit into
		   target node in its entirety. This item may be either
		   partially shifted, or not shifted at all. We will have to
		   create new item in target node, so decrease amout of free
		   space by an item creation overhead. We can reach here also
		   if stop point is in this item */
		target_free_space -= item_creation_overhead (&source);

		shift->part_units = item_op_can_shift (target_free_space, &source,
						       0/*target*/, shift->pend, &size, want);
		shift->part_bytes = size;
		shift->shift_bytes += size;

		/* update stop point to be set to last unit of @source we can
		   merge to @target */
		shift->real_stop = source;
		shift->real_stop.in_item.unit_pos = (shift->part_units - source.in_item.unit_pos - 1) * shift->pend;
		break;
	}
}

#if 0
/* copy part of @shift->real_stop.node starting either from its beginning or
   from its end and ending at @shift->real_stop to either the end or the
   beginning of @shift->target */
void leaf40_copy (struct shift_params * shift)
{
	tween_coord from;
	tween_coord to;
	item_header * from_ih, * to_ih;
	int free_space_start;
	int new_items;
	int old_items;
	int size;
	int old_offset;


	free_space_start = leaf40_get_free_space_start (shift->target);
	old_items = leaf40_get_num_items (shift->target);
	new_items = shift->entirely + part ? 1 : 0;
	assert ("vs-185",
		shift->shift_bytes ==
		shift->merging_bytes + shift->entire_bytes + shift->part_bytes);

	from = *shift->wish_stop;

	to.node = shift->target;
	if (pend == append) {
		/* copying to left */

		from.item_pos = 0;
		from_ih = item_head_by_node_and_pos (from.node, 0);

		to_node.item_pos = last_item_pos (to_node);
		if (merging_units) {
			/* appending last item of @target */
			item_plugin_by_coord (&from)->pend_units (@to, &from,
								  0, 
								  merging_units,
								  append, shift->merging_bytes);
			from.item_pos ++;
			from_ih --;
			to.item_pos ++;
		}

		to_ih = item_head_by_node_and_pos (target, old_items);
		if (entire) {
			/* copy @entire items entirely */

			/* copy item headers */
			memcpy (to_ih - entirely + 1, from_ih - entirely + 1,
				entire * LEAF40_ITEM_HEAD_SIZE);
			/* update item header offset */
			old_offset = leaf40_item_offset (from_ih);
			for (i = 0; i < entire; i ++, to_ih --, from_ih --)
				leaf40_set_item_offset (to_ih,
							leaf40_item_offset (from_ih) - old_offset + free_space_start);

			/* copy item bodies */
			memcpy (shift->target->data + free_space_start + merging_bytes,
				from.node->data + leaf40_item_offset (from_ih),
				entire_bytes);

			from.item_pos -= entire;
			to.item_pos += entire;
		}

		if (part_units) {
			/* copy heading part (@part units) of @source item as
			   a new item into @target->node */

			/* copy item header of partially copied item */
			memcpy (to_ih, from_ih, LEAF40_ITEM_HEAD_SIZE);
			leaf40_set_item_offset (to_ih, leaf40_item_offset (from_ih) - old_offset + free_space_start);
			free_space += item_plugin_by_coord (&to)->pend_units (&to, &from, 0, part,
									      pend, shift->part_bytes);
		}
	} else {
		/* copying to right */

		from.item_pos = last_item_pos (source->node);
		from_ih = item_head_by_node_and_pos (from.node, from.item_pos);

		/* prepare space for new items */
		memmove (target->node->data + sizeof (node_header_40) + size,
			 target->node->data + sizeof (node_header_40),
			 free_space_start - sizeof (node_header_40));
		/* update item headers of moved items */
		to_ih = item_head_by_node_and_pos (to.node, 0);
		for (i = 0; i < old_items; i ++)
			leaf40_set_item_offset (to_ih + i,
						leaf40_item_offset (to_ih + i) + size);
		/* first item gets @merging_bytes longer. free space appears
		   at its beginning */
		leaf40_set_item_offset (to_ih,
					leaf40_item_offset (to_ih) - merging_bytes);

		/* move item headers to make space for new items */
		memmove (to_ih - old_items + 1 - new_items, to_ih - old_items + 1,
			 LEAF40_ITEM_HEAD_SIZE * old_items);
		to_ih -= (new_items - 1);

		if (merging_units) {
			/* prepend first item of @target */
			item_plugin_by_coord (&to)->pend_units (target, &from,
								last_unit_pos (&from) - merging_units + 1,
								merging_units,
								pend, merging_bytes);
			from.item_pos --;
			from_ih ++;
		}

		if (entire) {
			/* copy @entire items entirely */

			/* copy item headers */
			memcpy (to_ih, from_ih, entire * LEAF40_ITEM_HEAD_SIZE);

			/* update item header offset */
			old_offset = leaf40_item_offset (from_ih);
			for (i = 0; i < entire; i ++, to_ih ++, from_ih ++)
				leaf40_set_item_offset (to_ih,
							leaf40_item_offset (from_ih) - old_offset +
							sizeof (node_header_40) + part_bytes);
			/* copy item bodies */
			memcpy (target->node->data + sizeof (node_header_40) + part_bytes,
				item_body_by_coord (&from), entire_bytes);

			from.item_pos -= entire;
		}

		if (part_units) {
			/* copy heading part (@part units) of @source item as
			   a new item into @target->node */
			
			/* copy item header of partially copied item */
			memcpy (to_ih, from_ih, LEAF40_ITEM_HEAD_SIZE);
			leaf40_set_item_offset (to_ih, sizeof (node_header_40));
			item_plugin_by_coord (&to)->pend_units (&to, &from,
								last_unit_pos (&from) - part_units + 1, part_uints, 
								pend, part_bytes);
		}
	}

	/* update node header */
	leaf40_set_num_items (target->node, old_items + new_items);
	leaf40_set_free_space_start (target->node, free_space_start + size);
	leaf40_set_free_space (target->node, leaf40_get_free_space (target->node) -
			       (size + LEAF40_ITEM_HEAD_SIZE * new_items));
	assert ("vs-170", leaf40_get_free_space (target->node) >= 0);
}

#if 0
/* cut part of node between @from and @to. return value is number of items
   removed in their entirety */
static int leaf40_cut (tween_coord * from, tween_coord * to)
{
	int want;
	int from_free; /* how many bytes were freed cutting item @from */
	int to_free; /* .. from item @to */
	item_header * ih;
	int new_from_end;
	int new_to_start;
	int first_removed; /* position of first item removed entirely */
	int count; /* number of items removed entirely */


	assert ("vs-184", from->node == to->node);

	first_removed = from->item_pos;
	count = to->item_pos - from->item_pos + 1;

	from_free = 0;
	to_free = 0;
	if (from->in_item.unit_pos != 0) {
		/* @from item has to be cut partially */
		want = wanted_units (from, to, append);
		/* remove @want units starting from @from->in_item.unit_pos
		   one of item @from and move freed space to the right */
		from_free = item_plugin_by_coord (from)->remove_units (from, from->in_item.unit_pos, want, append);
		first_removed ++;
		count --;
	}

	if (to->in_item.unit_pos != last_unit_pos (to)) {
		/* @to item has to be cut partially */
		if (from->item_pos != to->item_pos || from->in_item.uint_pos == 0) {
			/* @to item is cut partially and it is different item
			   than @from. remove units from the beginning of @to
			   and move free space to the left */
			to_free = item_plugin_by_coord (from)->remove_units (from, 0, to->in_item.unit_pos + 1, prepend);
			count --;
		} else {
			/* it has been cut already as @from and @to are set to
			   the same item and as @from was cut already */
		}
	}

	/* where does @from item end now */
	ih = item_head_by_node_and_pos (from->node, from->item_pos);
	new_from_end = leaf40_item_offset (ih) + item_length_by_coord (from) - from_free;

	/* where does @to start now */
	ih = item_head_by_node_and_pos (from->node, to->item_pos);
	if (to->in_item.unit_pos == last_unit_pos (to)) {
		if (to->item_pos == last_item_pos (to->node))
			new_to_start = leaf40_get_free_space_start (to->node);
		else
			new_to_start = leaf40_item_offset (ih - 1);
	} else
		new_to_start = leaf40_item_offset (ih) + to_free;

	/* move remaining data to left */
	memmove (from->node->data + new_from_end,
		 from->node->data + new_to_start,
		 leaf40_get_free_space_start (to->node) - new_to_start);

	/* update their item headers */
	i = first_removed + count;
	ih = item_head_by_node_and_pos (from->node, i);
	for (; i <= last_item_pos (to->node); i ++, ih --) {
		leaf40_set_item_offset (ih,
					leaf40_item_offset (ih) - (new_to_start - new_from_end));
	}

	/* cut item headers of removed items */
	ih = item_head_by_node_and_pos (from->node, first_removed) + 1;
	memmove (ih, ih - count, LEAF40_ITEM_HEAD_SIZE * (last_item_pos (to->node) + 1 -
							  count - first_removed));
	
	/* update node header */
	leaf40_set_num_items (to->node, last_item_pos (to->node) + 1 - count);
	leaf40_set_free_space_start (to->node, leaf40_get_free_space_start (to->node) - (new_to_start - new_from_end));
	leaf40_set_free_space (to->node, leaf40_get_free_space (to->node) +
			       ((new_to_start - new_from_end) + LEAF40_ITEM_HEAD_SIZE * count));

	return count;
}
#endif


/* remove everything either before or after @fact_stop. Number of items
   removed completely is returned */
static int leaf40_delete_copied (struct shift_params * shift)
{
	tween_coord from;
	tween_coord to;


	if (shift->pend == append) {
		/* we were shifting to left, remove everything from the
		   beginning of @shift->wish_stop->node upto
		   @shift->wish_stop */
		from.node = shift->real_stop->node;
		from.item_pos = 0;
		from.in_node = 0;
		from.between = at_unit;
		to = shift->real_point;
	} else {
		/* we were shifting to right, remove everything from
		   @shift->stop_point upto to end of
		   @shift->stop_point->node */
		from = shift->real_point;
		to.node = from->node;
		to.item_pos = last_item_pos (to.node);
		to.in_node.unit_pos = last_unit_pos (&to);
		to.between = at_unit;
	}

	return leaf40_cut (&from, &to, 0);
}


/* znode has left and right delimiting keys. We moved data between nodes,
   therefore we must update delimiting keys of those znodes */
static void update_znode_dkeys (znode * left, znode * right)
{
	reiser4_key key;
	tween_coord first_item;


	if (!is_empty_node (left) && !is_empty_node (right)) {
		first_item.node = right->node;
		first_item.item_pos = 0;
		item_key_by_coord (&first_item, &key);
		
		/* update right delimiting key of @left */
		znode_get_rd_key (left) = key;
		/* update left delimiting key of @right */
		znode_get_ld_key (right) = key;
		return;
	}

	if (is_empty_node (left)) {
		assert ("vs-186", !is_empty_node (right));

		/* update right delimiting key of @left */
		*znode_get_rd_key (left) = *znode_get_ld_key (left);

		/* update left delimiting key of @right */
		*znode_get_ld_key (right) = *znode_get_ld_key (left);
		return;
	}

	if (is_empty_node (right)) {
		assert ("vs-187", !is_empty_node (left));

		/* update right delimiting key of @left */
		*znode_get_rd_key (left) = *znode_get_rd_key (right);

		/* update left delimiting key of @right */
		*znode_get_ld_key (right) = *znode_get_rd_key (right);
		return;
	}
	impossible ("vs-188", "both nodes can not be empty");
}


static int prepare_for_update (znode * left, znode * right, carry_op_list * todo)
{
	carry_node * cn;
	carry_op * op;


	cn = reiser4_add_carry (&todo->nodes, cno_last, 0);
	if (IS_ERR (cn))
		return PTR_ERR (cn);

	cn->node = to->node;
	cn->parent = 1;
	cn->lock = znode_write_lock;

	op = reiser4_add_cop (todo, copo_last, 0);
	if (IS_ERR (op))
		return PTR_ERR (op);

	op->node = left;
	op->op = cop_update;
	/*op->u.update.left = left;*/
	op->u.update.right = right;
	op->u.update.key = *znode_get_ld_key (right);
	return 0;
}


/* when we have shifted everything from @empty - we have to update
   delimiting key first and then cut the pointer */
static int prepare_for_removal (znode * empty, znode * node,
				append_prepend pend,
				carry_op_list * todo)
{
	carry_node * cn;
	carry_op * op;


	if (pend == prepend) {
		/* we shifted everything to right */

		/* @empty was left neighbor of @node, we are going to delete
		   pointer to it, that will also remove left delimiting key of
		   @empty node, so we have to save that key first by updating
		   delimiting keys between @empty and @node */
		cn = reiser4_add_carry (&todo->nodes, cno_last, 0);
		if (IS_ERR (cn))
			return PTR_ERR (cn);
		cn->node = empty;
		cn->parent = 1;
		cn->lock = znode_write_lock;

		op = reiser4_add_cop (todo, copo_last, 0);
		if (IS_ERR (op))
			return PTR_ERR (op);
		op->op = cop_update;
		op->node = empty;
		/*op->u.update.left = empty;*/
		op->u.update.right = node;
		op->u.update.key = *znode_get_ld_key (empty);
	}

	/* prepare for deletion of pointer to @empty node from its parent */
	cn = reiser4_add_carry (&todo->nodes, cno_last, 0);
	if (IS_ERR (cn))
		return PTR_ERR (cn);
	cn->node = empty;
	cn->parent = 1;
	cn->lock = znode_write_lock;

	op = reiser4_add_cop (todo, copo_last, 0);
	if (IS_ERR (op))
		return PTR_ERR (op);

	op->op = cop_delete;
	op->u.delete.child = empty;
	return 0;
}
#endif

/* something were shifted from @insert_point->node to @shift->target, update
   @insert_coord correspondingly */
static void adjust_coord (tween_coord * insert_point, 
			  struct shift_params * shift,
			  int removed, int including_insert_point)
{
	if (shift->pend == prepend) {
		/* there was shifting to right */
		if (compare_coords (&shift->real_stop, &shift->wish_stop) == same) {
			/* everything wanted was shifted */
			if (including_insert_point) {
				/* @insert_point is set before first unit of
				   @to node */
				insert_point->node = shift->target;
				coord_first_unit (insert_point);
				insert_point->between = before_unit;
			} else {
				/* @insert_point is set after last unit of
				   @insert->node */
				coord_last_unit (insert_point);
				insert_point->between = after_unit;
			}
		}
		return;
	}

	/* there was shifting to left */
	if (compare_coords (&shift->real_stop, &shift->wish_stop) == same) {
		/* everything wanted was shifted */
		if (including_insert_point) {
			/* @insert_point is set after last unit in @to node */
			insert_point->node = shift->target;
			coord_last_unit (insert_point);
			insert_point->between = after_unit;
		} else {
			/* @insert_point is set before first unit in the same
			   node */
			coord_first_unit (insert_point);
			insert_point->between = before_unit;
		}
		return;
	}

	if (!removed) {
		/* no items were shifted entirely */
		assert ("vs-195", shift->merging_units == 0 ||
			shift->part_units == 0);
		if (shift->real_stop.item_pos == insert_point->item_pos) {
			if (shift->merging_units)
				insert_point->in_item.unit_pos -= shift->merging_units;
			else
				insert_point->in_item.unit_pos -= shift->part_units;
		}
		return;
	}
	if (shift->real_stop.item_pos == insert_point->item_pos)
		insert_point->in_item.unit_pos -= shift->part_units;
	insert_point->item_pos -= removed;
}


/* plugin->u.node.shift
   look for description of this method in plugin/node/node.h
 */
int leaf40_shift (tween_coord * from, znode * to, carry_op_list * todo,
		  append_prepend pend,
		  int delete_child, /* if @from->node becomes empty - it will
				       be deleted from the tree if this is set
				       to 1 */
		  int including_stop_point /* */)
{
	struct shift_params shift;
	int result;


	memset (&shift, 0, sizeof (shift));
	shift.pend = pend;
	shift.wish_stop = *from;
	shift.target = to;


	/* set @shift.wish_stop to rightmost/leftmost unit among units we want
	   shifted */
	result = ((pend == append) ? coord_set_to_left (&shift.wish_stop) :
		  coord_set_to_right (&shift.wish_stop));
	if (result)
		/* there is nothing to shift */
		return 0;


	/* when first node plugin with item body compression is implemented,
	   this must be changed to call node specific plugin */

	/* shift->stop_point is updated to last unit which really will be
	   shifted */
	leaf40_estimate_shift (&shift);

	leaf40_copy (&shift);

	result = leaf40_delete_copied (&shift);

	/* adjust @from pointer in accordance with @including_stop_point flag
	   and amount of data which was really shifted */
	adjust_coord (from, &shift, result, including_stop_point);

	/* update delimiting key of znodes */
	update_znode_dkeys (pend == append ? to : from->node,
			    pend == append ? from->node : to);

	/* provide info for tree_carry to continue */
	if (is_empty_node (from->node) && delete_child) {
		/* all contents of @from->node is moved to @to and @from->node
		   has to be removed from the tree, so, on higher level we
		   will be removing the pointer to node @to */
		result = prepare_for_removal (from->node, to, pend, todo);
	} else {
		/* put data into @todo for delimiting key updating */
		result = prepare_for_update (pend == append ? to : from->node,
					     pend == append ? from->node : to,
					     todo);
	}
	printf ("SHIFT: merging %d, entire %d, part %d, size %d\n",
		shift.merging_units, shift.entire, shift.part_units, shift.shift_bytes);
	return result ? result : shift.shift_bytes;
}

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
