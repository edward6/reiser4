/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definition of item plugins.
 */

#include "../../reiser4.h"

/** ->max_key_inside() method for items consisting of exactly one key (like
    stat-data) */
static reiser4_key *single_key( const tree_coord *coord /* coord of item */, 
				reiser4_key *result /* resulting key */ )
{
	assert( "nikita-604", coord != NULL );

	/* coord -> key is starting key of this item and it has to be already
	   filled in */
	return unit_key_by_coord( coord, result );
}

/** ->nr_units() method for items consisting of exactly one unit always */
static unsigned int single_unit( const tree_coord *coord UNUSED_ARG /* coord
								       of
								       item */ )
{
	return 1;
}

/** default ->fast_paste() method */
int agree_to_fast_op( const tree_coord *coord UNUSED_ARG /* coord of item */ )
{
	return 1;
}

int item_can_contain_key( const tree_coord *item /* coord of item */, 
			  const reiser4_key *key /* key to check */,
			  const reiser4_item_data *data /* parameters of item
							 * being created */ )
{
	common_item_plugin *iplug;
	reiser4_key min_key_in_item;
	reiser4_key max_key_in_item;
	
	assert( "nikita-1658", item != NULL );
	assert( "nikita-1659", key != NULL );

	iplug = item_plugin_by_coord( item );
	if( iplug -> can_contain_key != NULL )
		return iplug -> can_contain_key( item, key, data );
	else {
		assert( "nikita-1681", iplug -> max_key_inside != NULL );
		item_key_by_coord( item, &min_key_in_item );
		iplug -> max_key_inside( item, &max_key_in_item );
	
		/*
		 * can contain key if 
		 *    min_key_in_item <= key &&
		 *    key <= max_key_in_item
		 */
		return 
			( keycmp( &min_key_in_item, key ) != GREATER_THAN ) &&
			( keycmp( key, &max_key_in_item ) != GREATER_THAN );
	}
}

/* return 0 if @item1 and @item2 are not mergeable, !0 - otherwise */
int are_items_mergeable( const tree_coord *i1 /* coord of first item */, 
			 const tree_coord *i2 /* coord of second item */ )
{
	common_item_plugin *iplug;
	reiser4_key k1;
	reiser4_key k2;

	assert( "nikita-1336", i1 != NULL );
	assert( "nikita-1337", i2 != NULL );

	iplug = item_plugin_by_coord( i1 );
	assert( "nikita-1338", iplug != NULL );

	trace_if( TRACE_NODES, print_key( "k1", item_key_by_coord( i1, &k1 ) ) );
	trace_if( TRACE_NODES, print_key( "k2", item_key_by_coord( i2, &k2 ) ) );

	/*
	 * FIXME-NIKITA are_items_mergeable() is also called by assertions in
	 * shifting code when nodes are in "suspended" state.
	 */
	assert( "nikita-1663", 
		keycmp( item_key_by_coord( i1, &k1 ),
			item_key_by_coord( i2, &k2 ) ) != GREATER_THAN );

	if( iplug -> mergeable != NULL ) {
		return iplug -> mergeable( i1, i2 );
	} else if( iplug -> max_key_inside != NULL ) {
		iplug -> max_key_inside( i1, &k1 );
		item_key_by_coord( i2, &k2 );

		/*
		 * mergeable if ->max_key_inside() >= key of i2;
		 */
		return keycmp( iplug -> max_key_inside( i1, &k1 ), 
			       item_key_by_coord( i2, &k2 ) ) != LESS_THAN;
	} else {
		item_key_by_coord( i1, &k1 );
		item_key_by_coord( i2, &k2 );

		return 
			( get_key_locality( &k1 ) == get_key_locality( &k2 ) ) &&
			( get_key_objectid( &k1 ) == get_key_objectid( &k2 ) ) &&
			( iplug == item_plugin_by_coord( i2 ) );
	}
}

#if 0
int item_is_extent (const tree_coord *item)
{
	assert ("vs-482", coord_of_item (item));
	return item_plugin_by_coord (item)->item_plugin_id == EXTENT_POINTER_IT;
}


/* this returns true if item contains pointers to subtree of formatted nodes */
int item_is_internal (const tree_coord *item)
{
	assert ("vs-483", coord_of_item (item));
	return item_plugin_by_coord (item)->down_link;
}


int item_is_statdata (const tree_coord *item)
{
	common_item_plugin *common;

	assert ("vs-516", coord_of_item (item));
	common = item_plugin_by_coord (item);
	return (common->item_plugin_id == STATIC_STAT_DATA_ID);
}
#endif

