/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Tail policy plugins
 */

/*
 * Tail policy is used by object plugin (of regular file) to convert file
 * between two representations. TO BE CONTINUED.
 *
 * Currently only trivial policies are implemented.
 *
 */

#include "../reiser4.h"

/** Never store file's tail as direct item */
static int never_tail( const struct inode *inode UNUSED_ARG /* inode to
							     * operate on */, 
		       loff_t size UNUSED_ARG /* new object size */ )
{
	return 0;
}

/** Always store file's tail as direct item */
static int always_tail( const struct inode *inode UNUSED_ARG /* inode to
							      * operate on */,
			loff_t size UNUSED_ARG /* new object size */ )
{
	return 1;
}


/**
 * tail plugins
 */
reiser4_plugin tail_plugins[ LAST_TAIL_ID ] = {
	[ NEVER_TAIL_ID ] = {
		.h = {
			.type_id = REISER4_TAIL_PLUGIN_TYPE,
			.id      = NEVER_TAIL_ID,
			.pops    = NULL,
			.label   = "never",
			.desc    = "Never store file's tail as direct item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.tail = {
				.have_tail   = never_tail
			}
		}
	},
	[ ALWAYS_TAIL_ID ] = {
		.h = {
			.type_id = REISER4_TAIL_PLUGIN_TYPE,
			.id      = ALWAYS_TAIL_ID,
			.pops    = NULL,
			.label   = "always",
			.desc    = "Always store file's tail as direct item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.tail = {
				.have_tail   = always_tail
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
