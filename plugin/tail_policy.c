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
#include "../inode.h"
#include "object.h"
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

reiser4_block_nr never_tail_estimate ( const struct inode *inode, loff_t size,
	int is_fake) 
{
	/* Estimating the number of blocks for extents. Here is handled the both
	 * cases: request for allocating fake allocated extents and real allocated 
	 * ones */
	
	assert("umka-1245", inode != NULL);
	if (is_fake) {
	    reiser4_block_nr amount;
	    
	    /* In the case of fake allocated extent (truncate does) we are counting
	     * only overhead for one balancing */
	    estimate_internal_amount(1, tree_by_inode(inode)->height, &amount);

	    return amount;
	} else {
	    /* Here we are counting the number of blocks needed for creating of the
	     * real allocated extent(s) */
	    return ((size + (current_blocksize - 1)) / current_blocksize);
	}
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

reiser4_block_nr always_tail_estimate ( const struct inode *inode, loff_t size,
	int is_fake) 
{
	__u32 max_item_size;
	
	assert("umka-1244", inode != NULL);

        max_item_size = tree_by_inode(inode)->nplug->max_item_size();
	
	/* Write 4000 bytes: 2000 into one block and 2000 into the neighbour - 
	 * 2 blocks are ditry */
        return (size / max_item_size) + 2;
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

reiser4_block_nr test_tail_estimate ( const struct inode *inode, loff_t size,
	int is_fake) 
{
	assert("umka-1243", inode != NULL);

	return test_tail(inode, size) ? always_tail_estimate(inode, size, is_fake) : 
		never_tail_estimate(inode, size, is_fake);
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
			   .have_tail = never_tail,
			   .estimate = never_tail_estimate}
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
			    .have_tail = always_tail,
			    .estimate = always_tail_estimate}
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
			  .have_tail = test_tail,
			  .estimate = test_tail_estimate}
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
