/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Tail policy plugins
 */

/* this is completely uncommented.  Nikita, I want you to systematically review all of the code you have written and comment it.  Then remove this comment. */

/* does this actually get used?  Why is it missing a tail for files larger than or equal to 12k policy */

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
			.type_id = REISER4_TAIL_PLUGIN_TYPE,
			.id      = NEVER_TAIL_ID,
			.pops    = NULL,
			.label   = "never",
			.desc    = "Never store file's tail as direct item",
			.linkage = TS_LIST_LINK_ZERO
		},
		.u = {
			.tail = {
				.tail   = never_tail,
				.notail = always_tail
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
				.tail   = always_tail,
				.notail = never_tail
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
