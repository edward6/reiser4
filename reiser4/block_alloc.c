/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"


int reiser4_free_block (block_nr block UNUSED_ARG)
{
  	return 0;
}


int allocate_new_blocks (block_nr * hint UNUSED_ARG,
			 block_nr * count UNUSED_ARG)
{
	static block_nr block = 10000;
	*hint = block ++;
	*count = 1;
	return 0;
}


#if YOU_CAN_COMPILE_PSEUDO_CODE

/* for every node in the slum @sl */
int allocate_blocks_in_slum (reiser4_tree * tree UNUSED_ARG, slum * sl UNUSED_ARG)
{
	return 0;
}

/* we need to allocate node to the left of an extent before allocating the extent */

/* this code is simpler if we order nodes with level in tree being
most determinant of order and then within a level ordering from left
to right rather than putting each parent just before its children in
order.  Putting each parent just before its children will work better
with current generic readahead code though.  How does current code
order internal nodes relative to leaf nodes?  */

allocate_blocks_in_slum(struct key slum_key)
{
  slum = lock_slum(slum_key);
  current_node = left_node(slum);
  node_count = count_nodes(slum);
  reallocatable = make_list_blocknrs_in_slum(zn, actual_slum_size);
  blocknrs = allocate_block_numbers(node_count, reallocatable, left_neighbor(left_node));


/* this code doesn't handle having to dirty a parent (and thus add it
   to the slum) after changing an allocation.  That needs to be
   fixed.  */
  do 
    {
      if(parent_is_unallocated_extent(current_node))
	{
	  required_multiple_extents = allocate_extent_and_slum_so_far(non_extent, parent(current_node), blocknrs);
	  /*  */
	  non_extent = 0;
	  if (required_multiple_extents)
	    new_node_count = count_nodes(slum);
	  if (new_node_count != node_count)
	    {
	      node_count = new_node_count;
	      blocknrs = allocate_block_numbers(node_count);
	    }
	}
      else
	push(current_node, non_extent);
      get_right_neighbor(current_node);
    } while (slum not finished);
  allocate_extent_and_slum_so_far(non_extent, NULL_EXTENT);
}

/* take a look at struct znode  */

assign_blocknr(struct znode * target_zn, blocknr_t * cur_reallocatable, blocknr_t * reallocatable, int actual_slum_size, blocknr_t * search_start)

{
				/* find most optimal block which is either used or reallocatable */
  target_zn->true_blocknr = reiserfs_new_blocknr(search_start, blocknr_t * cur_reallocatable);
				/* if we must preserve this block
                                   because it is a new version of a
                                   node that has been written to disk
                                   in the past and it may contain
                                   contents that should only be
                                   changed transactionally. */
  if (preserve(target_zn->state))
    assign_wandered_blocknr(target_zn);
}

#endif

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
