/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* definition of item plugins. */

#include "../../forward.h"
#include "../../debug.h"
#include "../../key.h"
#include "../../coord.h"
#include "../plugin_header.h"
#include "sde.h"
/*#include "tail.h"*/
#include "../cryptcompress.h"
#include "internal.h"
#include "item.h"
/*#include "extent.h"*/
#include "static_stat.h"
#include "../plugin.h"
#include "../../znode.h"
#include "../../tree.h"
#include "ctail.h"

/* return pointer to item body */
void
item_body_by_coord_hard(coord_t * coord /* coord to query */ )
{
	assert("nikita-324", coord != NULL);
	assert("nikita-325", coord->node != NULL);
	assert("nikita-326", znode_is_loaded(coord->node));
	assert("nikita-3200", coord->body == NULL);
	trace_stamp(TRACE_TREE);

	coord->body = node_plugin_by_node(coord->node)->item_by_coord(coord);
}

int item_body_is_valid(const coord_t * coord)
{
	return
		coord->body == 
		node_plugin_by_node(coord->node)->item_by_coord(coord);
}

/* return length of item at @coord */
/* Audited by: green(2002.06.15) */
int
item_length_by_coord(const coord_t * coord /* coord to query */ )
{
	assert("nikita-327", coord != NULL);
	assert("nikita-328", coord->node != NULL);
	assert("nikita-329", znode_is_loaded(coord->node));
	trace_stamp(TRACE_TREE);

	return node_plugin_by_node(coord->node)->length_by_coord(coord);
}

void
obtain_item_plugin(const coord_t * coord)
{
	assert("nikita-330", coord != NULL);
	assert("nikita-331", coord->node != NULL);
	assert("nikita-332", znode_is_loaded(coord->node));
	trace_stamp(TRACE_TREE);

	coord_set_iplug((coord_t *) coord,
			node_plugin_by_node(coord->node)->plugin_by_coord(coord));
	assert("nikita-2479", 
	       coord_iplug(coord) == node_plugin_by_node(coord->node)->plugin_by_coord(coord));
}

/* return type of item at @coord */
item_type_id item_type_by_coord(const coord_t * coord /* coord to query */ )
{
	assert("nikita-333", coord != NULL);
	assert("nikita-334", coord->node != NULL);
	assert("nikita-335", znode_is_loaded(coord->node));
	assert("nikita-336", item_plugin_by_coord(coord) != NULL);

	trace_stamp(TRACE_TREE);

	return item_plugin_by_coord(coord)->b.item_type;
}

/* return id of item */
/* Audited by: green(2002.06.15) */
item_id item_id_by_coord(const coord_t * coord /* coord to query */ )
{
	assert("vs-539", coord != NULL);
	assert("vs-538", coord->node != NULL);
	assert("vs-537", znode_is_loaded(coord->node));
	assert("vs-536", item_plugin_by_coord(coord) != NULL);

	trace_stamp(TRACE_TREE);

	assert("vs-540", item_id_by_plugin(item_plugin_by_coord(coord)) < LAST_ITEM_ID);
	return item_id_by_plugin(item_plugin_by_coord(coord));
}

/* return key of item at @coord */
/* Audited by: green(2002.06.15) */
reiser4_key *
item_key_by_coord(const coord_t * coord /* coord to query */ ,
		  reiser4_key * key /* result */ )
{
	assert("nikita-338", coord != NULL);
	assert("nikita-339", coord->node != NULL);
	assert("nikita-340", znode_is_loaded(coord->node));
	trace_stamp(TRACE_TREE);

	return node_plugin_by_node(coord->node)->key_at(coord, key);
}

/* return key of unit at @coord */
reiser4_key *
unit_key_by_coord(const coord_t * coord /* coord to query */ ,
		  reiser4_key * key /* result */ )
{
	assert("nikita-772", coord != NULL);
	assert("nikita-774", coord->node != NULL);
	assert("nikita-775", znode_is_loaded(coord->node));
	trace_stamp(TRACE_TREE);

	if (item_plugin_by_coord(coord)->b.unit_key != NULL)
		return item_plugin_by_coord(coord)->b.unit_key(coord, key);
	else
		return item_key_by_coord(coord, key);
}

