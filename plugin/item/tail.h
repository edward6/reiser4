/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * plugin->u.item.common.*
 */
reiser4_key * tail_max_key_inside  (const new_coord *, reiser4_key *);
int           tail_can_contain_key ( const new_coord *coord,
				      const reiser4_key *key,
				      const reiser4_item_data * );
int           tail_mergeable       (const new_coord * p1,
				    const new_coord * p2);
unsigned      tail_nr_units        (const new_coord *);
lookup_result tail_lookup          (const reiser4_key *, lookup_bias,
				    new_coord *);
int           tail_paste           (new_coord *, reiser4_item_data *,
				    carry_level *);
int           tail_can_shift       (unsigned free_space, new_coord * source,
				    znode * target, shift_direction,
				    unsigned * size, unsigned want);
void          tail_copy_units      (new_coord * target, new_coord * source,
				    unsigned from, unsigned count,
				    shift_direction, unsigned free_space);
int           tail_cut_units       (new_coord * item, unsigned *from,
				    unsigned *to,
				    const reiser4_key * from_key,
				    const reiser4_key * to_key,
				    reiser4_key * smallest_removed);
reiser4_key * tail_unit_key        (const new_coord * coord,
				    reiser4_key * key);

/*
 * plugin->u.item.s.*
 */
int tail_write (struct inode *, new_coord *, lock_handle *, flow_t *,
		struct page *);
int tail_read  (struct inode *, new_coord *, lock_handle *, flow_t *);

