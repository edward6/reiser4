/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"

/* initialization of objectid management plugins */
reiser4_plugin oid_plugins[ LAST_OID_ALLOCATOR_ID ] = {
	[ OID_40_ALLOCATOR_ID ] = {
		.oid_allocator = {
			.h = {
				.type_id = REISER4_OID_ALLOCATOR_PLUGIN_TYPE,
				.id      = OID_40_ALLOCATOR_ID,
				.pops    = NULL,
				.label   = "reiser40 default oid manager",
				.desc    = "no reusing objectids",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.init_oid_allocator   = oid_40_read_allocator,
			.oids_used            = oid_40_used,
			.oids_free            = oid_40_free,
			.allocate_oid         = oid_40_allocate,
			.release_oid          = oid_40_release,
			.oid_reserve_allocate = oid_40_reserve_allocate,
			.oid_reserve_release  = oid_40_reserve_release,
			.print_info           = oid_40_print_info
		}
	}
};
