current_leaf = find_leftmost_leaf_in_slum();
/* current_leaf is locked */
parent = parent(current_leaf);
/* parent is locked */

if (is_relocate_set(current_leaf))
{
  dirty(parent);
}

if (is_dirty(parent))
{
  squeeze_level_left(parent);
  /* this can create an enormous recursive chain that could overflow
   the kernel stack, hmmmm..... */
  flush_all_other_child_slums(parent, min_key(current_leaf));
}
else
{
  unlock(parent);
}
/* parent is unlocked by squeeze_level_left, and squeezing may have
   changed the parent of current_leaf */

parent = parent(current_leaf);
/* parent is locked */
if (leftmost_child(parent) == current_leaf)
     allocate(parent);

/* ok, now we are ready to proceed through slum on the leaf level */
next_leaf = get_right_neighbor_in_slum_level(current_leaf);
/* next_leaf is locked or null */

/* need to review locking in the below */
while(next_leaf)
{
  if (is_formatted(current_leaf) && is_formatted(next_leaf))
    {
      squeeze_left(current_leaf, next_leaf);
      if (is_empty(next_leaf)) 
	{
	  delete_node(next_leaf);
	  next_leaf = get_right_neighbor_in_slum_level(current_leaf);
	  check_and_handle_parent_change();
	  continue;
	}
    }
  if (is_unformatted(current_leaf))
    /* allocate or reallocate */
    allocate_extent_in_parent_if_needed(current_leaf);
  /* the above may change the parent */
  check_and_handle_parent_change();
  allocate(current_leaf);	  
  next_leaf = get_right_neighbor_in_slum_level(current_leaf);
  check_and_handle_parent_change();
  
  
}

/* this means squeeze it as well as allocate it */
handle_non_leaf_end_of_slum();

check_and_handle_parent_change()
{
if ( (new_parent = parent(current_leaf)) != parent)
  squeeze_left(parent, new_parent(parent));
 else
   return;

/* the line above can change who the parent is so retest... */
  if((new_parent = parent(current_leaf)) != parent)
    {
      parent = new_parent;
      if (leftmost_child(parent) != current_leaf)
	reiser_panic("reiser-2071: this needs recoding to handle this case");
      allocate_node(parent);
				/* allocating other ancestors left for josh */

      /* our new parent might not be well packed, and we want
	 it to be well packed even if our slum never reaches its edge
	 so we... */
      squeeze_left(parent, right_neighbor(parent));
    }
}


################################################################################


The Problem:

We need to know the relocate set in order to perform a left-to-right
parent-first allocate-and-squeeze traversal over a dirty sub-tree.  We
could make this decision during the allocate-and-squeeze pass, but in
that case we would discover a node is dirty when we have already
passed over its position in the parent-first ordering.  In otherwords,
we would discover this information too late to be useful.

The Solution:

Maintain an active count of dirty children for each node.  This allows
us to mark a node dirty whenever its dirty count becomes >= 2 because
at that point overwriting the parent reduces the total number of
blocks written to disk.  How much of a counter is needed?  In order to
keep track of additions and subtractions to this count, a counter at
the same size as our znode's c_count field is needed.  If this value
were only ever incremented, then we could use a single bit (0 = no
dirty children, 1 = single dirty child, otherwise mark dirty &
relocate).  But since a node may have dirty children added while
flushes are active (it can happen, right) this requires more than just
a bit.  I worry about the complexity of maintaining this dirty count,
but I fear that the parent-first allocation policy will not succeed
without knowing before-hand all the dirty nodes it must consider.

The Algorithm:

Given the above assumption, that nodes are marked dirty whenever they
should be relocated (i.e., that the flush algorithm does not make this
decision during as part of its passing over the tree).

Starting from some leaf node, find the maximal dirty ancestor (note:
no scan left involved).  From the maximal dirty ancestor, find the
leftmost in-memory descendant.  If the leftmost descendant is dirty,
consider its left neighbor.  If the neighbor is also dirty, repeat the
steps of this paragraph starting at that node.  If the neighbor is not
dirty, save the maximal dirty parent of the leftmost descendant.

Now we have found the maximal dirty ancestor from which to begin
allocating and squeezing.  From this point we will traverse all
descendants of the maximal dirty ancestor, in parent-first order,
allocating blocks and squeezing nodes in the Right Order.  I will
define this order tomorrow.
