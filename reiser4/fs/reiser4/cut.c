/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

#include "defines.h"


/* cut data between @from and @to of @from->node and call do_make corresponding
   changes in the tree. @from->node may become empty. If so - pointer to it
   will be removed. Neighboring nodes are not changed. Smallest removed key is
   stored in @smallest_removed */
int cut_node (tween_coord * from, tween_coord * to,
	      reiser4_key * smallest_removed)
{
	int result;
	tween_coord first, /* coord of leftmost unit of @from->node which will
			      be removed */
		last /* coord of rightmost unit of @from->node ... */;

	carry_pool  pool;
	carry_level lowest_level;
	carry_op   *op;


	reiser4_init_carry_pool( &pool );
	reiser4_init_carry_level( &lowest_level, &pool );

	/* set @first and @last to first and last units which are to be
	   removed (getting rid of betweenness) */
	first = *from;
	last = *to;
	set_to_right (&first);
	set_to_left (&last);

	/* make sure that @from and @to are set to existing units in the
	   node */
	assert ("vs-161", coord_of_unit (&first));
	assert ("vs-162", coord_of_unit (&last));
	
	/* store smallest removed key */
	unit_key_by_coord (&first, smallest_removed);

	op = reiser4_post_carry( &lowest_level, cop_cut, first.node, 0 );
	if( IS_ERR( op ) || ( op == NULL ) )
		return op ? PTR_ERR (op) : -EIO;

	op->u.cut.left = &first;
	op->u.cut.right = &laft;
	result = tree_carry( &lowest_level );
	reiser4_done_carry_pool( &pool );

	return result;
}


/* there is a fundamental problem with optimizing deletes: VFS does it
   one file at a time.  Another problem is that if an item can be
   anything, then deleting items must be done one at a time.  It just
   seems clean to writes this to specify a from and a to key, and cut
   everything between them though.  */

/* use this function with care if deleting more than what is part of a single file. */
/* do not use this when cutting a single item, it is suboptimal for that */

/* You are encouraged to write plugin specific versions of this.  It
   cannot be optimal for all plugins because it works item at a time,
   and some plugins could sometimes work node at a time. Regular files
   however are not optimizable to work node at a time because of
   extents needing to free the blocks they point to.  

   Optimizations compared to v3 code:

   It does not balance (that task is left to memory pressure code).

   Nodes are deleted only if empty.

   Uses extents.

   Performs read-ahead of formatted nodes whose contents are part of
   the deletion.
*/

int cut_tree (reiser4_key * from_key, reiser4_key * to_key)
{
	tween_coord intranode_to, intranode_from;
	reiser4_key smallest_removed;
	int result;


	reiser4_init_point (&intranode_to);

	
#if WE_HAVE_READAHEAD
	request_read_ahead_key_range(from, to, LIMIT_READ_AHEAD_BY_CACHE_SIZE_ONLY);
	/* locking? */
	spans_node = key_range_spans_node(from, to);
#endif /* WE_HAVE_READAHEAD */

	do {
		/* look for @to_key in the tree or use @to_coord if it is set
		   properly */
		result = point_by_hint_and_key (to_key,
						&intranode_to /* was set as hint in previous loop iteration (if there was one) */
						);
		if (result != pbk_point_found && result != pbk_point_notfound)
			/* -EIO, or something like that */
			break;

		/* lookup for @from_key in current node */
		intranode_from.node = intranode_to.node;
		result = node_plugin_by_node (intranode_to.node)->lookup (intranode_to.node,
									  from_key, find_exact,
									  &intranode_from);
		
		if (result != pbk_point_found && result != pbk_point_notfound)
			/* -EIO, or something like that */
			break;

		/* cut data from one node */
		result = cut_node (&intranode_from, 
				   &intranode_to, /* is used as an input and
						     an output, with output
						     being a hint used by next
						     loop iteration */
				   &smallest_removed);
		if (result)
			break;

	} while (keycmp (&smallest_removed, from_key) == greater_than);

	release_point (&intranode_to);
	return result;
}





#if 0
/* ??? shouldn't this start from bigger key? Then
   reiser4_ordinary_file_truncate will be able to use it. Because if cut_tree
   will not complete due to power failure or other error file will be left in
   consistent state only if deleting from larger to smaller */
cut_error cut_tree_original(key * from, key * to)
{
	request_read_ahead_key_range(from, to, LIMIT_READ_AHEAD_BY_CACHE_SIZE_ONLY);
	/* locking? */
	spans_node = key_range_spans_node(from, to);
  
	/* partial cut first item if cut
	   starts from the middle of it */
	if(key_cmp(from, key_of_item(from_item_header) == less_than )) {
		handler(from_item_header)->cut(from_item_header, from, to, node);
		from_coord = next_item(from_item_coord); /* need to check error conditions here.... */
	}


	while (from < to)
		{
			from_item_coord = item_coord_by_key(from);

			/* need to check error conditions here in case there is no
			   next item.... */
			next_item = next_item(from_item_coord); 

			/* partial item cuts: invokes item
			   handler directly, by contrast whole
			   item cuts invoke node handlers
			   (which call whole item delete
			   handlers) */
			if (key_by_item_coord (next_item) > to)
				{
					handler(from_item_header)->cut(from_item_header, from, to, node);
					if(num_items(from_item_coord.node) == 0)
						delete_node(from_item_coord.node);
					break;
				}
			/* whole item deletes */
			/* this indirectly calls the item handler delete method */
			node->delete_item(from_item_coord);
			/* if node is empty, delete it */
			if (num_items(from_item_coord.node) == 0)
				delete_node(from_item_coord.node);
		}
}
#endif

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
