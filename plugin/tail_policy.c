/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Tail policy plugins */

/* Tail policy is used by object plugin (of regular file) to convert file
   between two representations. TO BE CONTINUED.
  
   Currently only trivial policies are implemented.
  
*/

#include "../tree.h"
#include "../inode.h"
#include "../super.h"
#include "object.h"
#include "plugin.h"
#include "node/node.h"
#include "plugin_header.h"
#include "../lib.h"

#include <linux/pagemap.h>
#include <linux/fs.h>		/* For struct inode */

/* Never store file's tail as direct item */
/* Audited by: green(2002.06.12) */
static int
have_tail_never(const struct inode *inode UNUSED_ARG /* inode to operate on */ ,
		loff_t size UNUSED_ARG /* new object size */ )
{
	return 0;
}

/* Always store file's tail as direct item */
/* Audited by: green(2002.06.12) */
static int
have_tail_always(const struct inode *inode UNUSED_ARG	/* inode to operate on */ ,
		 loff_t size UNUSED_ARG /* new object size */ )
{
	return 1;
}

/* This function makes test if we should store file denoted @inode as tails only or
   as extents only. */
static int
have_tail_default(const struct inode *inode UNUSED_ARG	/* inode to operate on */ ,
		  loff_t size /* new object size */ )
{
	assert("umka-1253", inode != NULL);
	
	if (size > inode->i_sb->s_blocksize * 4)
		return 0;
	
	return 1;
}

/* tail plugins */
tail_plugin tail_plugins[LAST_TAIL_ID] = {
	[NEVER_TAIL_ID] = {
		.h = {
			.type_id = REISER4_TAIL_PLUGIN_TYPE,
			.id = NEVER_TAIL_ID,
			.pops = NULL,
			.label = "never",
			.desc = "Never store file's tail",
			.linkage = TS_LIST_LINK_ZERO
		},
		.have_tail = have_tail_never
	},
	[ALWAYS_TAIL_ID] = {
		.h = {
			.type_id = REISER4_TAIL_PLUGIN_TYPE,
			.id = ALWAYS_TAIL_ID,
			.pops = NULL,
			.label = "always",
			.desc = "Always store file's tail",
			.linkage = TS_LIST_LINK_ZERO
		},
		.have_tail = have_tail_always
	},
	[DEFAULT_TAIL_ID] = {
		.h = {
			.type_id = REISER4_TAIL_PLUGIN_TYPE,
			.id = DEFAULT_TAIL_ID,
			.pops = NULL,
			.label = "default tail policy plugin",
			.desc = "store files shorter than 4 blocks in tail items",
			.linkage = TS_LIST_LINK_ZERO
		},
		.have_tail = have_tail_default
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
