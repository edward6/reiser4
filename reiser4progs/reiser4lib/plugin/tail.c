/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Tail policy plugins
 */

#include "../reiser4.h"

static int never_tail( const struct inode *inode UNUSED_ARG, 
		       loff_t size UNUSED_ARG )
{
	return 0;
}

static int always_tail( const struct inode *inode UNUSED_ARG, 
			loff_t size UNUSED_ARG )
{
	return 1;
}

reiser4_plugin tail_plugins[ LAST_TAIL_ID ] = {
	[ NEVER_TAIL_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_TAIL_PLUGIN_ID,
			.id      = NEVER_TAIL_ID,
			.pops    = NULL,
			.label   = "never",
			.desc    = "Never store file's tail as direct item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.tail = {
				.tail = never_tail
			}
		}
	},
	[ ALWAYS_TAIL_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_TAIL_PLUGIN_ID,
			.id      = ALWAYS_TAIL_ID,
			.pops    = NULL,
			.label   = "always",
			.desc    = "Always store file's tail as direct item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.tail = {
				.tail = always_tail
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
