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
never_tail(const struct inode *inode UNUSED_ARG	/* inode to
						 * operate on */ ,
	   loff_t size UNUSED_ARG /* new object size */ )
{
	return 0;
}

static reiser4_block_nr never_tail_estimate ( const struct inode *inode, loff_t size,
	int is_hole) 
{
	/* Estimating the number of blocks for extents. Here is handled the both
	   cases: request for allocating fake allocated extents and real allocated 
	   ones */
	
	assert("umka-1245", inode != NULL);
	if (is_hole) {
	    reiser4_block_nr amount;
	    
	    /* In the case of unallocated extent (truncate does) we are counting 
	       the overhead for one balancing, stat data update and three blocks
	       may become dirty in the worse case on the twig level */
	    estimate_internal_amount(1, tree_by_inode(inode)->height, &amount);
	    return inode_file_plugin(inode)->estimate.update(inode) + amount + 3;
	} else {
	    /* Here we are counting the number of blocks needed for creating of the
	       allocated extent(s). The digit 3 is the number of dirty nodes on 
	       the twing level. */
	    return ((size + (current_blocksize - 1)) >> 
		    reiser4_get_current_sb()->s_blocksize_bits) +
		    inode_file_plugin(inode)->estimate.update(inode) + 3;
	}
}

/* Always store file's tail as direct item */
/* Audited by: green(2002.06.12) */
static int
always_tail(const struct inode *inode UNUSED_ARG	/* inode to
							 * operate on */ ,
	    loff_t size UNUSED_ARG /* new object size */ )
{
	return 1;
}

static reiser4_block_nr always_tail_estimate ( const struct inode *inode, loff_t size,
	int is_hole) 
{
	reiser4_block_nr amount;
	__u32 max_item_size, block_nr;
	
	assert("umka-1244", inode != NULL);

        max_item_size = tree_by_inode(inode)->nplug->max_item_size();

	/* Here 2 is the worse node packing factor */
	max_item_size >>= 1;
	
	block_nr = div64_32(size + (max_item_size - 1), max_item_size, NULL);
	
	/** write_flow writes in small pieces and every write starts it own balancing. 
	 *  Early flush may clean dirty nodes and they can become dirty again during
	 *  futher writes. */
	estimate_internal_amount(1, tree_by_inode(inode)->height, &amount);

	return block_nr + (block_nr * amount) + 
		inode_file_plugin(inode)->estimate.update(inode) + 1;
}

/* This function makes test if we should store file denoted @inode as tails only or
   as extents only. */
static int
test_tail(const struct inode *inode UNUSED_ARG	/* inode to operate
						 * on */ ,
	  loff_t size /* new object size */ )
{
	assert("umka-1253", inode != NULL);
	
	if (size > inode->i_sb->s_blocksize * 4)
		return 0;
	
	return 1;
}

static reiser4_block_nr test_tail_estimate ( const struct inode *inode, loff_t size,
	int is_hole) 
{
	assert("umka-1243", inode != NULL);

	return test_tail(inode, size) ? always_tail_estimate(inode, size, is_hole) : 
		never_tail_estimate(inode, size, is_hole);
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
				.desc = "store files shorter than 2 blocks in tail items",
				.linkage = TS_LIST_LINK_ZERO}
			  ,
			  .have_tail = test_tail,
			  .estimate = test_tail_estimate}
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
