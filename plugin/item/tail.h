/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * plugin->u.item.b.*
 */
reiser4_key * tail_max_key_inside (const tree_coord *, reiser4_key *);
int tail_mergeable (const tree_coord * p1, const tree_coord * p2);
unsigned tail_nr_units (const tree_coord *);
lookup_result tail_lookup (const reiser4_key *, lookup_bias,
			   tree_coord *);
int tail_paste (tree_coord *, reiser4_item_data *,
		carry_level *);
int tail_can_shift (unsigned free_space, tree_coord * source,
		    znode * target, shift_direction,
		    unsigned * size, unsigned want);
void tail_copy_units (tree_coord * target, tree_coord * source,
		      unsigned from, unsigned count,
		      shift_direction, unsigned free_space);
int tail_cut_units (tree_coord * item, unsigned *from, unsigned *to,
		    const reiser4_key * from_key,
		    const reiser4_key * to_key,
		    reiser4_key * smallest_removed);
reiser4_key * tail_unit_key (const tree_coord * coord, reiser4_key * key);

/*
 * plugin->u.item.s.file.*
 */
int tail_write (struct inode *, tree_coord *, reiser4_lock_handle *, flow *);
int tail_read (struct inode *, tree_coord *, reiser4_lock_handle *, flow *);
