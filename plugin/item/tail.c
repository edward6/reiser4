/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../key.h"
#include "../../tree.h"
#include "../../context.h"

#include <linux/quotaops.h>
#include <asm/uaccess.h>
#include <linux/writeback.h>

/* plugin->u.item.b.max_key_inside */
/* Audited by: green(2002.06.14) */
reiser4_key *
tail_max_key_inside(const coord_t * coord, reiser4_key * key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(max_key()));
	return key;
}

/* plugin->u.item.b.can_contain_key */
int
tail_can_contain_key(const coord_t * coord, const reiser4_key * key, const reiser4_item_data * data)
{
	reiser4_key item_key;

	if (item_plugin_by_coord(coord) != data->iplug)
		return 0;

	item_key_by_coord(coord, &item_key);
	if (get_key_locality(key) != get_key_locality(&item_key) ||
	    get_key_objectid(key) != get_key_objectid(&item_key)) return 0;

	return 1;

	assert("vs-459",
	       (coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
	       (coord->unit_pos == coord_last_unit_pos(coord) && coord->between == AFTER_UNIT));

	if (coord->between == BEFORE_UNIT) {
		if (get_key_offset(key) + data->length != get_key_offset(&item_key)) {
			printk("could not merge tail items of one file\n");
			return 0;
		} else
			return 1;

	} else {
		if (get_key_offset(key) != (get_key_offset(&item_key) + item_length_by_coord(coord))) {
			printk("could not append tail item with " "a tail item of the same file\n");
			return 0;
		} else
			return 1;
	}
}

/* plugin->u.item.b.mergeable
   first item is of tail type */
/* Audited by: green(2002.06.14) */
int
tail_mergeable(const coord_t * p1, const coord_t * p2)
{
	reiser4_key key1, key2;

	assert("vs-535", item_type_by_coord(p1) == ORDINARY_FILE_METADATA_TYPE);
	assert("vs-365", item_id_by_coord(p1) == TAIL_ID);

	if (item_id_by_coord(p2) != TAIL_ID) {
		/* second item is of another type */
		return 0;
	}

	item_key_by_coord(p1, &key1);
	item_key_by_coord(p2, &key2);
	if (get_key_locality(&key1) != get_key_locality(&key2) ||
	    get_key_objectid(&key1) != get_key_objectid(&key2) || get_key_type(&key1) != get_key_type(&key2)) {
		/* items of different objects */
		return 0;
	}
	if (get_key_offset(&key1) + tail_nr_units(p1) != get_key_offset(&key2)) {
		/* not adjacent items */
		return 0;
	}
	return 1;
}

/* plugin->u.item.b.print
   plugin->u.item.b.check */

/* plugin->u.item.b.nr_units */
unsigned
tail_nr_units(const coord_t * coord)
{
	return item_length_by_coord(coord);
}

/* plugin->u.item.b.lookup */
lookup_result
tail_lookup(const reiser4_key * key, lookup_bias bias, coord_t * coord)
{
	reiser4_key item_key;
	__u64 lookuped, offset;
	unsigned nr_units;

	item_key_by_coord(coord, &item_key);
	offset = get_key_offset(item_key_by_coord(coord, &item_key));
	nr_units = tail_nr_units(coord);

	/* key we are looking for must be greater than key of item @coord */
	assert("vs-416", keygt(key, &item_key));

	if (keygt(key, tail_max_key_inside(coord, &item_key))) {
		/* @key is key of another file */
		coord->unit_pos = nr_units - 1;
		coord->between = AFTER_UNIT;
		return CBK_COORD_NOTFOUND;
	}

	/* offset we are looking for */
	lookuped = get_key_offset(key);

	if (lookuped >= offset && lookuped < offset + nr_units) {
		/* byte we are looking for is in this item */
		coord->unit_pos = lookuped - offset;
		coord->between = AT_UNIT;
		return CBK_COORD_FOUND;
	}

	/* set coord after last unit */
	coord->unit_pos = nr_units - 1;
	coord->between = AFTER_UNIT;
	return bias == FIND_MAX_NOT_MORE_THAN ? CBK_COORD_FOUND : CBK_COORD_NOTFOUND;
}

/* plugin->u.item.b.paste */
int
tail_paste(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG)
{
	unsigned old_item_length;
	char *item;

	/* length the item had before resizing has been performed */
	old_item_length = item_length_by_coord(coord) - data->length;

	/* tail items never get pasted in the middle */
	assert("vs-363",
	       (coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
	       (coord->unit_pos == old_item_length - 1 &&
		coord->between == AFTER_UNIT) ||
	       (coord->unit_pos == 0 && old_item_length == 0 && coord->between == AT_UNIT));

	item = item_body_by_coord(coord);
	if (coord->unit_pos == 0)
		/* make space for pasted data when pasting at the beginning of
		   the item */
		xmemmove(item + data->length, item, old_item_length);

	if (coord->between == AFTER_UNIT)
		coord->unit_pos++;

	if (data->data) {
		assert("vs-554", data->user == 0 || data->user == 1);
		if (data->user) {
			assert("nikita-3035", schedulable());
			/* AUDIT: return result is not checked! */
			/* copy from user space */
			__copy_from_user(item + coord->unit_pos, data->data, (unsigned) data->length);
		} else
			/* copy from kernel space */
			xmemcpy(item + coord->unit_pos, data->data, (unsigned) data->length);
	} else {
		xmemset(item + coord->unit_pos, 0, (unsigned) data->length);
	}
	return 0;
}

/* plugin->u.item.b.fast_paste */

/* plugin->u.item.b.can_shift
   number of units is returned via return value, number of bytes via @size. For
   tail items they coincide */
int
tail_can_shift(unsigned free_space, coord_t * source UNUSED_ARG,
	       znode * target UNUSED_ARG, shift_direction direction UNUSED_ARG, unsigned *size, unsigned want)
{
	/* make sure that that we do not want to shift more than we have */
	assert("vs-364", want > 0 && want <= (unsigned) item_length_by_coord(source));

	*size = min(want, free_space);
	return *size;
}

/* plugin->u.item.b.copy_units */
void
tail_copy_units(coord_t * target, coord_t * source,
		unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space UNUSED_ARG)
{
	/* make sure that item @target is expanded already */
	assert("vs-366", (unsigned) item_length_by_coord(target) >= count);
	assert("vs-370", free_space >= count);

	if (where_is_free_space == SHIFT_LEFT) {
		/* append item @target with @count first bytes of @source */
		assert("vs-365", from == 0);

		xmemcpy((char *) item_body_by_coord(target) +
			item_length_by_coord(target) - count, (char *) item_body_by_coord(source), count);
	} else {
		/* target item is moved to right already */
		reiser4_key key;

		assert("vs-367", (unsigned) item_length_by_coord(source) == from + count);

		xmemcpy((char *) item_body_by_coord(target), (char *) item_body_by_coord(source) + from, count);

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		item_key_by_coord(source, &key);
		set_key_offset(&key, get_key_offset(&key) + from);

		node_plugin_by_node(target->node)->update_item_key(target, &key, 0	/*info */
		    );
	}
}

/* plugin->u.item.b.create_hook
   plugin->u.item.b.kill_hook
   plugin->u.item.b.shift_hook */

/* plugin->u.item.b.cut_units
   plugin->u.item.b.kill_units */
int
tail_cut_units(coord_t * coord, unsigned *from, unsigned *to,
	       const reiser4_key * from_key UNUSED_ARG,
	       const reiser4_key * to_key UNUSED_ARG, reiser4_key * smallest_removed)
{
	reiser4_key key;
	unsigned count;

	count = *to - *from + 1;
	/* regarless to whether we cut from the beginning or from the end of
	   item - we have nothing to do */
	assert("vs-374", count > 0 && count <= (unsigned) item_length_by_coord(coord));
	/* tails items are never cut from the middle of an item */
	assert("vs-396", ergo(*from != 0, *to == coord_last_unit_pos(coord)));

	if (smallest_removed) {
		/* store smallest key removed */
		item_key_by_coord(coord, smallest_removed);
		set_key_offset(smallest_removed, get_key_offset(smallest_removed) + *from);
	}
	if (*from == 0) {
		/* head of item is removed, update item key therefore */
		item_key_by_coord(coord, &key);
		set_key_offset(&key, get_key_offset(&key) + count);
		node_plugin_by_node(coord->node)->update_item_key(coord, &key, 0 /*info */ );
	}

	if (REISER4_DEBUG)
		xmemset((char *) item_body_by_coord(coord) + *from, 0, count);
	return count;
}

/* plugin->u.item.b.unit_key */
reiser4_key *
tail_unit_key(const coord_t * coord, reiser4_key * key)
{
	assert("vs-375", coord_is_existing_unit(coord));

	item_key_by_coord(coord, key);
	set_key_offset(key, (get_key_offset(key) + coord->unit_pos));

	return key;
}

/* plugin->u.item.b.estimate
   plugin->u.item.b.item_data_by_flow */

/* item_plugin->common.real_max_key_inside */
reiser4_key *
tail_max_key(const coord_t * coord, reiser4_key * key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(key) + item_length_by_coord(coord) - 1);
	return key;
}

/* item_plugin->common.key_in_item */
int
tail_key_in_item(coord_t * coord, const reiser4_key * key)
{
	reiser4_key item_key;

	assert("vs-778", coord_is_existing_item(coord));

	if (keygt(key, tail_max_key(coord, &item_key))) {
		/* key > max key of item */
		if (get_key_offset(key) == get_key_offset(&item_key) + 1) {
			coord->unit_pos = tail_nr_units(coord) - 1;
			coord->between = AFTER_UNIT;
			return 1;
		}
		
		return 0;
	}

	/* key of first byte pointed by item */
	item_key_by_coord(coord, &item_key);
	if (keylt(key, &item_key))
		/* key < min key of item */
		return 0;

	coord->unit_pos = get_key_offset(key) - get_key_offset(&item_key);
	coord->between = AT_UNIT;
	return 1;
}

/* overwrite tail item or its part by use data */
static int
overwrite_tail(coord_t * coord, flow_t * f)
{
	unsigned count;

	assert("vs-570", f->user == 1);
	assert("vs-946", f->data);
	assert("vs-947", coord_is_existing_unit(coord));
	assert("vs-948", znode_is_write_locked(coord->node));
	assert("nikita-3036", schedulable());

	count = item_length_by_coord(coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	if (__copy_from_user((char *) item_body_by_coord(coord) + coord->unit_pos, f->data, count))
		return -EFAULT;

	znode_set_dirty(coord->node);

	move_flow_forward(f, count);
	return 0;
}

/* drop longterm znode lock before calling balance_dirty_pages. balance_dirty_pages may cause transaction to close,
   therefore we have to update stat data if necessary */
static int tail_balance_dirty_pages(struct address_space *mapping, const flow_t *f, coord_t *coord, lock_handle *lh)
{
	int result;
	struct sealed_coord hint;
	loff_t new_size;
	
	set_hint(&hint, &f->key, coord);
	done_lh(lh);
	coord->node = 0;
	new_size = get_key_offset(&f->key);
	result = update_inode_and_sd_if_necessary(mapping->host, new_size, (new_size > mapping->host->i_size) ? 1 : 0);
	if (result)
		return result;

	/* FIXME-VS: this is temporary: the problem is that bdp takes inodes from sb's dirty list and it looks like
	   nobody puts there inodes of files which are built of tails */
	mapping->dirtied_when = jiffies|1;
	spin_lock(&inode_lock);
	list_move(&mapping->host->i_list, &mapping->host->i_sb->s_dirty);
	spin_unlock(&inode_lock);

	balance_dirty_pages_ratelimited(mapping);
	return hint_validate(&hint, &f->key, coord, lh);
}

/* calculate number of blocks which can be dirtied/added when flow is inserted and stat data gets updated and grab them.
   FIXME-VS: we may want to call grab_space with BA_CAN_COMMIT flag but that would require all that complexity with
   sealing coord, releasing long term lock and validating seal later */
static int
insert_flow_reserve(tree_level height)
{
	grab_space_enable();
	return reiser4_grab_space(estimate_insert_flow(height) + estimate_one_insert_into_item(height), 0, "for insert_flow");
}

/* one block gets overwritten and stat data may get updated */
static int
overwrite_reserve(tree_level height)
{
	grab_space_enable();
	return reiser4_grab_space(1 + estimate_one_insert_into_item(height), 0, "overwrite_reserve");
}

/* plugin->u.item.s.file.write
   access to data stored in tails goes directly through formatted nodes */
int
tail_write(struct inode *inode, coord_t *coord, lock_handle *lh, flow_t * f)
{
	int result;
	write_mode todo;
	znode *loaded;

	result = 0;
	while (f->length && !result) {
		/* zload is necessary because balancing may return coord->node moved to another possibly not loaded
		   node. Store what we loaded so that we will be able to zrelse it */
		result = zload(coord->node);
		if (result)
			return result;
		loaded = coord->node;

		todo = how_to_write(coord, lh, &f->key);
		if (unlikely(todo < 0)) {
			zrelse(loaded);
			return todo;
		}

		switch (todo) {
		case FIRST_ITEM:
		case APPEND_ITEM:
			/* check quota before appending data */
			if (DQUOT_ALLOC_SPACE_NODIRTY(inode, f->length)) {
				result = -EDQUOT;
				break;
			}

			result = insert_flow_reserve(znode_get_tree(loaded)->height);
			if (!result)
				result = insert_flow(coord, lh, f);
			if (f->length)
				DQUOT_FREE_SPACE_NODIRTY(inode, f->length);
			break;

		case OVERWRITE_ITEM:
			result = overwrite_reserve(znode_get_tree(loaded)->height);
			if (!result)
				result = overwrite_tail(coord, f);
			break;

		case RESEARCH:
			/* FIXME-VS:  */
			result = -EAGAIN;
			break;
		default:
			impossible("vs-1031", "does this ever happen?");
			result = -EIO;
			break;

		}
		zrelse(loaded);

		if (result) {
			all_grabbed2free("tail_write: on error");
			break;
		}
		/* throttle the writer */
		result = tail_balance_dirty_pages(inode->i_mapping, f, coord, lh);
		all_grabbed2free("tail_write");
		if (result) {
			// reiser4_stat_tail_add(bdp_caused_repeats);
			break;
		}
	}

	return result;
}

/* plugin->u.item.s.file.read */
int
tail_read(struct file *file UNUSED_ARG, coord_t *coord, flow_t * f)
{
	unsigned count;

	assert("vs-571", f->user == 1);
	assert("vs-571", f->data);
	assert("vs-967", coord && coord->node);
	assert("vs-1117", znode_is_rlocked(coord->node));
	assert("vs-1118", znode_is_loaded(coord->node));

	assert("nikita-3037", schedulable());
	if (!tail_key_in_item(coord, &f->key))
		return -EAGAIN;

	/* calculate number of bytes to read off the item */
	count = item_length_by_coord(coord) - coord->unit_pos;
	if (count > f->length)
		count = f->length;

	if (__copy_to_user(f->data, ((char *) item_body_by_coord(coord) + coord->unit_pos), count))
		return -EFAULT;

	/* probably mark_page_accessed() should only be called if
	 * coord->unit_pos is zero. */
	mark_page_accessed(znode_page(coord->node));
	move_flow_forward(f, count);

	return 0;
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
