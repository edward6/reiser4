/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/*
 * this file contains implementation of permission plugins. Currently, only
 * RWX_PERM_ID is implemented
 */

#include "../plugin.h"
#include "../plugin_header.h"
#include "../../debug.h"

perm_plugin perm_plugins[LAST_PERM_ID] = {
	[NULL_PERM_ID] = {
		.h = {
			.type_id = REISER4_PERM_PLUGIN_TYPE,
			.id = NULL_PERM_ID,
			.pops = NULL,
			.label = "null",
			.desc = "stub permission plugin",
			.linkage = {NULL, NULL}
		},
		.read_ok = NULL,
		.write_ok = NULL,
		.lookup_ok = NULL,
		.create_ok = NULL,
		.link_ok = NULL,
		.unlink_ok = NULL,
		.delete_ok = NULL,
		.mask_ok = NULL,
		.setattr_ok = NULL,
		.getattr_ok = NULL,
		.rename_ok = NULL,
	}
};

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
