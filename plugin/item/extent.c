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

#define HOLE_EXTENT_START zero
#define UNALLOCATED_EXTENT_START one

#if REISER4_DEBUG
static int
coord_extension_is_ok(const uf_coord_t *uf_coord)
{	
	const coord_t *coord;
	const extent_coord_extension_t *ext_coord;

	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;
	
	return WITH_DATA(coord->node, (uf_coord->valid == 1 &&
				       coord_is_iplug_set(coord) &&
				       item_is_extent(coord) &&
				       ext_coord->nr_units == nr_units_extent(coord) &&
				       ext_coord->ext == extent_by_coord(coord) &&
				       ext_coord->width == extent_get_width(ext_coord->ext) &&
				       coord->unit_pos < ext_coord->nr_units &&
				       ext_coord->pos_in_unit < ext_coord->width &&
				       extent_get_start(ext_coord->ext) == extent_get_start(&ext_coord->extent) &&
				       extent_get_width(ext_coord->ext) == extent_get_width(&ext_coord->extent)));
}
#endif

/* prepare structure reiser4_item_data to put one extent unit into tree */
/* Audited by: green(2002.06.13) */
static reiser4_item_data *
init_new_extent(reiser4_item_data *data, void *ext_unit, int nr_extents)
{
	if (REISER4_ZERO_NEW_NODE)
		memset(data, 0, sizeof (reiser4_item_data));

	data->data = ext_unit;
	/* data->data is kernel space */
	data->user = 0;
	data->length = sizeof (reiser4_extent) * nr_extents;
	data->arg = 0;
	data->iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	return data;
}

/* how many bytes are addressed by @nr first extents of the extent item */
static reiser4_block_nr
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

/* plugin->u.item.b.max_key_inside */
reiser4_key *
max_key_inside_extent(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(max_key()));
	return key;
}

/* plugin->u.item.b.can_contain_key
   this checks whether @key of @data is matching to position set by @coord */
int
can_contain_key_extent(const coord_t *coord, const reiser4_key *key, const reiser4_item_data *data)
{
	reiser4_key item_key;

	if (item_plugin_by_coord(coord) != data->iplug)
		return 0;

	item_key_by_coord(coord, &item_key);
	if (get_key_locality(key) != get_key_locality(&item_key) ||
	    get_key_objectid(key) != get_key_objectid(&item_key) ||
	    get_key_ordering(key) != get_key_ordering(&item_key)) return 0;

	return 1;
}

/* plugin->u.item.b.mergeable
   first item is of extent type */
