/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "dformat.h"
#include "tree.h"
#include "carry.h"

/* this returns how many nodes might get dirty and added nodes if @children nodes are dirtied

   Amount of internals which will get dirty or get allocated we estimate as 5% of the childs + 1 balancing. 1 balancing
   is 2 neighbours, 2 new blocks and the current block on the leaf level, 2 neighbour nodes + the current (or 1
   neighbour and 1 new and the current) on twig level, 2 neighbour nodes on upper levels and 1 for a new root. So 5 for
   leaf level, 3 for twig level, 2 on upper + 1 for root.

   Do not calculate the current node of the lowest level here - this is overhead only.

   children is almost always 1 here. Exception is flow insertion
*/
static reiser4_block_nr
max_balance_overhead(reiser4_block_nr childen, tree_level tree_height)
{
	reiser4_block_nr ten_percent;
	
	ten_percent = ((103 * childen) >> 10);
	
	/* If we have too many balancings at the time, tree height can raise on more
	   then 1. Assume that if tree_height is 5, it can raise on 1 only. */
	return ((tree_height < 5 ? 5 : tree_height) * 2 + (4 + ten_percent));
}

/* this returns maximal possible number of nodes which can be modified plus number of new nodes which can be required to
   perform insertion of one item into the tree */
/* it is only called when tree height changes, or gets initialized */
reiser4_block_nr
calc_estimate_one_insert(tree_level height)
{
	return 1 + max_balance_overhead(1, height);
}

reiser4_block_nr
estimate_internal_amount(reiser4_block_nr children, tree_level tree_height)
{
	return max_balance_overhead(children, tree_height);
}

inline reiser4_block_nr
estimate_one_insert_item(reiser4_tree *tree)
{
	return tree->estimate_one_insert;
}

/* this returns maximal possible number of nodes which can be modified plus number of new nodes which can be required to
   perform insertion of one unit into an item in the tree */
inline reiser4_block_nr
estimate_one_insert_into_item(reiser4_tree *tree)
{
	/* estimate insert into item just like item insertion */
	return tree->estimate_one_insert;
}

inline reiser4_block_nr
estimate_one_item_removal(reiser4_tree *tree)
{
	/* on item removal reiser4 does not try to pack nodes more complact, so, only one node may be dirtied on leaf
	   level */
	return tree->estimate_one_insert;
}

/* on leaf level insert_flow may add CARRY_FLOW_NEW_NODES_LIMIT new nodes and dirty 3 existing nodes (insert point and
   both its neighbors). Max_balance_overhead should estimate number of blocks which may change/get added on internal
   levels */
reiser4_block_nr
estimate_insert_flow(tree_level height)
{
	return 3 + CARRY_FLOW_NEW_NODES_LIMIT + max_balance_overhead(3 + CARRY_FLOW_NEW_NODES_LIMIT, height);
}

#if YOU_CAN_COMPILE_PSEUDO_CODE

/* maximal cost in leaf nodes of deleting an item (left and right are wandered, current disappears but not immediately)*/
#define ESTIMATE_ITEM_DELETE 2

/* maximal cost in leaf nodes of inserting an item (left, right, new, and current are wandered) */
#define ESTIMATE_ITEM_INSERT 4

/* maximal cost in leaf nodes of updating an item (current is wandered)*/
#define ESTIMATE_ITEM_UPDATE 1

estimate_rename()
{

/* we ignore internal nodes because we have some percent of the device
   space in reserve, and no set of changes to internal nodes can
   exceed that reserve and leave us with internal nodes whose children
   can fit onto this disk drive because we know what worst case fan
   out is. */

/* if we ever get a rename that does more than insert one item and
   delete one item and update a parent directory stat data, we'll need
   to recode this. */
	return ESTIMATE_ITEM_DELETE + ESTIMATE_ITEM_INSERT + ESTIMATE_ITEM_UPDATE;

}

#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
