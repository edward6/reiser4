current_leaf = find_leftmost_leaf_in_slum();

while (current_leaf)
{
  while (ancestors remain to be allocated by this flush)
    {
      ancestor_node = find its highest dirty parent not yet allocated by this flush();
      allocate_node(ancestor_node);
      release lock on node(ancestor_node);
    }
  current_parent = parent(current_leaf);
   do
     {
       squeeze_leaf_left(current_leaf);
       if (current_leaf ceases to exist)
	 continue;
       if (parent(current_leaf) != current_parent)
	 squeeze_parent_pointer_left(current_leaf);
       if (parent(current_leaf) != current_parent)
	 break;
       allocate_node(current_leaf);
       if (parent(current_leaf) != current_parent)
	 break;
     }
   while (current_leaf = get_right_neighbor_in_slum(current_leaf))
}

