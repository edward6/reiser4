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


