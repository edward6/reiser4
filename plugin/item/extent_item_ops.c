/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "item.h"
#include "../../inode.h"
#include "../../tree_walk.h" /* check_sibling_list() */

/* item_plugin->b.max_key_inside */
reiser4_key *
max_key_inside_extent(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, get_key_offset(max_key()));
	return key;
}

/* item_plugin->b.can_contain_key
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

/* item_plugin->b.mergeable
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

/* item_plugin->b.show */
void
show_extent(struct seq_file *m, coord_t *coord)
{
	reiser4_extent *ext;
	ext = extent_by_coord(coord);
	seq_printf(m, "%Lu %Lu", extent_get_start(ext), extent_get_width(ext));
}


#if REISER4_DEBUG_OUTPUT

/* item_plugin->b.print */
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

	case UNALLOCATED_EXTENT2:
		label = "unalloc2";
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

/* item_plugin->b.item_stat */
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
		case UNALLOCATED_EXTENT2:
			assert("vs-1419", 0);
		}
	}
}

#endif /* REISER4_DEBUG_OUTPUT */

/* item_plugin->b.nr_units */
pos_in_item_t
nr_units_extent(const coord_t *coord)
{
	/* length of extent item has to be multiple of extent size */
	assert("vs-1424", (item_length_by_coord(coord) % sizeof (reiser4_extent)) == 0);
	return item_length_by_coord(coord) / sizeof (reiser4_extent);
}

/* item_plugin->b.lookup */
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
	
	assert("umka-99945",
	        !keygt(key, max_key_inside_extent(coord, &item_key)));

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

/* item_plugin->b.paste
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

/* item_plugin->b.can_shift */
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

/* item_plugin->b.copy_units */
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

/* item_plugin->b.create_hook
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
			/* FIXME: eflushed nodes get removed by truncate_inode_jnodes */
			assert("vs-1433", 0);
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

static int
truncate_inode_jnodes(struct inode *inode, unsigned long from)
{
	reiser4_inode *r4_inode;
	jnode *jnodes[16];
	int i, nr;
	int truncated_jnodes;
	
	truncated_jnodes = 0;
	r4_inode = reiser4_inode_data(inode);
	
	while ((nr = radix_tree_gang_lookup(&r4_inode->jnode_tree, (void **)jnodes, from, 16)) != 0) {
		for (i = 0; i < nr; i ++) {
			uncapture_jnode(jnodes[i]);
		}
		truncated_jnodes += nr;
	}
	return truncated_jnodes;
}

static int
truncate_inode_jnodes_range(struct inode *inode, unsigned long from, int count)
{
	int i;
	reiser4_inode *r4_inode;
	jnode *node;
	int truncated_jnodes;

	truncated_jnodes = 0;
	r4_inode = reiser4_inode_data(inode);
	for (i = 0; i < count; i ++) {
		node = radix_tree_lookup(&r4_inode->jnode_tree, from + i);
		if (node) {
			uncapture_jnode(node);
			truncated_jnodes ++;
		}
	}
	return truncated_jnodes;
}

/* item_plugin->b.kill_hook
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

	assert ("zam-811", znode_is_write_locked(coord->node));
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

		WLOCK_TREE(tree);
		link_left_and_right(left, right);
		WUNLOCK_TREE(tree);

		if (right != NULL)
			UNDER_RW_VOID(dk, tree, write,
				      update_znode_dkeys(left, right));
	}

	if (inode != NULL) {
		truncate_inode_pages(inode->i_mapping, offset);
		truncate_inode_jnodes(inode, index);
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
		  int cut, const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed,
		  struct cut_list *cl)
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
			/* kill */
			loff_t start;
			loff_t end;
			long nr_pages;

			/* round @start upward */
			start = round_up(get_key_offset(from_key),
					 PAGE_CACHE_SIZE);
			/* convert to page index */
			start >>= PAGE_CACHE_SHIFT;
			/* index of last page which is to be truncated */
			/* FIXME: this looses high bits. Fortunately on i386 they can not be set */
			end   = get_key_offset(to_key) >> PAGE_CACHE_SHIFT;
			/* number of completely removed pages */
			nr_pages = end - start + 1;
			truncate_mapping_pages_range(inode->i_mapping,
						     start, nr_pages);
			/* detach jnodes from inode's tree of jnodes */
			truncate_inode_jnodes_range(inode, start, nr_pages);
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
		/* call kill hook for all extents removed completely */
		cl->inode = NULL;
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

/* item_plugin->b.cut_units */
int
cut_units_extent(coord_t *item, unsigned *from, unsigned *to,
		 const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed,
		 struct cut_list *p)
{
	return cut_or_kill_units(item, from, to, 1, from_key, to_key, smallest_removed, p);
}

/* item_plugin->b.kill_units */
int
kill_units_extent(coord_t *item, unsigned *from, unsigned *to,
		  const reiser4_key *from_key, const reiser4_key *to_key, reiser4_key *smallest_removed,
		  struct cut_list *p)
{
	return cut_or_kill_units(item, from, to, 0, from_key, to_key, smallest_removed, p);
}

/* item_plugin->b.unit_key */
reiser4_key *
unit_key_extent(const coord_t *coord, reiser4_key *key)
{
	assert("vs-300", coord_is_existing_unit(coord));

	item_key_by_coord(coord, key);
	set_key_offset(key, (get_key_offset(key) + extent_size(coord, (unsigned) coord->unit_pos)));

	return key;
}

/* item_plugin->b.estimate
   item_plugin->b.item_data_by_flow */

#if REISER4_DEBUG_OUTPUT


#endif /* REISER4_DEBUG_OUTPUT */


#if REISER4_DEBUG

/* item_plugin->b.check
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
				node = jlookup(tree, oid, index + j);
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