/* ->max_key_inside() method for items consisting of exactly one key (like
    stat-data) */
static reiser4_key *
max_key_inside_single_key(const coord_t * coord /* coord of item */ ,
			  reiser4_key * result /* resulting key */)
{
	assert("nikita-604", coord != NULL);

	/* coord -> key is starting key of this item and it has to be already
	   filled in */
	return unit_key_by_coord(coord, result);
}

/* ->nr_units() method for items consisting of exactly one unit always */
static pos_in_item_t
nr_units_single_unit(const coord_t * coord UNUSED_ARG	/* coord of item */ )
{
	return 1;
}

static int
paste_no_paste(coord_t * coord UNUSED_ARG, 
	       reiser4_item_data * data UNUSED_ARG, 
	       carry_plugin_info * info UNUSED_ARG)
{
	return 0;
}

/* default ->fast_paste() method */
int
agree_to_fast_op(const coord_t * coord UNUSED_ARG /* coord of item */ )
{
	return 1;
}

int
item_can_contain_key(const coord_t * item /* coord of item */ ,
		     const reiser4_key * key /* key to check */ ,
		     const reiser4_item_data * data	/* parameters of item
							 * being created */ )
{
	item_plugin *iplug;
	reiser4_key min_key_in_item;
	reiser4_key max_key_in_item;

	assert("nikita-1658", item != NULL);
	assert("nikita-1659", key != NULL);

	iplug = item_plugin_by_coord(item);
	if (iplug->b.can_contain_key != NULL)
		return iplug->b.can_contain_key(item, key, data);
	else {
		assert("nikita-1681", iplug->b.max_key_inside != NULL);
		item_key_by_coord(item, &min_key_in_item);
		iplug->b.max_key_inside(item, &max_key_in_item);

		/* can contain key if 
		      min_key_in_item <= key &&
		      key <= max_key_in_item
		*/
		return keyle(&min_key_in_item, key) && keyle(key, &max_key_in_item);
	}
}

/* return 0 if @item1 and @item2 are not mergeable, !0 - otherwise */
int
are_items_mergeable(const coord_t * i1 /* coord of first item */ ,
		    const coord_t * i2 /* coord of second item */ )
{
	item_plugin *iplug;
	reiser4_key k1;
	reiser4_key k2;

	assert("nikita-1336", i1 != NULL);
	assert("nikita-1337", i2 != NULL);

	iplug = item_plugin_by_coord(i1);
	assert("nikita-1338", iplug != NULL);

	IF_TRACE(TRACE_NODES, print_key("k1", item_key_by_coord(i1, &k1)));
	IF_TRACE(TRACE_NODES, print_key("k2", item_key_by_coord(i2, &k2)));

	/* NOTE-NIKITA are_items_mergeable() is also called by assertions in
	   shifting code when nodes are in "suspended" state. */
	assert("nikita-1663", keyle(item_key_by_coord(i1, &k1), item_key_by_coord(i2, &k2)));

	if (iplug->b.mergeable != NULL) {
		return iplug->b.mergeable(i1, i2);
	} else if (iplug->b.max_key_inside != NULL) {
		iplug->b.max_key_inside(i1, &k1);
		item_key_by_coord(i2, &k2);

		/* mergeable if ->max_key_inside() >= key of i2; */
		return keyge(iplug->b.max_key_inside(i1, &k1), item_key_by_coord(i2, &k2));
	} else {
		item_key_by_coord(i1, &k1);
		item_key_by_coord(i2, &k2);

		return
		    (get_key_locality(&k1) == get_key_locality(&k2)) &&
		    (get_key_objectid(&k1) == get_key_objectid(&k2)) && (iplug == item_plugin_by_coord(i2));
	}
}

