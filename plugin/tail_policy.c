/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
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

#include "../tree.h"
#include "plugin.h"
#include "plugin_header.h"

#include <linux/pagemap.h>
#include <linux/fs.h>		/* For struct inode */

/** Never store file's tail as direct item */
/* Audited by: green(2002.06.12) */
static int
never_tail(const struct inode *inode UNUSED_ARG	/* inode to
						 * operate on */ ,
	   loff_t size UNUSED_ARG /* new object size */ )
{
	return 0;
}

/** Always store file's tail as direct item */
/* Audited by: green(2002.06.12) */
static int
always_tail(const struct inode *inode UNUSED_ARG	/* inode to
							 * operate on */ ,
	    loff_t size UNUSED_ARG /* new object size */ )
{
	return 1;
}

/** store tails only Always store file's tail as direct item */
/* Audited by: green(2002.06.12) */
/* AUDIT: above comment is incorerct and unclean. Also PAGE_SIZE is not correct
   thing to test against. It should become fs_blocksize instead */
static int
test_tail(const struct inode *inode UNUSED_ARG	/* inode to operate
						 * on */ ,
	  loff_t size /* new object size */ )
{
	if (size > PAGE_CACHE_SIZE * 4)
		return 0;
	return 1;
}

/**
 * tail plugins
 */
tail_plugin tail_plugins[LAST_TAIL_ID] = {
	[NEVER_TAIL_ID] = {
			   .h = {
				 .type_id = REISER4_TAIL_PLUGIN_TYPE,
				 .id = NEVER_TAIL_ID,
				 .pops = NULL,
				 .label = "never",
				 .desc = "Never store file's tail",
				 .linkage = TS_LIST_LINK_ZERO}
			   ,
			   .have_tail = never_tail}
	,
	[ALWAYS_TAIL_ID] = {
			    .h = {
				  .type_id = REISER4_TAIL_PLUGIN_TYPE,
				  .id = ALWAYS_TAIL_ID,
				  .pops = NULL,
				  .label = "always",
				  .desc = "Always store file's tail",
				  .linkage = TS_LIST_LINK_ZERO}
			    ,
			    .have_tail = always_tail}
	,
	[TEST_TAIL_ID] = {
			  .h = {
				.type_id = REISER4_TAIL_PLUGIN_TYPE,
				.id = TEST_TAIL_ID,
				.pops = NULL,
				.label = "test",
				.desc =
				"store files shorter than 2 blocks in tail items",
				.linkage = TS_LIST_LINK_ZERO}
			  ,
			  .have_tail = test_tail}
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
