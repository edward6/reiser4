/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definition of item plugins.
 */

#include "../../reiser4.h"

reiser4_plugin perm_plugins[] = {
	[ RWX_PERM_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_PERM_PLUGIN_ID,
			.id      = RWX_PERM_ID,
			.pops    = NULL,
			.label   = "rwx",
			.desc    = "standard UNIX permissions",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.u = {
			.perm = {
				.rw_ok     = NULL,
				.lookup_ok = NULL,
				.create_ok = NULL,
				.link_ok   = NULL,
				.unlink_ok = NULL,
				.delete_ok = NULL,
				/* smart thing */
				.mask_ok   = vfs_permission
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