int
item_is_extent(const coord_t * item)
{
	assert("vs-482", coord_is_existing_item(item));
	return item_id_by_coord(item) == EXTENT_POINTER_ID || item_id_by_coord(item) == FROZEN_EXTENT_POINTER_ID;
}

int
item_is_tail(const coord_t * item)
{
	assert("vs-482", coord_is_existing_item(item));
	return item_id_by_coord(item) == TAIL_ID || item_id_by_coord(item) == FROZEN_TAIL_ID;
}

int
item_is_statdata(const coord_t * item)
{
	assert("vs-516", coord_is_existing_item(item));
	return item_type_by_coord(item) == STAT_DATA_ITEM_TYPE;
}

/*
  FIXME-VS: a description of what frozen items are, where are they used and what are 3 functions below goes here
*/

static int
mergeable_frozen(const coord_t * p1 UNUSED_ARG, const coord_t * p2 UNUSED_ARG)
{
	return 0;
}

/* plugin->u.item.b.paste
   this should not be called */
static int
paste_frozen(coord_t * coord UNUSED_ARG, reiser4_item_data * data UNUSED_ARG, carry_plugin_info * info UNUSED_ARG)
{
	impossible("vs-1122", "pasting into partially converted file\n");
	return 0;
}

/* plugin->u.item.b.can_shift */
static int
can_shift_frozen(unsigned free_space UNUSED_ARG, coord_t * source UNUSED_ARG,
		 znode * target UNUSED_ARG, shift_direction direction UNUSED_ARG,
		 unsigned *size, unsigned want UNUSED_ARG)
{
	*size = 0;
	return 0;
}