/* Audited by: green(2002.06.13) */
int
mergeable_extent(const coord_t *p1, const coord_t *p2)
{
	reiser4_key key1, key2;

	assert("vs-299", item_id_by_coord(p1) == EXTENT_POINTER_ID);
	/* FIXME-VS: Which is it? Assert or return 0 */
	if (item_id_by_coord(p2) != EXTENT_POINTER_ID) {
		return 0;
	}

	item_key_by_coord(p1, &key1);
	item_key_by_coord(p2, &key2);
	if (get_key_locality(&key1) != get_key_locality(&key2) ||
	    get_key_objectid(&key1) != get_key_objectid(&key2) || 
	    get_key_ordering(&key1) != get_key_ordering(&key2) || 
	    get_key_type(&key1) != get_key_type(&key2))
		return 0;
	if (get_key_offset(&key1) + extent_size(p1, nr_units_extent(p1)) != get_key_offset(&key2))
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

static extent_state
state_of_extent(reiser4_extent *ext)
{
	switch ((int) extent_get_start(ext)) {
	case 0:
		return HOLE_EXTENT;
	case 1:
		return UNALLOCATED_EXTENT;
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

#if REISER4_DEBUG_OUTPUT
/* plugin->u.item.b.print */
/* Audited by: green(2002.06.13) */
static const char *
state2label(extent_state state)
{
	const char *label;

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
	assert("vs-376", label);
	return label;
}

void
print_extent(const char *prefix, coord_t *coord)
{
	reiser4_extent *ext;
	unsigned i, nr;

	if (prefix)
		printk("%s:", prefix);

	nr = nr_units_extent(coord);
	ext = (reiser4_extent *) item_body_by_coord(coord);

	printk("%u: ", nr);
	for (i = 0; i < nr; i++, ext++) {
		printk("[%Lu (%Lu) %s]", extent_get_start(ext), extent_get_width(ext), state2label(state_of_extent(ext)));
	}
	printk("\n");
}

#endif

void
show_extent(struct seq_file *m, coord_t *coord)
{
	reiser4_extent *ext;
	ext = extent_by_coord(coord);
	seq_printf(m, "%Lu %Lu", extent_get_start(ext), extent_get_width(ext));
}

#if REISER4_TRACE
static void
print_ext_coord(const char *s, uf_coord_t *uf_coord)
{
	reiser4_key key;
	extent_coord_extension_t *ext_coord;

	item_key_by_coord(&uf_coord->base_coord, &key);
	ext_coord = &uf_coord->extension.extent;
	printk("%s: item key [%llu, %llu], nr_units %d, cur extent [%llu, %llu], unit_pos %d, pos_in_unit %Lu\n",
	       s, get_key_objectid(&key), get_key_offset(&key),
	       ext_coord->nr_units,
	       extent_get_start(ext_coord->ext), extent_get_width(ext_coord->ext),
	       uf_coord->base_coord.unit_pos, ext_coord->pos_in_unit);
}
#endif

/* plugin->u.item.b.nr_units */
pos_in_item_t
nr_units_extent(const coord_t *coord)
{
	/* length of extent item has to be multiple of extent size */
#if REISER4_DEBUG
	if ((item_length_by_coord(coord) % sizeof (reiser4_extent)) != 0)
		reiser4_panic("vs-10", "assertion failed: (item_length_by_coord(coord) %% sizeof (reiser4_extent)) != 0");
#endif
	return item_length_by_coord(coord) / sizeof (reiser4_extent);
}

/* plugin->u.item.b.lookup */
lookup_result
lookup_extent(const reiser4_key *key, lookup_bias bias UNUSED_ARG, coord_t *coord)
{				/* znode and item_pos are
				   set to an extent item to
				   look through */
	reiser4_key item_key;
	reiser4_block_nr lookuped, offset;
	unsigned i, nr_units;
	reiser4_extent *ext;
	unsigned blocksize;
	unsigned char blocksize_bits;

	item_key_by_coord(coord, &item_key);
	offset = get_key_offset(&item_key);

	/* key we are looking for must be greater than key of item @coord */
	assert("vs-414", keygt(key, &item_key));

	if (keygt(key, max_key_inside_extent(coord, &item_key))) {
		/* @key is key of another file */
		coord->unit_pos = 0;
		coord->between = AFTER_ITEM;
		return CBK_COORD_NOTFOUND;
	}

	ext = extent_item(coord);
	assert("vs-1350", ext == coord->body);

	blocksize = current_blocksize;
	blocksize_bits = current_blocksize_bits;

	/* offset we are looking for */
	lookuped = get_key_offset(key);

	nr_units = nr_units_extent(coord);
	/* go through all extents until the one which address given offset */
	for (i = 0; i < nr_units; i++, ext++) {
		offset += (extent_get_width(ext) << blocksize_bits);
		if (offset > lookuped) {
			/* desired byte is somewhere in this extent */
			coord->unit_pos = i;
			coord->between = AT_UNIT;
			return CBK_COORD_FOUND;
		}
	}

	/* set coord after last unit */
	coord->unit_pos = nr_units - 1;
	coord->between = AFTER_UNIT;
	return CBK_COORD_FOUND;
}

/* set extent's start and width */
static void
set_extent(reiser4_extent *ext, reiser4_block_nr start, reiser4_block_nr width)
{
	extent_set_start(ext, start);
	extent_set_width(ext, width);
}

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
	}

	if (node) {
		znode_set_rd_key(node, item_key_by_coord(coord, &key));

		/* break sibling links */
		if (ZF_ISSET(node, JNODE_RIGHT_CONNECTED) && node->right) {
			/*ZF_CLR (node->right, JNODE_LEFT_CONNECTED); */
			node->right->left = NULL;
			/*ZF_CLR (node, JNODE_RIGHT_CONNECTED); */
			node->right = NULL;
		}
	}
	WUNLOCK_TREE(tree);
	WUNLOCK_DK(tree);
	return 0;
}

/* check inode's list of eflushed jnodes and drop those which correspond to this extent */
static void
drop_eflushed_nodes(struct inode *inode, unsigned long index)
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
		if (index_jnode(j) >= index) {
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
kill_hook_extent(const coord_t *coord, unsigned from, unsigned count, void *p)
{
	reiser4_extent *ext;
	unsigned i;
	reiser4_block_nr start, length;
	reiser4_key key;
	loff_t offset;
	unsigned long index;
	struct inode *inode;

	inode = p;

	assert ("zam-811", znode_is_write_locked(coord->node));

	item_key_by_coord(coord, &key);
	offset = get_key_offset(&key) + extent_size(coord, from);
	index = offset >> current_blocksize_bits;

	if (inode) {
		truncate_inode_pages(inode->i_mapping, offset);
		drop_eflushed_nodes(inode, index);
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
		  int cut, const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed, struct inode *inode)
{
	reiser4_extent *ext;
	reiser4_key key;
	unsigned blocksize, blocksize_bits;
	reiser4_block_nr offset;
	unsigned count;
	__u64 cut_from_to;

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

			/* FIXME:NIKITA->VS I see this failing with new_width
			   == old_width (@to unit is not affected at all). */
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

	if (!cut)
		/* call kill hook for all extents removed completely */
		kill_hook_extent(coord, *from, count, inode);

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
		 const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed, void *p)
{
	return cut_or_kill_units(item, from, to, 1, from_key, to_key, smallest_removed, p);
}

/* plugin->u.item.b.kill_units */
int
kill_units_extent(coord_t *item, unsigned *from, unsigned *to,
		  const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed, void *p)
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

/* plugin->u.item.b.estimate
   plugin->u.item.b.item_data_by_flow */

/* union union-able extents and cut an item correspondingly */
static void
optimize_extent(const coord_t *item)
{
	unsigned i, old_num, new_num;
	reiser4_extent *cur, *new_cur, *start;
	reiser4_block_nr cur_width, new_cur_width;
	extent_state cur_state;
	ON_DEBUG(const char *error);

	assert("vs-765", coord_is_existing_item(item));
	assert("vs-763", item_is_extent(item));
	assert("vs-934", check_extent(item, &error) == 0);

	cur = start = extent_item(item);
	old_num = nr_units_extent(item);
	new_num = 0;
	new_cur = NULL;
	new_cur_width = 0;

	for (i = 0; i < old_num; i++, cur++) {
		cur_width = extent_get_width(cur);
		if (!cur_width)
			continue;

		cur_state = state_of_extent(cur);
		if (new_cur && state_of_extent(new_cur) == cur_state) {
			/* extents can be unioned when they are holes or unallocated extents or when they are adjacent
			   allocated extents */
			if (cur_state != ALLOCATED_EXTENT ||
			    (extent_get_start(new_cur) + new_cur_width == extent_get_start(cur))) {
				new_cur_width += cur_width;
				extent_set_width(new_cur, new_cur_width);
				continue;
			}
		}

		/* @ext can not be joined with @prev, move @prev forward */
		if (new_cur)
			new_cur++;
		else {
			assert("vs-935", cur == start);
			new_cur = start;
		}

		/* FIXME-VS: this is not necessary if new_cur == cur */
		*new_cur = *cur;
		new_cur_width = cur_width;
		new_num++;
	}

	if (new_num != old_num) {
		/* at least one pair of adjacent extents has merged. Shorten
		   item from the end correspondingly */
		int result;
		coord_t from, to;

		assert("vs-952", new_num < old_num);

		coord_dup(&from, item);
		from.unit_pos = new_num;
		from.between = AT_UNIT;

		coord_dup(&to, &from);
		to.unit_pos = old_num - 1;

		/* wipe part of item which is going to be cut, so that
		   node_check will not be confused by extent overlapping */
		xmemset(extent_by_coord(&from), 0, sizeof (reiser4_extent) * (old_num - new_num));
		result = cut_node(&from, &to, 0, 0, 0, DELETE_DONT_COMPACT, 0, 0/*inode*/);

		/* nothing should happen cutting
		   FIXME: JMACD->VS: Just return the error! */
		assert("vs-456", result == 0);
	}
	check_me("nikita-2755", reiser4_grab_space_force(1, BA_RESERVED, "optimize_extent") == 0);
	znode_make_dirty(item->node);
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

	for (i = 0; i < num_units; ++i, ++ext) {

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

/* ask block allocator for some blocks */
static int
extent_allocate_blocks(reiser4_blocknr_hint *preceder,
		       reiser4_block_nr wanted_count, reiser4_block_nr *first_allocated, reiser4_block_nr *allocated)
{
	int result;

	*allocated = wanted_count;
	preceder->max_dist = 0;	/* scan whole disk, if needed */

	/* that number of blocks (wanted_count) must be in UNALLOCATED stage */
	preceder->block_stage = BLOCK_UNALLOCATED;
	
	result = reiser4_alloc_blocks (preceder, first_allocated, allocated, BA_PERMANENT, "extent_allocate");

	if (result)
		/* no free space
		   FIXME-VS: returning -ENOSPC is not enough
		   here. It should not happen actually
		*/
		impossible("vs-420", "could not allocate unallocated: %d", result);
	else
		/* update flush_pos's preceder to last allocated block number */
		preceder->blk = *first_allocated + *allocated - 1;

	return result;
}

/* look unallocated extent of file with @objectid corresponding to @offset was
   replaced allocated extent [first, count]. Look for corresponding buffers in
   the page cache and map them properly
   FIXME-VS: this needs changes if blocksize != pagesize is needed
*/
static int
assign_jnode_blocknrs(oid_t oid, unsigned long index, reiser4_block_nr first,
		      /* FIXME-VS: get better type for number of
		         blocks */
		      reiser4_block_nr count, flush_pos_t *flush_pos)
{
	jnode *j;
	int i;
	reiser4_tree *tree = current_tree;

	for (i = 0; i < (int) count; i++, first++, index ++) {
		j = jlook_lock(tree, oid, index);
		assert("vs-1401", j);
		assert("vs-1132", blocknr_is_fake(jnode_get_block(j)));
		jnode_set_block(j, &first);

		/* If we allocated it cannot have been wandered -- in that case
		   extent_needs_allocation returns 0. */
		assert("jmacd-61442", !JF_ISSET(j, JNODE_OVRWR));
		jnode_make_reloc(j, pos_fq(flush_pos));
		/* FIXME: ??? */
		junprotect(j);
		jput(j);
	}

	return 0;
}

static lock_handle *
znode_lh(znode *node)
{
	assert("vs-1371", znode_is_write_locked(node));
	assert("vs-1372", znode_is_wlocked_once(node));
	return owners_list_front(&node->lock.owners);
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
	check_me("vpf-340", !reiser4_grab_space_force(needed, BA_RESERVED, "reserve_replace"));
	return grabbed;
}

static void
free_replace_reserved(reiser4_block_nr grabbed)
{
	reiser4_context *ctx;

	ctx = get_current_context();
	grabbed2free(ctx, get_super_private(ctx->super), ctx->grabbed_blocks - grabbed, "free_replace_reserved");
}

/* @coord is set to allocated extent - [start/width]. 
   Replace it with one of these depending of ue_start and ue_width
   1. [u/width]
   2. [u/ue_width][start+ue_width]
   3. [start/ue_start][u/ue_width]
   4. [start/ue_start][u/ue_width][start+ue_start+ue_width/ae_width-ue_start-ue_width]
*/
static int
allocated2unallocated(coord_t *coord, reiser4_block_nr ue_start, reiser4_block_nr ue_width)
{
	int result;
	reiser4_extent *ext;
	reiser4_extent new_exts[2]; /* extents which will be added after original hole one */
	reiser4_extent replace;	    /* extent original hole extent will be replaced with */
	reiser4_block_nr ae_first_block;
	reiser4_block_nr grabbed;
	reiser4_block_nr ae_width;
	reiser4_item_data item;
	int count;
	reiser4_key key;

	ext = extent_by_coord(coord);
	ae_first_block = extent_get_start(ext);
	ae_width = extent_get_width(ext);
	
	if (ae_width == ue_width) {
		/* 1 */
		set_extent(ext, UNALLOCATED_EXTENT_START, ae_width);
		znode_make_dirty(coord->node);
		return 0;
	} else if (ue_start == 0) {
		/* 2 */
		/* replace ae with ue and ae unallocated extent */
		set_extent(&replace, UNALLOCATED_EXTENT_START, ue_width);
		/* allocated extent  */
		set_extent(&new_exts[0], ae_first_block + ue_width, ae_width - ue_width);
		count = 1;
	} else if (ue_start + ue_width == ae_width) {
		/* 3 */
		/* replace ae with ae and ue */
		/* FIXME-VS: possible optimization: look to the right and merge if right neighbor is unallocated extent
		   too */
		/* allocated extent */
		set_extent(&replace, ae_first_block, ae_width - ue_width);
		/* unallocated extent */
		set_extent(&new_exts[0], UNALLOCATED_EXTENT_START, ue_width);
		count = 1;
	} else {
		/* 4 */
		/* replace ae with ae, ue, ae */
		set_extent(&replace, ae_first_block, ue_start);
		/* extents to be inserted */
		set_extent(&new_exts[0], UNALLOCATED_EXTENT_START, ue_width);
		set_extent(&new_exts[1], ae_first_block + ue_start + ue_width, ae_width - ue_start - ue_width);
		count = 2;
	}

	/* insert_into_item will insert new units after the one @coord is set to. So, update key correspondingly */
	unit_key_by_coord(coord, &key);
	set_key_offset(&key, (get_key_offset(&key) + extent_get_width(&replace) * current_blocksize));

	grabbed = reserve_replace();
	result = replace_extent(coord, znode_lh(coord->node), &key, init_new_extent(&item, new_exts, count), &replace, COPI_DONT_SHIFT_LEFT);
	free_replace_reserved(grabbed);
	return result;
}

/* if @key is glueable to the item @coord is set to */
static int
must_insert(coord_t *coord, reiser4_key *key)
{
	reiser4_key last;

	if (item_id_by_coord(coord) == EXTENT_POINTER_ID && keyeq(append_key_extent(coord, &last), key))
		return 0;
	return 1;
}

/* helper for allocate_and_copy_extent
   append last item in the @node with @data if @data and last item are
   mergeable, otherwise insert @data after last item in @node. Have carry to
   put new data in available space only. This is because we are in squeezing.
  
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
		result = insert_into_item(&coord, 0 /*lh */ , key, data, flags);
	}

	assert("vs-438", result == 0 || result == -E_NODE_FULL);
	return result;
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

		node = jlook_lock(tree, oid, ind);
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

/* unallocated extent of width @count is going to be allocated. Protect all unformatted nodes from e-flushing. If
   unformatted node is eflushed already - it gets un-eflushed. Note, that it does not un-eflush more than JNODES_TO_UNFLUSH
   jnodes. All jnodes corresponding to this extent must exist */
static int
protect_extent_nodes(oid_t oid, unsigned long ind, __u64 count, __u64 *protected)
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
	*protected = 0;
	for (i = 0; i < count; ++i) {
		jnode  *node;

		node = jlook_lock(tree, oid, ind + i);
		/*
		 * all jnodes of unallocated extent should be in
		 * place. Truncate removes extent item together with jnodes
		 */
		assert("nikita-3087", node != NULL);

		LOCK_JNODE(node);

		assert("zam-836", !JF_ISSET(node, JNODE_EPROTECTED));
		assert("vs-1216", jnode_is_unformatted(node));
	
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
		unprotect_extent_nodes(oid, ind, i);
	}
	return result;
#else
/* !REISER4_USE_EFLUSH */
	*protected = count;
	return 0;
#endif
}


unsigned find_extent_slum_size(const coord_t *start, unsigned pos_in_unit)
{
	reiser4_tree *tree;
	oid_t oid;
	unsigned long index;
	jnode *node;
	unsigned slum_size;
	int slum_done;
	unsigned i; /* position within an unit */
	coord_t coord;
	reiser4_key key;
	reiser4_extent *ext;

	tree = current_tree;

	assert ("vs-1387", item_is_extent(start));
	coord_dup(&coord, start);

	oid = get_key_objectid(item_key_by_coord(&coord, &key));
	index = extent_unit_index(&coord) + pos_in_unit;

	ON_TRACE(TRACE_EXTENTS, "find_extent_slum_size: start from page %lu, [item %d, unit %d, pos_in_unit %u] ext:[%llu/%llu] of oid %llu\n",
		 index, coord.item_pos, coord.unit_pos, pos_in_unit, extent_unit_start(&coord), extent_unit_width(&coord), oid);

	slum_size = 0;
	slum_done = 0;
	do {
		ext = extent_by_coord(&coord);
		switch (state_of_extent(ext)) {
		case ALLOCATED_EXTENT:
			for (i = pos_in_unit; i < extent_get_width(ext); i ++) {
				node = jlook_lock(tree, oid, index);
				if (!node) {
					slum_done = 1;
					break;
				}
				if (jnode_check_flushprepped(node)) {
					jput(node);
					slum_done = 1;
					break;
				}
				slum_size ++;
				index ++;
				jput(node);
			}
			break;

		case HOLE_EXTENT:
			/* slum does not break at hole */
			assert("vs-1384", pos_in_unit == 0);
			index += extent_get_width(ext);
			break;

		case UNALLOCATED_EXTENT:
			assert("vs-1388", pos_in_unit == 0);
			ON_DEBUG({
				for (i = 0; i < extent_get_width(ext); i ++) {
					node = jlook_lock(tree, oid, index + i);
					assert("vs-1389", node);
					jput(node);
				}
			});

			slum_size += extent_get_width(ext);
			index += extent_get_width(ext);
			break;
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

		ON_TRACE(TRACE_EXTENTS, "find_extent_slum_size: slum size %u. Next page %lu. "
			 "Coord: [item %d, unit %d, pos_in_unit %u] ext:[%llu/%llu] of oid %llu\n",
			 slum_size, index, coord.item_pos, coord.unit_pos, pos_in_unit,
			 extent_unit_start(&coord), extent_unit_width(&coord), oid);
	} while (1);

	ON_TRACE(TRACE_EXTENTS, "find_extent_slum_size: end. slum size: %u\n", slum_size);
	return slum_size;
}

/* convert allocated jnodes to unallocated, replace allocated extent corresponding to them to unallocated, dealloc
   allocated blocks */
static int
convert_allocated_extent2unallocated(oid_t oid, coord_t *coord, unsigned slum_start, unsigned slum_size, int do_conversion)
{
	int result;
	unsigned i;
	jnode *j;
	txn_atom *atom;
	reiser4_block_nr blocknr;
	reiser4_tree *tree;
	unsigned long index;
	reiser4_block_nr ae_start;
	reiser4_block_nr blocks;

	assert("vs-1405", slum_start + slum_size <= extent_unit_width(coord));

	atom = get_current_atom_locked();
	/* All additional blocks needed for safe writing of modified extent are counted in atom's flush reserved
	   counted.  Here we move that amount to "grabbed" counter for further spending it in
	   assign_fake_blocknr(). Thus we convert "flush reserved" space to "unallocated" one reflecting that
	   extent allocation status change. */
	flush_reserved2grabbed(atom, slum_size);
	UNLOCK_ATOM(atom);

	/* assign fake blocknr to all nodes which are going to be relocated */
	tree = ZJNODE(coord->node)->tree;
	index = extent_unit_index(coord) + slum_start;
	for (i = 0; i < slum_size; i ++, index ++) {
		j = jlook_lock(tree, oid, index);
		assert("vs-1367", j);
		assert("vs-1363", !jnode_check_flushprepped(j));
		blocknr = fake_blocknr_unformatted();
		jnode_set_block(j, &blocknr);
		jput(j);
	}

	/* slum_size allocated blocks will be freed. Get block number of first of them */
	ae_start = extent_unit_start(coord);
	ae_start += slum_start;

	if (do_conversion) {
		/* convert [alloc/extent_width] to 
		           [alloc/slum_start][unalloc/slum_size][alloc/extent_width - slum_start - slum_size] */
		result = allocated2unallocated(coord, slum_start, slum_size);
		if (result)
			return result;
	}
	blocks = slum_size;
	return reiser4_dealloc_blocks(&ae_start, &blocks, BLOCK_ALLOCATED, BA_DEFER,
				      "deallocate blocks of already allocated extent");
}

/* flush_pos is set to extent unit. Slum starts from flush_pos->pos_in_unit within this unit. This function may perform
   complex extent convertion. It may be either converted to allocated or to mixture of allocated and unallocated
   extents. */
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
		if (state == HOLE_EXTENT)
			*slum_size -= extent_slum_size;
		return 0;
	}

	if (state == HOLE_EXTENT)
		/* hole does not break "slum" */
		return 0;

	if (state == ALLOCATED_EXTENT) {
		/* [start/width] ->
		 * [start/pos_in_unit][u/extent_slum_size][start+pos_in_unit+extent_slum_size/width-pos_in_unit-extent_slum_size] */
		result = convert_allocated_extent2unallocated(oid, coord, flush_pos->pos_in_unit, extent_slum_size, 1/*do conversion*/);
		if (result)
			return result;
		if (flush_pos->pos_in_unit) {
			/* slum starts in next unit */
			assert("vs-1399", state_of_extent(ext) == ALLOCATED_EXTENT);
			return 0;
		}
	}

	assert("vs-1400", state_of_extent(ext) == UNALLOCATED_EXTENT);
	assert("vs-1402", extent_slum_size == extent_get_width(ext));
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
	result = protect_extent_nodes(oid, index, extent_slum_size, &protected);
	if (result)
		return result;
	
	check_me("vs-1138", extent_allocate_blocks(pos_hint(flush_pos), 
							   protected, &first_allocated, &allocated) == 0);
	assert("vs-440", allocated > 0 && allocated <= protected);
	if (allocated < protected)
		/* unprotect nodes which will not be allocated on this iteration */
		protected -= unprotect_extent_nodes(oid, index + allocated, protected - allocated);
	
	/* find all jnodes for which blocks were allocated and assign block numbers to them, call jnode_make_reloc and
	   unprotect (thye are now protected by JNODE_FLUSH_QUEUED bit) */
	assign_jnode_blocknrs(oid, index, first_allocated, allocated, flush_pos);
	index += allocated;

	/* compose extent which will replace current one */
	set_extent(&replace, first_allocated, allocated);
	if (allocated == extent_slum_size) {
		/* whole extent is allocated */
		*ext = replace;
		
		/* no need to grab space as it is done already */
		znode_make_dirty(coord->node);

		*slum_size -= allocated;
		return 0;
	}

	/* set @key to key of first byte of part of extent which left unallocated */
	set_key_offset(&key, index << PAGE_CACHE_SHIFT);
	set_extent(&paste, UNALLOCATED_EXTENT_START, width - allocated);

	/* [u/width] ->
	   [first_allocated/allocated][u/width - allocated] */
	
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

	if (item_id_by_coord(&flush_pos->coord) == FROZEN_EXTENT_POINTER_ID) {
		*slum_size -= extent_slum_size;
		return 0;
	}
	for (i = 0; i < extent_slum_size; i ++) {
		j = jlook_lock(tree, oid, index + i);
		assert("vs-1396", j && !jnode_check_flushprepped(j));
		jnode_make_wander(j);
		jput(j);
	}
	*slum_size -= extent_slum_size;
	return 0;
}

/* this is used for relocation, so all jnodes have real blocknrs */
static int
change_jnode_blocknrs(oid_t oid, unsigned long index, reiser4_block_nr first,
		      reiser4_block_nr count, flush_pos_t *flush_pos)
{
	jnode *j;
	int i;
	reiser4_tree *tree = current_tree;

	for (i = 0; i < (int) count; i++, first++, index ++) {
		j = jlook_lock(tree, oid, index);
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
		result = protect_extent_nodes(oid, index, extent_slum_size, &protected);
		if (result)
			break;
		do {
			check_me("vs-1137", extent_allocate_blocks(pos_hint(flush_pos),
								   protected, &first_allocated, &allocated) == 0);
			if (try_copy(left, oid, first_allocated, allocated, &key) == SQUEEZE_TARGET_FULL) {
				if (state == ALLOCATED_EXTENT)
					grabbed2flush_reserved(extent_slum_size, "relocation allocated extents: left is full");
				unprotect_extent_nodes(oid, index, protected);
				reiser4_dealloc_blocks(&first_allocated, &allocated, BLOCK_ALLOCATED, 0, "free blocks allocated for relocation");
				return SQUEEZE_TARGET_FULL;
			}

			/* FIXME: add error handling */
			check_me("vs-1400", reiser4_dealloc_blocks(&start, &allocated,
								   BLOCK_ALLOCATED, BA_DEFER, "freed blocks which were relocated") == 0);
			change_jnode_blocknrs(oid, index, first_allocated, allocated, flush_pos);
			index += allocated;
			protected -= allocated;
			extent_slum_size -= allocated;
			done += allocated;
			set_key_offset(&key, index << PAGE_CACHE_SHIFT);

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
		j = jlook_lock(tree, oid, index + i);
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

/* Block offset of first block addressed by unit */
/* AUDIT shouldn't return value be of reiser4_block_nr type?
   Josh's answer: who knows?  This returns the same type of information as "struct page->index", which is currently an unsigned long. */
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

/* insert extent item (containing one unallocated extent of width 1) to place
   set by @coord */
static int
insert_first_block(uf_coord_t *uf_coord, const reiser4_key *key, reiser4_block_nr *block)
{
	int result;
	reiser4_extent ext;
	reiser4_item_data unit;

	/* make sure that we really write to first block */
	assert("vs-240", get_key_offset(key) == 0);

	/* extent insertion starts at leaf level */
	assert("vs-719", znode_get_level(uf_coord->base_coord.node) == LEAF_LEVEL);

	set_extent(&ext, UNALLOCATED_EXTENT_START, 1);
	result = insert_extent_by_coord(&uf_coord->base_coord, init_new_extent(&unit, &ext, 1), key, uf_coord->lh);
	if (result) {
		/* FIXME-VITALY: this is grabbed at file_write time. */
		/* grabbed2free ((__u64)1); */
		return result;
	}

	*block = fake_blocknr_unformatted();

	/* invalidate coordinate, research must be performed to continue because write will continue on twig level */
	uf_coord->valid = 0;
	return 0;
}

/* @coord is set to the end of extent item. Append it with pointer to one block - either by expanding last unallocated
   extent or by appending a new one of width 1 */
static int
append_one_block(uf_coord_t *uf_coord, reiser4_key *key, reiser4_block_nr *block)
{
	int result;
	reiser4_extent new_ext;
	reiser4_item_data unit;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;

	/* check correctness of position in the item */
	assert("vs-228", coord->unit_pos == coord_last_unit_pos(coord));
	assert("vs-1311", coord->between == AFTER_UNIT);
	assert("vs-1302", ext_coord->pos_in_unit == ext_coord->width - 1);
	assert("vs-883", 
	       ( {
		       reiser4_key next;
		       keyeq(key, append_key_extent(coord, &next));
	       }));

	switch (state_of_extent(ext_coord->ext)) {
	case UNALLOCATED_EXTENT:
		set_extent(ext_coord->ext, UNALLOCATED_EXTENT_START, extent_get_width(ext_coord->ext) + 1);
		znode_make_dirty(coord->node);

		/* update coord extension */
		ext_coord->width ++;
		break;

	case HOLE_EXTENT:
	case ALLOCATED_EXTENT:
		/* append one unallocated extent of width 1 */
		set_extent(&new_ext, UNALLOCATED_EXTENT_START, 1);
		result = insert_into_item(coord, uf_coord->lh, key, init_new_extent(&unit, &new_ext, 1), 0 /* flags */ );
		/* FIXME: for now */
		uf_coord->valid = 0;
		if (result)
			return result;
		break;
	}

	*block = fake_blocknr_unformatted();
	return 0;
}

/* @coord is set to hole unit inside of extent item, replace hole unit with an
   unit for unallocated extent of the width 1, and perhaps a hole unit before
   the unallocated unit and perhaps a hole unit after the unallocated unit. */
static int
plug_hole(uf_coord_t *uf_coord, reiser4_key *key)
{
	reiser4_extent *ext, new_exts[2],	/* extents which will be added after original
						 * hole one */
	 replace;		/* extent original hole extent will be replaced
				 * with */
	reiser4_block_nr width, pos_in_unit;
	reiser4_item_data item;
	int count;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;

	ext = ext_coord->ext;
	width = ext_coord->width;
	pos_in_unit = ext_coord->pos_in_unit;

	if (width == 1) {
		set_extent(ext, UNALLOCATED_EXTENT_START, 1);
		znode_make_dirty(coord->node);
		return 0;
	} else if (pos_in_unit == 0) {
		if (coord->unit_pos) {
			if (state_of_extent(ext - 1) == UNALLOCATED_EXTENT) {
				extent_set_width(ext - 1, extent_get_width(ext - 1) + 1);
				extent_set_width(ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos --;
				ext_coord->width = extent_get_width(ext - 1);
				ext_coord->pos_in_unit = ext_coord->width - 1;
				ext_coord->ext --;
				ON_DEBUG(ext_coord->extent = *ext_coord->ext);
				return 0;
			}
		}
		/* extent for replace */
		set_extent(&replace, UNALLOCATED_EXTENT_START, 1);
		/* extent to be inserted */
		set_extent(&new_exts[0], HOLE_EXTENT_START, width - 1);
		count = 1;
	} else if (pos_in_unit == width - 1) {
		if (coord->unit_pos < nr_units_extent(coord) - 1) {
			if (state_of_extent(ext + 1) == UNALLOCATED_EXTENT) {
				extent_set_width(ext + 1, extent_get_width(ext + 1) + 1);
				extent_set_width(ext, width - 1);
				znode_make_dirty(coord->node);

				/* update coord extension */
				coord->unit_pos ++;
				ext_coord->width = extent_get_width(ext + 1);
				ext_coord->pos_in_unit = 0;
				ext_coord->ext ++;
				ON_DEBUG(ext_coord->extent = *ext_coord->ext);
				return 0;
			}
		}
		/* extent for replace */
		set_extent(&replace, HOLE_EXTENT_START, width - 1);
		/* extent to be inserted */
		set_extent(&new_exts[0], UNALLOCATED_EXTENT_START, 1);
		count = 1;
	} else {
		/* extent for replace */
		set_extent(&replace, HOLE_EXTENT_START, pos_in_unit);
		/* extents to be inserted */
		set_extent(&new_exts[0], UNALLOCATED_EXTENT_START, 1);
		set_extent(&new_exts[1], HOLE_EXTENT_START, width - pos_in_unit - 1);
		count = 2;
	}

	/* insert_into_item will insert new units after the one @coord is set
	   to. So, update key correspondingly */
	unit_key_by_coord(coord, key);	/* FIXME-VS: how does it work without this? */
	set_key_offset(key, (get_key_offset(key) + extent_get_width(&replace) * current_blocksize));

	uf_coord->valid = 0;
	return replace_extent(coord, uf_coord->lh, key, init_new_extent(&item, new_exts, count), &replace, 0 /* flags */);
}

/* make unallocated node pointer in the position @uf_coord is set to */
static int
overwrite_one_block(uf_coord_t *uf_coord, reiser4_key *key, reiser4_block_nr *block)
{
	int result;
	extent_coord_extension_t *ext_coord;

	assert("vs-1312", uf_coord->base_coord.between == AT_UNIT);

	ext_coord = &uf_coord->extension.extent;
	switch (state_of_extent(ext_coord->ext)) {
	case ALLOCATED_EXTENT:
		*block = extent_get_start(ext_coord->ext) + ext_coord->pos_in_unit;
		result = 0;
		break;

	case HOLE_EXTENT:
		result = plug_hole(uf_coord, key);
		if (!result)
			*block = fake_blocknr_unformatted();
		break;

	case UNALLOCATED_EXTENT:
	default:
		impossible("vs-238", "extent of unknown type found");
		result = RETERR(-EIO);
		break;
	}

	return result;
}

static int
make_extent(reiser4_key *key, uf_coord_t *uf_coord, write_mode_t mode, reiser4_block_nr *block)
{
	int result;

	assert("vs-960", znode_is_write_locked(uf_coord->base_coord.node));
	assert("vs-1334", znode_is_loaded(uf_coord->base_coord.node));

	switch (mode) {
	case FIRST_ITEM:
		/* create first item of the file */
		result = insert_first_block(uf_coord, key, block);
		break;

	case APPEND_ITEM:
		assert("vs-1316", coord_extension_is_ok(uf_coord));
		result = append_one_block(uf_coord, key, block);
		break;

	case OVERWRITE_ITEM:
		assert("vs-1316", coord_extension_is_ok(uf_coord));
		result = overwrite_one_block(uf_coord, key, block);
		break;

	default:
		assert("vs-1346", 0);
		result = RETERR(-EAGAIN);
		break;
	}
		
	return result;
}

/* if page is not completely overwritten - read it if it is not new or fill by zeros otherwise */
static int
prepare_page(struct inode *inode, struct page *page, loff_t file_off, unsigned from, unsigned count)
{
	char *data;
	int result;
	jnode *j;

	result = 0;
	if (PageUptodate(page))
		goto done;

	if (count == current_blocksize)
		goto done;

	j = jnode_by_page(page);

	/* jnode may be emergency flushed and have fake blocknumber, */

	if (JF_ISSET(j, JNODE_NEW)) {
		/* new page does not get zeroed. Fill areas around write one by 0s */
		data = kmap_atomic(page, KM_USER0);
		memset(data, 0, from);
		memset(data + from + count, 0, PAGE_CACHE_SIZE - from - count);
		flush_dcache_page(page);
		kunmap_atomic(data, KM_USER0);
		goto done;
	}
	/* page contains some data of this file */
	assert("vs-699", inode->i_size > page->index << PAGE_CACHE_SHIFT);

	if (from == 0 && file_off + count >= inode->i_size) {
		/* current end of file is in this page. write areas covers it
		   all. No need to read block. Zero page past new end of file,
		   though */
		data = kmap_atomic(page, KM_USER0);
		memset(data + from + count, 0, PAGE_CACHE_SIZE - from - count);
		kunmap_atomic(data, KM_USER0);
		goto done;
	}

	/* read block because its content is not completely overwritten */
	reiser4_stat_inc(extent.unfm_block_reads);

	page_io(page, j, READ, GFP_NOIO);

	reiser4_lock_page(page);
	UNDER_SPIN_VOID(jnode, j, eflush_del(j, 1));

	if (!PageUptodate(page)) {
		warning("jmacd-61238", "prepare_page: page not up to date");
		result = RETERR(-EIO);
	}
	
 done:
	return result;
}

/* drop longterm znode lock before calling balance_dirty_pages. balance_dirty_pages may cause transaction to close,
   therefore we have to update stat data if necessary */
static int
extent_balance_dirty_pages(struct address_space *mapping, const flow_t *f,
			   hint_t *hint)
{
	int result;
	loff_t new_size;
	struct inode *object;

	if (hint->coord.valid)
		set_hint(hint, &f->key);
	else
		unset_hint(hint);
	longterm_unlock_znode(hint->coord.lh);

	new_size = get_key_offset(&f->key);
	object = mapping->host;
	result = update_inode_and_sd_if_necessary(object, new_size, 
						  (new_size > object->i_size) ? 1 : 0, 1/* update stat data */);
	if (result)
		return result;

	balance_dirty_page_unix_file(object);

	return hint_validate(hint, &f->key, 0/* do not check key */, ZNODE_WRITE_LOCK);
}

/* estimate and reserve space which may be required for writing one page of file */
static int
reserve_extent_write_iteration(struct inode *inode, reiser4_tree *tree)
{
	int result;

	grab_space_enable();
	/* one unformatted node and one insertion into tree and one stat data update may be involved */
	result = reiser4_grab_space(1 + /* Hans removed reservation for balancing here. */
				    /* if extent items will be ever used by plugins other than unix file plugin - estimate update should instead be taken by
				       inode_file_plugin(inode)->estimate.update(inode)
				    */
				    estimate_update_common(inode),
				    0/* flags */, "extent_write");
	return result;
}

static void
write_move_coord(coord_t *coord, uf_coord_t *uf_coord, write_mode_t mode, int full_page)
{
	extent_coord_extension_t *ext_coord;

	assert("vs-1339", ergo(mode == OVERWRITE_ITEM, coord->between == AT_UNIT));
	assert("vs-1341", ergo(mode == FIRST_ITEM, uf_coord->valid == 0));

	if (uf_coord->valid == 0)
		return;

	ext_coord = &uf_coord->extension.extent;

	if (mode == APPEND_ITEM) {
		assert("vs-1340", coord->between == AFTER_UNIT);
		assert("vs-1342", coord->unit_pos == ext_coord->nr_units - 1);
		assert("vs-1343", ext_coord->pos_in_unit == ext_coord->width - 2);
		assert("vs-1344", state_of_extent(ext_coord->ext) == UNALLOCATED_EXTENT);
		ext_coord->pos_in_unit ++;
		if (!full_page)
			coord->between = AT_UNIT;
		return;
	}

	assert("vs-1345", coord->between == AT_UNIT);

	if (!full_page)
		return;
	if (ext_coord->pos_in_unit == ext_coord->width - 1) {
		/* last position in the unit */
		if (coord->unit_pos == ext_coord->nr_units - 1) {
			/* last unit in the item */
			uf_coord->valid = 0;
		} else {
			/* move to the next unit */
			coord->unit_pos ++;
			ext_coord->ext ++;
			ON_DEBUG(ext_coord->extent = *ext_coord->ext);
			ext_coord->width = extent_get_width(ext_coord->ext);
			ext_coord->pos_in_unit = 0;
		}
	} else
		ext_coord->pos_in_unit ++;
}

void jnode_attach_page(jnode *, struct page *);

/* staring from page through extent to jnode */
static jnode *
page_extent_jnode(reiser4_tree *tree, oid_t oid, reiser4_key *key, uf_coord_t *uf_coord,
		  struct page *page, write_mode_t mode)
{
	int result;
	jnode *j;

	assert("vs-1394", PageLocked(page));
	assert("vs-1396", get_key_objectid(key) == get_inode_oid(page->mapping->host));
	assert("vs-1397", get_key_objectid(key) == oid);
	assert("vs-1395", get_key_offset(key) == (loff_t)page->index << PAGE_CACHE_SHIFT);

	if (!PagePrivate(page)) {
		/* page has no jnode */
		j = jlook_lock(tree, oid, page->index);
		if (!j) {
			reiser4_block_nr blocknr;

			j = jnew();
			if (unlikely(!j))
				return ERR_PTR(RETERR(-ENOMEM));

			reiser4_unlock_page(page);
			result = make_extent(key, uf_coord, mode, &blocknr);
			if (result) {
				jfree(j);
				return ERR_PTR(result);
			}
			reiser4_lock_page(page);
			if (!PagePrivate(page)) {
				/* page is still not private. Initialize jnode and attach to page */
				jnode_set_mapped(j);
				jnode_set_block(j, &blocknr);
				if (blocknr_is_fake(&blocknr)) {
					jnode_set_created(j);
					JF_SET(j, JNODE_NEW);			
				}
				assert("vs-1402", !jlook_lock(tree, oid, page->index));
				bind_jnode_and_page(j, oid, page);
			} else {
				/* page was attached to jnode already in other thread */
				jfree(j);
				j = jnode_by_page(page);
				assert("vs-1390", jnode_mapped(j));
				assert("vs-1392", *jnode_get_block(j) == blocknr);
				jref(j);
			}
		} else {
			assert("vs-1390", jnode_mapped(j));
			UNDER_SPIN_VOID(jnode, j, jnode_attach_page(j, page));
		}
	} else {
		/* page has jnode already. Therefore, there is non hole extent which points to this page */
		j = jnode_by_page(page);
		assert("vs-1390", jnode_mapped(j));
		jref(j);
	}
	return j;
}

/* this is used to capture page in write_extent and in writepage_extent */
static int
try_capture_dirty_page(jnode *node, struct page *page)
{
	int result;

	assert("umka-292", page != NULL);
	assert("nikita-2597", PageLocked(page));

	LOCK_JNODE(node);
	reiser4_unlock_page(page);

	/* FIXME: possible optimization: if jnode is not dirty yet - it gets into clean list in try_capture and then in
	   jnode_mark_dirty gets moved to dirty list. So, it would be more optimal to put jnode directly to dirty
	   list */
	result = try_capture(node, ZNODE_WRITE_LOCK, 0);
	if (!result)
		unformatted_jnode_make_dirty(node);
	UNLOCK_JNODE(node);
	return result;
}

/* write flow's data into file by pages */
static int
extent_write_flow(struct inode *inode, flow_t *flow, hint_t *hint, 
		  int grabbed, /* 0 if space for operation is not reserved yet, 1 - otherwise */
		  write_mode_t mode)
{
	int result;
	loff_t file_off;
	unsigned long page_nr;
	unsigned long page_off, count;
	struct page *page;
	jnode *j;
	uf_coord_t *uf_coord;
	coord_t *coord;
	oid_t oid;
	reiser4_tree *tree;
	reiser4_key page_key;
	PROF_BEGIN(extent_write);

	assert("nikita-3139", !inode_get_flag(inode, REISER4_NO_SD));
	assert("vs-885", current_blocksize == PAGE_CACHE_SIZE);
	assert("vs-700", flow->user == 1);
	assert("vs-1352", flow->length > 0);

	/* FIXME-VS: fix this */
	if (DQUOT_ALLOC_SPACE_NODIRTY(inode, flow->length))
		return RETERR(-EDQUOT);

	tree = tree_by_inode(inode);
	oid = get_inode_oid(inode);
	uf_coord = &hint->coord;
	coord = &uf_coord->base_coord;

	/* position in a file to start write from */
	file_off = get_key_offset(&flow->key);
	/* index of page containing that offset */
	page_nr = (unsigned long)(file_off >> PAGE_CACHE_SHIFT);
	/* offset within the page */
	page_off = (unsigned long)(file_off & (PAGE_CACHE_SIZE - 1));

	/* key of first byte of page */
	page_key = flow->key;
	set_key_offset(&page_key, (loff_t)page_nr << PAGE_CACHE_SHIFT);
	do {
		if (!grabbed) {
			result = reserve_extent_write_iteration(inode, tree);
			if (result)
				break;
		}
		/* number of bytes to be written to page */
		count = PAGE_CACHE_SIZE - page_off;
		if (count > flow->length)
			count = flow->length;

		write_page_trace(inode->i_mapping, page_nr);

		fault_in_pages_readable(flow->data, count);
		page = grab_cache_page(inode->i_mapping, page_nr);
		if (!page) {
			result = RETERR(-ENOMEM);
			goto exit1;
		}

		j = page_extent_jnode(tree, oid, &page_key, uf_coord, page, mode);
		if (IS_ERR(j)) {
			result = PTR_ERR(j);
			goto exit2;
		}

		/* if page is not completely overwritten - read it if it is not new or fill by zeros otherwise */
		result = prepare_page(inode, page, file_off, page_off, count);
		JF_CLR(j, JNODE_NEW);
		if (result)
			goto exit3;

		assert("nikita-3033", schedulable());

		/* copy user data into page */
		result = __copy_from_user((char *)kmap(page) + page_off, flow->data, count);
		kunmap(page);
		if (unlikely(result)) {
			result = RETERR(-EFAULT);
			goto exit3;
		}

		set_page_dirty_internal(page);
		SetPageUptodate(page);
		if (!PageReferenced(page))
			SetPageReferenced(page);

		result = try_capture_dirty_page(j, page);
		/* unlock page is in try_capture_dirty_page */
		page_cache_release(page);
		jput(j);
		if (result)
			goto exit1;

		move_flow_forward(flow, count);
		write_move_coord(coord, uf_coord, mode, page_off + count == PAGE_CACHE_SIZE);
			
		/* throttle the writer */
		result = extent_balance_dirty_pages(inode->i_mapping, flow, hint);
		if (!grabbed)
			all_grabbed2free("extent_write_flow");
		if (result) {
			reiser4_stat_inc(extent.bdp_caused_repeats);
			break;
		}

		page_off = 0;
		page_nr ++;
		file_off += count;
		set_key_offset(&page_key, (loff_t)page_nr << PAGE_CACHE_SHIFT);
		continue;

	exit3:
		jput(j);
	exit2:
		reiser4_unlock_page(page);
		page_cache_release(page);
	exit1:
		if (!grabbed)
			all_grabbed2free("extent_write_flow on error");
		break;

		/* hint is unset by make_page_extent when first extent of a
		   file was inserted: in that case we can not use coord anymore
		   because we are to continue on twig level but are still at
		   leaf level
		*/
	} while (flow->length && uf_coord->valid == 1);

	if (flow->length)
		DQUOT_FREE_SPACE_NODIRTY(inode, flow->length);

	PROF_END(extent_write);
	return result;
}

/* estimate and reserve space which may be required for appending file with hole stored in extent */
static int
extent_hole_reserve(reiser4_tree *tree)
{
	/* adding hole may require adding a hole unit into extent item and stat data update */
	grab_space_enable();
	return reiser4_grab_space(estimate_one_insert_into_item(tree) * 2, 0, "extent_hole_reserve");
}

static int
extent_write_hole(struct inode *inode, flow_t *flow, hint_t *hint, int grabbed)
{
	int result;
	loff_t new_size;
	coord_t *coord;

	coord = &hint->coord.base_coord;
	if (!grabbed) {
		result = extent_hole_reserve(znode_get_tree(coord->node));
		if (result)
			return result;
	}

	new_size = get_key_offset(&flow->key) + flow->length;
	set_key_offset(&flow->key, new_size);
	flow->length = 0;
	result = add_hole(coord, hint->coord.lh, &flow->key);
	hint->coord.valid = 0;
	if (!result) {
		done_lh(hint->coord.lh);
		result = update_inode_and_sd_if_necessary(inode, new_size, 1/*update i_size*/, 1/* update stat data */);
	}
	if (!grabbed)
		all_grabbed2free("extent_write_hole");
	return result;
}

/*
  plugin->s.file.write
  It can be called in two modes:
  1. real write - to write data from flow to a file (@flow->data != 0)
  2. expanding truncate (@f->data == 0)
*/
int
write_extent(struct inode *inode, flow_t *flow, hint_t *hint, 
	     int grabbed, /* extent's write may be called from plain unix file write and from tail conversion. In first
			     case (grabbed == 0) space is not reserved forehand, so, it must be done here. When it is
			     being called from tail conversion - space is reserved already for whole operation which may
			     involve several calls to item write. In this case space reservation will not be done
			     here */
	     write_mode_t mode)
{
	if (flow->data)
		/* real write */
		return extent_write_flow(inode, flow, hint, grabbed, mode);

	/* expanding truncate. add_hole requires f->key to be set to new end of file */
	return extent_write_hole(inode, flow, hint, grabbed);
}

#if REISER4_DEBUG

/* return 1 if offset @off is inside of extent unit pointed to by @coord. Set pos_in_unit inside of unit
   correspondingly */
static int
offset_is_in_unit(const coord_t *coord, loff_t off)
{
	reiser4_key unit_key;
	__u64 unit_off;
	reiser4_extent *ext;

	ext = extent_by_coord(coord);

	unit_key_extent(coord, &unit_key);
	unit_off = get_key_offset(&unit_key);
	if (off < unit_off)
		return 0;
	if (off >= (unit_off + (current_blocksize * extent_get_width(ext))))
		return 0;
	return 1;
}

static int
coord_matches_key(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key item_key;

	assert("vs-771", coord_is_existing_unit(coord));
	assert("vs-1258", keylt(key, append_key_extent(coord, &item_key)));
	assert("vs-1259", keyge(key, item_key_by_coord(coord, &item_key)));

	return offset_is_in_unit(coord, get_key_offset(key));
}

#endif

/* move coord one page forward. Return 1 if coord is moved out of item */
static int
read_move_coord(coord_t *coord, extent_coord_extension_t *ext_coord)
{
	if (ext_coord->pos_in_unit == ext_coord->width - 1) {
		/* last position in the unit */
		if (coord->unit_pos == ext_coord->nr_units - 1) {
			/* last unit in the item */
			return 1;
		} else {
			/* move to the next unit */
			coord->unit_pos ++;
			ext_coord->ext ++;
			ON_DEBUG(ext_coord->extent = *ext_coord->ext);
			ext_coord->width = extent_get_width(ext_coord->ext);
			ext_coord->pos_in_unit = 0;
		}
	} else
		ext_coord->pos_in_unit ++;
	return 0;
}

static void
call_page_cache_readahead(struct address_space *mapping, struct file *file, unsigned long page_nr,
			  const uf_coord_t *uf_coord)
{
	reiser4_file_fsdata *fsdata;
	uf_coord_t ra_coord;
	
	fsdata = reiser4_get_file_fsdata(file);
	ra_coord = *uf_coord;
	ra_coord.extension.extent.expected_page = page_nr;
	fsdata->reg.coord = &ra_coord;
	
	page_cache_readahead(mapping, &file->f_ra, file, page_nr);
}


/* Implements plugin->u.item.s.file.read operation for extent items. */
int
read_extent(struct file *file, flow_t *f, uf_coord_t *uf_coord)
{
	int result;
	struct page *page;
	unsigned long page_nr;
	unsigned long page_off, count;
	struct inode *inode;
	__u64 file_off;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	assert("vs-1318", coord_extension_is_ok(uf_coord));

	inode = file->f_dentry->d_inode;
	coord = &uf_coord->base_coord;
	ext_coord = &uf_coord->extension.extent;

	ON_TRACE(TRACE_EXTENTS, "read_extent start: ino %llu, size %llu, offset %llu, count %u\n",
		 get_inode_oid(inode), inode->i_size, get_key_offset(&f->key), f->length);
	IF_TRACE(TRACE_EXTENTS, print_ext_coord("read_extent start", uf_coord));

	assert("vs-1353", current_blocksize == PAGE_CACHE_SIZE);
	assert("vs-572", f->user == 1);
	assert("vs-1351", f->length > 0);
	assert("vs-1119", znode_is_rlocked(coord->node));
	assert("vs-1120", znode_is_loaded(coord->node));
	assert("vs-1256", coord_matches_key(coord, &f->key));
	assert("vs-1355", get_key_offset(&f->key) + f->length <= inode->i_size);

	/* offset in a file to start read from */
	file_off = get_key_offset(&f->key);
	/* index of page containing that offset */
	page_nr = (unsigned long)(file_off >> PAGE_CACHE_SHIFT);
	/* offset within the page */
	page_off = (unsigned long)(file_off & (PAGE_CACHE_SIZE - 1));

	count = PAGE_CACHE_SIZE - page_off;

	do {
		call_page_cache_readahead(inode->i_mapping, file, page_nr, uf_coord);

		/* this will return page if it exists and is uptodate, otherwise it will allocate page and call
		   extent_readpage to fill it */
		page = read_cache_page(inode->i_mapping, page_nr, readpage_extent, coord);
		if (IS_ERR(page))
			return PTR_ERR(page);

		wait_on_page_locked(page);
		if (!PageUptodate(page)) {
			page_detach_jnode(page, inode->i_mapping, page_nr);
			page_cache_release(page);
			warning("jmacd-97178", "extent_read: page is not up to date");
			return RETERR(-EIO);
		}

		/* If users can be writing to this page using arbitrary virtual addresses, take care about potential
		   aliasing before reading the page on the kernel side.
		*/
		if (!list_empty(&inode->i_mapping->i_mmap_shared))
			flush_dcache_page(page);

		assert("nikita-3034", schedulable());
		
		/* number of bytes which can be read from the page */
		if (count > f->length)
			count = f->length;

		/* AUDIT: We must page-in/prepare user area first to avoid deadlocks */
		result = __copy_to_user(f->data, (char *)kmap(page) + page_off, count);
		kunmap(page);
	
		page_cache_release(page);
		if (unlikely(result))
			return RETERR(-EFAULT);
		
		/* coord should still be set properly */
		assert("vs-1263", coord_matches_key(coord, &f->key));
		move_flow_forward(f, count);
		if (page_off + count == PAGE_CACHE_SIZE)
			if (read_move_coord(coord, ext_coord))
				uf_coord->valid = 0;
		assert("vs-1214", ergo(uf_coord->valid == 1, coord_matches_key(coord, &f->key)));
		page_off = 0;
		page_nr ++;
		count = PAGE_CACHE_SIZE;
	} while (f->length && uf_coord->valid == 1);

	ON_TRACE(TRACE_EXTENTS, "read_extent done: left %u\n", f->length);
	IF_TRACE(TRACE_EXTENTS, print_ext_coord("read_extent done", uf_coord));

	return 0;
}

static int
move_coord_pages(coord_t *coord, extent_coord_extension_t *ext_coord, unsigned count)
{
	ext_coord->expected_page += count;

	do {
		if (ext_coord->pos_in_unit + count < ext_coord->width) {
			ext_coord->pos_in_unit += count;
			break;
		}

		if (coord->unit_pos == ext_coord->nr_units - 1) {
			coord->between = AFTER_UNIT;			
			return 1;
		}

		/* shift to next unit */
		count -= (ext_coord->width - ext_coord->pos_in_unit);
		coord->unit_pos ++;
		ext_coord->pos_in_unit = 0;
		ext_coord->ext ++;
		ON_DEBUG(ext_coord->extent = *ext_coord->ext);
		ext_coord->width = extent_get_width(ext_coord->ext);
	} while (1);

	return 0;	
}

static int
do_readpage_extent(reiser4_extent *ext, reiser4_block_nr pos, struct page *page)
{
	jnode *j;

	ON_TRACE(TRACE_EXTENTS, "readpage_extent: page (oid %llu, index %lu, count %d)..", 
		 get_inode_oid(page->mapping->host), page->index, page_count(page));

	switch (state_of_extent(ext)) {
	case HOLE_EXTENT:
		{
			char *kaddr = kmap_atomic(page, KM_USER0);

			xmemset(kaddr, 0, PAGE_CACHE_SIZE);
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
			SetPageUptodate(page);
			reiser4_unlock_page(page);
			ON_TRACE(TRACE_EXTENTS, "hole, OK\n");

			return 0;
		}

	case ALLOCATED_EXTENT:
		j = jnode_of_page(page);
		if (IS_ERR(j))
			return PTR_ERR(j);
		
#if 0
		if (!PagePrivate(page)) {
			oid_t oid;
			reiser4_block_nr blocknr;
			
			oid = get_inode_oid(page->mapping->host);
			j = jlook_lock(current_tree, oid, page->index);
			if (j) {
				info_jnode("found jnode", j);
				assert("", 0);
			}
			/*assert("vs-1391", !jlook_lock(current_tree, oid, page->index));*/

			j = jnew();
			if (unlikely(!j))
				return RETERR(-ENOMEM);

			jnode_set_mapped(j);
			blocknr = extent_get_start(ext) + pos;
			jnode_set_block(j, &blocknr);
			bind_jnode_and_page(j, oid, page);
			ON_TRACE(TRACE_EXTENTS, "allocated, page mage private, read issued\n");
		} else {
			j = jnode_by_page(page);
			assert("vs-1390", jnode_mapped(j));
			assert("vs-1392", !blocknr_is_fake(jnode_get_block(j)));
			jref(j);
			ON_TRACE(TRACE_EXTENTS, "allocated, page was private, read issued\n");
		}
#endif

		break;

	case UNALLOCATED_EXTENT:
		j = jlook_lock(current_tree, get_inode_oid(page->mapping->host),
			       page->index);
		assert("nikita-2688", j);
		assert("nikita-2802", JF_ISSET(j, JNODE_EFLUSH));
		ON_TRACE(TRACE_EXTENTS, "page was eflushed\n");
		break;

	default:
		warning("vs-957", "extent_readpage: wrong extent\n");
		return RETERR(-EIO);
	}

	BUG_ON(j == 0);
	page_io(page, j, READ, GFP_NOIO);
	jput(j);
	return 0;
}

/* 
   plugin->s.file.readpage
   reiser4_read->unix_file_read->page_cache_readahead->reiser4_readpage->unix_file_readpage->extent_readpage
   or
   filemap_nopage->reiser4_readpage->readpage_unix_file->->readpage_extent
   
   At the beginning: coord->node is read locked, zloaded, page is
   locked, coord is set to existing unit inside of extent item (it is not necessary that coord matches to page->index)
*/
int
readpage_extent(void *vp, struct page *page)
{
	uf_coord_t *uf_coord = vp;
	ON_DEBUG(coord_t *coord = &uf_coord->base_coord);
	ON_DEBUG(reiser4_key key);

	assert("vs-1040", PageLocked(page));
	assert("vs-1050", !PageUptodate(page));
	assert("vs-757", !jprivate(page) && !PagePrivate(page));
	assert("vs-1039", page->mapping && page->mapping->host);

	assert("vs-1044", znode_is_loaded(coord->node));
	assert("vs-758", item_is_extent(coord));
	assert("vs-1046", coord_is_existing_unit(coord));
	assert("vs-1045", znode_is_rlocked(coord->node));
	assert("vs-1047", page->mapping->host->i_ino == get_key_objectid(item_key_by_coord(coord, &key)));
	assert("vs-1320", coord_extension_is_ok(uf_coord));

	return do_readpage_extent(uf_coord->extension.extent.ext, uf_coord->extension.extent.pos_in_unit, page);
}

/*
  plugin->s.file.writepage
  At the beginning: coord.node is read locked, zloaded, page is
  locked, coord is set to existing unit inside of extent item
*/
int
writepage_extent(reiser4_key *key, uf_coord_t *uf_coord, struct page *page, write_mode_t mode)
{
	int result;
	jnode *j;

	ON_TRACE(TRACE_EXTENTS, "WP: index %lu, count %d..", page->index, page_count(page));

	assert("vs-1052", PageLocked(page));
	// assert("vs-1073", PageDirty(page));
	assert("vs-1051", page->mapping && page->mapping->host);
	assert("nikita-3139", !inode_get_flag(page->mapping->host, REISER4_NO_SD));
	assert("vs-864", znode_is_wlocked(uf_coord->base_coord.node));
	assert("vs-1398", get_key_objectid(key) == get_inode_oid(page->mapping->host));

	j = page_extent_jnode(current_tree, get_key_objectid(key), key, uf_coord, page, mode);
	if (IS_ERR(j))
		return PTR_ERR(j);
	JF_CLR(j, JNODE_NEW);
	result = try_capture_dirty_page(j, page);
	jput(j);
	reiser4_lock_page(page);

	ON_TRACE(TRACE_EXTENTS, "OK\n");
	return result;
}

/*
  plugin->u.item.s.file.get_block
*/
int get_block_address_extent(const uf_coord_t *uf_coord, sector_t block, struct buffer_head *bh)
{
	const extent_coord_extension_t *ext_coord;

	assert("vs-1321", coord_extension_is_ok(uf_coord));
	ext_coord = &uf_coord->extension.extent;

	if (state_of_extent(ext_coord->ext) != ALLOCATED_EXTENT)
		bh->b_blocknr = 0;
	else
		bh->b_blocknr = extent_get_start(ext_coord->ext) + ext_coord->pos_in_unit;
	return 0;
}

static int
readahead_readpage_extent(void *vp, struct page *page)
{
	int result;
	uf_coord_t *uf_coord;
	coord_t *coord;
	extent_coord_extension_t *ext_coord;

	uf_coord = vp;
	coord = &uf_coord->base_coord;

	if (coord->between != AT_UNIT) {
		reiser4_unlock_page(page);
		return RETERR(-EINVAL);
	}

	ext_coord = &uf_coord->extension.extent;
	if (ext_coord->expected_page != page->index) {
		/* read_cache_pages skipped few pages. Try to adjust coord to page */
		assert("vs-1269", page->index > ext_coord->expected_page);
		if (move_coord_pages(coord, ext_coord,  page->index - ext_coord->expected_page)) {
			/* extent pointing to this page is not here */
			reiser4_unlock_page(page);
			return RETERR(-EINVAL);
		}

		assert("vs-1274", offset_is_in_unit(coord,
						    (loff_t)page->index << PAGE_CACHE_SHIFT));
		ext_coord->expected_page = page->index;
	}
	
	assert("vs-1281", page->index == ext_coord->expected_page);
	result = do_readpage_extent(ext_coord->ext, ext_coord->pos_in_unit, page);
	if (!result)
		move_coord_pages(coord, ext_coord, 1);
	return result;
}

/*
  plugin->u.item.s.file.readpages
*/
void
readpages_extent(void *vp, struct address_space *mapping, struct list_head *pages)
{
	read_cache_pages(mapping, pages, readahead_readpage_extent, vp);
}

/*
  plugin->u.item.s.file.append_key
  key of first byte which is the next to last byte by addressed by this extent
*/ 
reiser4_key *
append_key_extent(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(key) + extent_size(coord, nr_units_extent(coord)));

	assert("vs-610", get_key_offset(key) && (get_key_offset(key) & (current_blocksize - 1)) == 0);
	return key;
}

/* plugin->u.item.s.file.init_coord_extension */
void
init_coord_extension_extent(uf_coord_t *uf_coord, loff_t lookuped)
{
	coord_t *coord;
	extent_coord_extension_t *ext_coord;
	reiser4_key key;
	loff_t offset;
	pos_in_item_t i;

	assert("vs-1295", uf_coord->valid == 0);

	coord = &uf_coord->base_coord;
	assert("vs-1288", coord_is_iplug_set(coord));
	assert("vs-1327", znode_is_loaded(coord->node));

	if (coord->between != AFTER_UNIT && coord->between != AT_UNIT)
		return;		

	ext_coord = &uf_coord->extension.extent;
	ext_coord->nr_units = nr_units_extent(coord);

	if (coord->between == AFTER_UNIT) {
		assert("vs-1330", coord->unit_pos == nr_units_extent(coord) - 1);
		ext_coord->ext = extent_by_coord(coord);
		ON_DEBUG(ext_coord->extent = *ext_coord->ext);
		ext_coord->width = extent_get_width(ext_coord->ext);
		ext_coord->pos_in_unit = ext_coord->width - 1;
		uf_coord->valid = 1;
		return;
	}

	/* AT_UNIT */
	item_key_by_coord(coord, &key);
	offset = get_key_offset(&key);

	/* FIXME: it would not be necessary if pos_in_unit were in coord_t */
	ext_coord->ext = extent_item(coord);

	for (i = 0; i < coord->unit_pos; i++, ext_coord->ext ++)
		offset += (extent_get_width(ext_coord->ext) * current_blocksize);
	ON_DEBUG(ext_coord->extent = *ext_coord->ext);
	ext_coord->width = extent_get_width(ext_coord->ext);

	assert("vs-1328", offset <= lookuped);
	assert("vs-1329", lookuped < offset + extent_get_width(ext_coord->ext) * current_blocksize);
	ext_coord->pos_in_unit = ((lookuped - offset) >> current_blocksize_bits);

	uf_coord->valid = 1;
}

/* plugin->u.item.f.scan */
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

	tree = current_tree;

	/* If the extent is allocated we have to check each of its blocks.  If the extent
	   is unallocated we can skip to the scan_max. */
	if (allocated) {
		do {
			neighbor = jlook_lock(tree, oid, scan_index);
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
		neighbor = jlook_lock(tree, oid, scan_max/*index*/);
		if (neighbor == NULL) {
			/* Race with truncate */
			scan->stop = 1;
			ret = 0;
			goto exit;
		}

		ON_TRACE(TRACE_FLUSH_VERB, "unalloc scan index %lu: %s\n", scan_index, jnode_tostring(neighbor));

		assert("jmacd-3551", !jnode_check_flushprepped(neighbor)
		       && same_atom_dirty(neighbor, scan->node, 0, 0));

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
	int ret = 0;

	ext = extent_by_coord(coord);

	if (state_of_extent(ext) == HOLE_EXTENT)
		return 0;

	width = extent_get_width(ext);
	start = extent_get_start(ext);
	index = extent_unit_index(coord);

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
			return -EAGAIN;
		}

		if (is_bad_inode(inode)) {
			iput(inode);
			return -EIO;
		}
	}

	for (nr_marked = 0, i = 0; nr_marked < max_nr_marked && i < width; i++) {
		jnode * node;
		struct page * page;

		ret = 0;
		/* Jnodes could not be available  */
		page = grab_cache_page(inode->i_mapping, index + i);
		if (page == NULL) {
			ret = -ENOMEM;
			break;
		}
		
		node = jnode_of_page(page);
		unlock_page(page);
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

				unformatted_jnode_make_dirty(node);
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