reiser4_plugin item_plugins[ LAST_ITEM_ID ] = {
	[ STATIC_STAT_DATA_ID ] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id      = STATIC_STAT_DATA_ID,
			.pops    = NULL,
			.label   = "sd",
			.desc    = "stat-data",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.item = {
				.item_plugin_id = STATIC_STAT_DATA_ID,
				.max_key_inside = single_key,
				.can_contain_key = NULL,
				.mergeable      = NULL,
				.print          = sd_print,
				.check          = NULL,
				.nr_units       = single_unit,
				/* to need for ->lookup method */
				.lookup         = NULL,
				.init           = NULL,
				.paste          = NULL,
				.fast_paste     = NULL,
				.can_shift      = NULL,
				.copy_units     = NULL,
				.create_hook    = NULL,
				.kill_hook      = NULL,
				.shift_hook     = NULL,
				.cut_units      = NULL,
				.kill_units     = NULL,
				.unit_key       = NULL,
				.estimate       = NULL,
				.item_data_by_flow = NULL,
				.utmost_child   = NULL,
				.down_link      = NULL,
				.has_pointer_to = NULL
			}
		},
	},
	[ SIMPLE_DIR_ENTRY_ID ] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id      = SIMPLE_DIR_ENTRY_ID,
			.pops    = NULL,
			.label   = "de",
			.desc    = "directory entry",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.item = {
				.item_plugin_id = SIMPLE_DIR_ENTRY_ID,
				.max_key_inside = single_key,
				.can_contain_key = NULL,
				.mergeable      = NULL,
				.print          = de_print,
				.check          = NULL,
				.nr_units       = single_unit,
				/* to need for ->lookup method */
				.lookup         = NULL,
				.init           = NULL,
				.paste          = NULL,
				.fast_paste     = NULL,
				.can_shift      = NULL,
				.copy_units     = NULL,
				.create_hook    = NULL,
				.kill_hook      = NULL,
				.shift_hook     = NULL,
				.cut_units      = NULL,
				.kill_units     = NULL,
				.unit_key       = NULL,
				.estimate       = NULL,
				.item_data_by_flow = NULL,
				.utmost_child   = NULL,
				.down_link      = NULL,
				.has_pointer_to = NULL
			}
		}
	},
	[ COMPOUND_DIR_ID ] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id      = COMPOUND_DIR_ID,
			.pops    = NULL,
			.label   = "cde",
			.desc    = "compressed directory entry",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.item = {
				.item_plugin_id = COMPOUND_DIR_ID,
				.max_key_inside = cde_max_key_inside,
				.can_contain_key = cde_can_contain_key,
				.mergeable      = cde_mergeable,
				.print          = cde_print,
				.check          = cde_check,
				.nr_units       = cde_nr_units,
				.lookup         = cde_lookup,
				.init           = cde_init,
				.paste          = cde_paste,
				.fast_paste     = agree_to_fast_op,
				.can_shift      = cde_can_shift,
				.copy_units     = cde_copy_units,
				.create_hook    = NULL,
				.kill_hook      = NULL,
				.shift_hook     = NULL,
				.cut_units      = cde_cut_units,
				.kill_units     = cde_cut_units,
				.unit_key       = cde_unit_key,
				.estimate       = cde_estimate,
				.item_data_by_flow = NULL,
				.utmost_child   = NULL,
				.down_link      = NULL,
				.has_pointer_to = NULL
			}
		}
	},
	[ NODE_POINTER_ID ] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id      = NODE_POINTER_ID,
			.pops    = NULL,
			.label   = "internal",
			.desc    = "internal item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.item = {
				.item_plugin_id    = NODE_POINTER_ID,
				.max_key_inside    = NULL,
				.can_contain_key   = NULL,
				.mergeable         = internal_mergeable,
				.print             = internal_print,
				.check             = NULL,
				.nr_units          = single_unit,
				.lookup            = internal_lookup,
				.init              = NULL,
				.paste             = NULL,
				.fast_paste        = NULL,
				.can_shift         = NULL,
				.copy_units        = NULL,
				.create_hook       = internal_create_hook,
				.kill_hook         = internal_kill_hook,
				.shift_hook        = internal_shift_hook,
				.cut_units         = NULL,
				.kill_units        = NULL,
				.unit_key          = NULL,
				.estimate          = NULL,
				.item_data_by_flow = NULL,
				.utmost_child      = internal_utmost_child,
				.down_link         = internal_down_link,
				.has_pointer_to    = internal_has_pointer_to
			}
		}
	},
	[ EXTENT_POINTER_ID ] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id      = EXTENT_POINTER_ID,
			.pops    = NULL,
			.label   = "extent",
			.desc    = "extent item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.item = {
				.item_plugin_id = EXTENT_POINTER_ID,
				.max_key_inside = extent_max_key_inside,
				.can_contain_key = extent_can_contain_key,
				.mergeable      = extent_mergeable,
				.print          = extent_print,
				.check          = NULL,
				.nr_units       = extent_nr_units,
				.lookup         = extent_lookup,
				.init           = NULL,
				.paste          = extent_paste,
				.fast_paste     = agree_to_fast_op,
				.can_shift      = extent_can_shift,
				.create_hook    = extent_create_hook,
				.copy_units     = extent_copy_units,
				.kill_hook      = extent_kill_item_hook,
				.shift_hook     = NULL,
				.cut_units      = extent_cut_units,
				.kill_units     = extent_kill_units,
				.unit_key       = extent_unit_key,
				.estimate       = NULL,
				.item_data_by_flow = extent_item_data_by_flow,
				.utmost_child   = extent_utmost_child,
				.down_link      = NULL,
				.has_pointer_to = NULL
			}
		}
	},
	[ TAIL_ID ] = {
		.h = {
			.type_id = REISER4_ITEM_PLUGIN_TYPE,
			.id      = TAIL_ID,
			.pops    = NULL,
			.label   = "body",
			.desc    = "body (or tail?) item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.item = {
				.item_plugin_id = TAIL_ID,
				.max_key_inside = tail_max_key_inside,
				.can_contain_key = tail_can_contain_key,
				.mergeable      = tail_mergeable,
				.print          = NULL,
				.check          = NULL,
				.nr_units       = tail_nr_units,
				.lookup         = tail_lookup,
				.init           = NULL,
				.paste          = tail_paste,
				.fast_paste     = agree_to_fast_op,
				.can_shift      = tail_can_shift,
				.create_hook    = NULL,
				.copy_units     = tail_copy_units,
				.kill_hook      = NULL,
				.shift_hook     = NULL,
				.cut_units      = tail_cut_units,
				.kill_units     = tail_cut_units,
				.unit_key       = tail_unit_key,
				.estimate       = NULL,
				.item_data_by_flow = extent_item_data_by_flow,
				.utmost_child   = NULL,
				.down_link      = NULL,
				.has_pointer_to = NULL
			}
		}
	}
};



/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