item_plugin item_plugins[LAST_ITEM_ID] = {
	[STATIC_STAT_DATA_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = STATIC_STAT_DATA_ID,
			.pops = NULL,
			.label = "sd",
			.desc = "stat-data",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = STAT_DATA_ITEM_TYPE,
			.max_key_inside = max_key_inside_single_key,
			.can_contain_key = NULL,
			.mergeable = NULL,
			.nr_units = nr_units_single_unit,
			/* to need for ->lookup method */
			.lookup = NULL,
			.init = NULL,
			.paste = paste_no_paste,
			.fast_paste = NULL,
			.can_shift = NULL,
			.copy_units = NULL,
			.create_hook = NULL,
			.kill_hook = NULL,
			.shift_hook = NULL,
			.cut_units = NULL,
			.kill_units = NULL,
			.unit_key = NULL,
			.estimate = NULL,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = print_sd,
			.item_stat = item_stat_static_sd
#endif
#if REISER4_DEBUG
			, .check = NULL
#endif
		},
		.f = {
			.utmost_child            = NULL,
			.utmost_child_real_block = NULL,
			.update                  = NULL,
			.scan                    = NULL,
			.squeeze                 = NULL
		},
		.s = {
			.sd = {
				.init_inode = init_inode_static_sd,
				.save_len = save_len_static_sd,
				.save = save_static_sd
			}
		}
	},
	[SIMPLE_DIR_ENTRY_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = SIMPLE_DIR_ENTRY_ID,
			.pops = NULL,
			.label = "de",
			.desc = "directory entry",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = DIR_ENTRY_ITEM_TYPE,
			.max_key_inside = max_key_inside_single_key,
			.can_contain_key = NULL,
			.mergeable = NULL,
			.nr_units = nr_units_single_unit,
			/* to need for ->lookup method */
			.lookup = NULL,
			.init = NULL,
			.paste = NULL,
			.fast_paste = NULL,
			.can_shift = NULL,
			.copy_units = NULL,
			.create_hook = NULL,
			.kill_hook = NULL,
			.shift_hook = NULL,
			.cut_units = NULL,
			.kill_units = NULL,
			.unit_key = NULL,
			.estimate = NULL,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = print_de,
			.item_stat = NULL
#endif
#if REISER4_DEBUG
			, .check = NULL
#endif
		},
		.f = {
			.utmost_child            = NULL,
			.utmost_child_real_block = NULL,
			.update                  = NULL,
			.scan                    = NULL,
			.squeeze                 = NULL
		},
		.s = {
			.dir = {
				.extract_key = extract_key_de,
				.update_key = update_key_de,
				.extract_name = extract_name_de,
				.extract_file_type = extract_file_type_de,
				.add_entry = add_entry_de,
				.rem_entry = rem_entry_de,
				.max_name_len = max_name_len_de
			}
		}
	},
	[COMPOUND_DIR_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = COMPOUND_DIR_ID,
			.pops = NULL,
			.label = "cde",
			.desc = "compressed directory entry",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = DIR_ENTRY_ITEM_TYPE,
			.max_key_inside = max_key_inside_cde,
			.can_contain_key = can_contain_key_cde,
			.mergeable = mergeable_cde,
			.nr_units = nr_units_cde,
			.lookup = lookup_cde,
			.init = init_cde,
			.paste = paste_cde,
			.fast_paste = agree_to_fast_op,
			.can_shift = can_shift_cde,
			.copy_units = copy_units_cde,
			.create_hook = NULL,
			.kill_hook = NULL,
			.shift_hook = NULL,
			.cut_units = cut_units_cde,
			.kill_units = cut_units_cde,
			.unit_key = unit_key_cde,
			.estimate = estimate_cde,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = print_cde,
			.item_stat = NULL
#endif
#if REISER4_DEBUG
			, .check = check_cde
#endif
		},
		.f = {
			.utmost_child            = NULL,
			.utmost_child_real_block = NULL,
			.update                  = NULL,
			.scan                    = NULL,
			.squeeze                 = NULL
		},
		.s = {
			.dir = {
				.extract_key = extract_key_cde,
				.update_key = update_key_cde,
				.extract_name = extract_name_cde,
				.extract_file_type = extract_file_type_de,
				.add_entry = add_entry_cde,
				.rem_entry = rem_entry_cde,
				.max_name_len = max_name_len_cde
			}
		}
	},
	[NODE_POINTER_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = NODE_POINTER_ID,
			.pops = NULL,
			.label = "internal",
			.desc = "internal item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = INTERNAL_ITEM_TYPE,
			.max_key_inside = NULL,
			.can_contain_key = NULL,
			.mergeable = mergeable_internal,
			.nr_units = nr_units_single_unit,
			.lookup = lookup_internal,
			.init = NULL,
			.paste = NULL,
			.fast_paste = NULL,
			.can_shift = NULL,
			.copy_units = NULL,
			.create_hook = create_hook_internal,
			.kill_hook = kill_hook_internal,
			.shift_hook = shift_hook_internal,
			.cut_units = NULL,
			.kill_units = NULL,
			.unit_key = NULL,
			.estimate = NULL,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = print_internal,
			.item_stat = NULL
#endif
#if REISER4_DEBUG
			, .check = check__internal
#endif
		},
		.f = {
			.utmost_child            = utmost_child_internal,
			.utmost_child_real_block = utmost_child_real_block_internal,
			.update                  = update_internal,
			.scan                    = NULL,
			.squeeze                 = NULL
		},
		.s = {
			.internal = {
				.down_link = down_link_internal,
				.has_pointer_to = has_pointer_to_internal
			}
		}
	},
	[EXTENT_POINTER_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = EXTENT_POINTER_ID,
			.pops = NULL,
			.label = "extent",
			.desc = "extent item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = ORDINARY_FILE_METADATA_TYPE,
			.max_key_inside = max_key_inside_extent,
			.can_contain_key = can_contain_key_extent,
			.mergeable = mergeable_extent,
			.nr_units = nr_units_extent,
			.lookup = lookup_extent,
			.init = NULL,
			.paste = paste_extent,
			.fast_paste = agree_to_fast_op,
			.can_shift = can_shift_extent,
			.create_hook = create_hook_extent,
			.copy_units = copy_units_extent,
			.kill_hook = kill_hook_extent,
			.shift_hook = NULL,
			.cut_units = cut_units_extent,
			.kill_units = kill_units_extent,
			.unit_key = unit_key_extent,
			.show = show_extent,
			.estimate = NULL,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = print_extent,
			.item_stat = item_stat_extent
#endif
#if REISER4_DEBUG
			, .check = check_extent
#endif
		},
		.f = {
			.utmost_child            = utmost_child_extent,
			.utmost_child_real_block = utmost_child_real_block_extent,
			.update                  = NULL,
			.scan                    = scan_extent,
			.squeeze                 = NULL
		},
		.s = {
			.file = {
				.write = write_extent,
				.read = read_extent,
				.readpage = readpage_extent,
				.writepage = writepage_extent,
				.get_block = get_block_address_extent,
				.readpages = readpages_extent,
				.append_key = append_key_extent,
				.init_coord_extension = init_coord_extension_extent
/*
#if REISER4_DEBUG
				, .key_in_item = key_in_item_extent
#endif
*/
			}
		}
	},
	[TAIL_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = TAIL_ID,
			.pops = NULL,
			.label = "body",
			.desc = "body (or tail?) item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = ORDINARY_FILE_METADATA_TYPE,
			.max_key_inside = max_key_inside_tail,
			.can_contain_key = can_contain_key_tail,
			.mergeable = mergeable_tail,
			.nr_units = nr_units_tail,
			.lookup = lookup_tail,
			.init = NULL,
			.paste = paste_tail,
			.fast_paste = agree_to_fast_op,
			.can_shift = can_shift_tail,
			.create_hook = NULL,
			.copy_units = copy_units_tail,
			.kill_hook = NULL,
			.shift_hook = NULL,
			.cut_units = cut_units_tail,
			.kill_units = cut_units_tail,
			.unit_key = unit_key_tail,
			.estimate = NULL,
			.show = show_tail,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = NULL,
			.item_stat = NULL
#endif
#if REISER4_DEBUG
			, .check = NULL
#endif
		},
		.f = {
			.utmost_child            = NULL,
			.utmost_child_real_block = NULL,
			.update                  = NULL,
			.scan                    = NULL,
			.squeeze                 = NULL
		},
		.s = {
			.file = {
				.write = write_tail,
				.read = read_tail,
				.readpage = NULL,
				.writepage = NULL,
				.get_block = NULL,
				.readpages = NULL,
				.append_key = append_key_tail,
				.init_coord_extension = init_coord_extension_tail
/*
#if REISER4_DEBUG
				, .key_in_item = key_in_item_tail
#endif
*/
			}
		}
	},
	[CTAIL_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = CTAIL_ID,
			.pops = NULL,
			.label = "ctail",
			.desc = "cryptcompress tail item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = ORDINARY_FILE_METADATA_TYPE,
			.max_key_inside = max_key_inside_tail,
			.can_contain_key = can_contain_key_ctail,
			.mergeable = mergeable_ctail,
			.nr_units = nr_units_ctail,
			.lookup = NULL,
			.init = NULL,
			.paste = paste_ctail,
			.fast_paste = agree_to_fast_op,
			.can_shift = can_shift_ctail,
			.create_hook = NULL,
			.copy_units = copy_units_ctail,
			.kill_hook = kill_hook_ctail,
			.shift_hook = NULL,
			.cut_units = cut_units_ctail,
			.kill_units = cut_units_ctail,
			.unit_key = unit_key_tail,
			.estimate = estimate_ctail,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = print_ctail,
			.item_stat = NULL
#endif
#if REISER4_DEBUG
			, .check = NULL
#endif
		},
		.f = {
			.utmost_child            = utmost_child_ctail,
			/* FIXME-EDWARD: write this */
			.utmost_child_real_block = NULL,
			.update                  = NULL,
			.scan                    = scan_ctail,
			.squeeze                 = squeeze_ctail
		},
		.s = {
			.file = {
				.write = NULL,
				.read = read_ctail,
				.readpage = readpage_ctail,
				.writepage = writepage_ctail,
				.get_block = get_block_address_tail,
				.readpages = readpages_ctail,
				.append_key = append_key_ctail,
				.init_coord_extension = init_coord_extension_tail
/*
#if REISER4_DEBUG
				, .key_in_item = key_in_item_ctail
#endif
*/
			}
		}
	},	
	/* the below two items are used in tail conversion. Before tail conversion starts all items of file becomes
	   frozen. That is nothing of them can be shifted to another node. This guarantees that estimation/reservation
	   remains valid during tail conversion operation */
	[FROZEN_TAIL_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = FROZEN_TAIL_ID,
			.pops = NULL,
			.label = "frozen tail",
			.desc = "non split-able tail item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = ORDINARY_FILE_METADATA_TYPE,
			.max_key_inside = max_key_inside_tail,
			.can_contain_key = can_contain_key_tail,
			.mergeable = mergeable_frozen,
			.nr_units = nr_units_tail,
			.lookup = lookup_tail,
			.init = NULL,
			.paste = paste_frozen,
			.fast_paste = agree_to_fast_op,
			.can_shift = can_shift_frozen,
			.create_hook = NULL,
			.copy_units = copy_units_tail,
			.kill_hook = NULL,
			.shift_hook = NULL,
			.cut_units = cut_units_tail,
			.kill_units = cut_units_tail,
			.unit_key = unit_key_tail,
			.show = show_tail,
			.estimate = NULL,
			.item_data_by_flow = NULL
#if REISER4_DEBUG_OUTPUT
			, .print = NULL,
			.item_stat = NULL
#endif
#if REISER4_DEBUG
			, .check = NULL
#endif
		},
		.f = {
			.utmost_child            = NULL,
			.utmost_child_real_block = NULL,
			.update                  = NULL,
			.scan                    = NULL,
			.squeeze                 = NULL
		},
		.s = {
			.file = {
				.write = NULL,
				.read = NULL,
				.readpage = NULL,
				.writepage = NULL,
				.readpages = NULL,
				.get_block = NULL,
				.append_key = append_key_tail,
				.init_coord_extension = init_coord_extension_tail
/*
#if REISER4_DEBUG
				, .key_in_item = key_in_item_tail
#endif
*/
			}
		}
	},
	[FROZEN_EXTENT_POINTER_ID] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id = FROZEN_EXTENT_POINTER_ID,
			.pops = NULL,
			.label = "frozen extent",
			.desc = "non split-able extent item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.b = {
			.item_type = ORDINARY_FILE_METADATA_TYPE,
			.max_key_inside = max_key_inside_extent,
			.can_contain_key = can_contain_key_extent,
			.mergeable = mergeable_frozen,
			.nr_units = nr_units_extent,
			.lookup = lookup_extent,
			.init = NULL,
			.paste = paste_frozen,
			.fast_paste = agree_to_fast_op,
			.can_shift = can_shift_frozen,
			.create_hook = create_hook_extent,
			.copy_units = copy_units_extent,
			.kill_hook = kill_hook_extent,
			.shift_hook = NULL,
			.cut_units = cut_units_extent,
			.kill_units = kill_units_extent,
			.unit_key = unit_key_extent,
			.show = show_extent,
			.estimate = NULL,
			.item_data_by_flow = NULL,
#if REISER4_DEBUG_OUTPUT
			.print = print_extent,
			.item_stat = item_stat_extent,
#endif
#if REISER4_DEBUG
			.check = check_extent
#endif
		},
		.f = {
			.utmost_child = utmost_child_extent,
			.utmost_child_real_block = utmost_child_real_block_extent,
			.update                  = NULL,
			.scan                    = scan_extent,
			.squeeze                 = NULL
		},
		.s = {
			.file = {
				.write = NULL,
				.read = NULL,
				.readpage = readpage_extent,
				.readpages = NULL,
				.writepage = NULL,
				.get_block = NULL,
				.append_key = append_key_extent,
				.init_coord_extension = init_coord_extension_extent
/*
#if REISER4_DEBUG
				, .key_in_item = key_in_item_extent
#endif
*/
			}
		}
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
