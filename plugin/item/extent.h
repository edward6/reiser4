/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/* on disk extent */
typedef struct {
	d64 start;
	d64 width;
} reiser4_extent;


/* macros to set/get fields of on-disk extent */
#define extent_get_start(ext) d64tocpu(&((ext)->start))
#define extent_get_width(ext) d64tocpu(&((ext)->width))

#define extent_set_start(ext,a) cputod64 (a, &(ext)->start)
#define extent_set_width(ext,a) cputod64 (a, &(ext)->width)


#define extent_item(coord) ((reiser4_extent *)item_body_by_coord (coord))
#define extent_by_coord(coord) (extent_item (coord) + (coord)->unit_pos)
#define width_by_coord(coord) extent_width (extent_by_coord(coord))


/*
 * plugin->u.item.b.*
 */
reiser4_key * extent_max_key_inside (const tree_coord *, reiser4_key *);
int extent_mergeable (const tree_coord * p1, const tree_coord * p2);
unsigned extent_nr_units (const tree_coord *);
lookup_result extent_lookup (const reiser4_key *, lookup_bias,
			    tree_coord *);
int extent_paste (tree_coord *, reiser4_item_data *,
		  carry_level *);
int extent_can_shift (unsigned free_space, tree_coord * source,
		      znode * target, shift_direction,
		      unsigned * size, unsigned want);
void extent_copy_units (tree_coord * target, tree_coord * source,
			unsigned from, unsigned count, shift_direction
			where_is_free_space, unsigned free_space);
int extent_kill_item_hook (const tree_coord *, unsigned from, unsigned count);
int extent_create_hook (const tree_coord * coord, void * arg);
int extent_cut_units (tree_coord *, unsigned from, unsigned count,
		      shift_direction, const reiser4_key * from_key,
		      const reiser4_key * to_key,
		      reiser4_key * smallest_removed);
int extent_kill_units (tree_coord *, unsigned from, unsigned count,
		       shift_direction, const reiser4_key * from_key,
		       const reiser4_key * to_key,
		      reiser4_key * smallest_removed);
reiser4_key * extent_unit_key (const tree_coord * coord, reiser4_key * key);
int extent_item_data_by_flow (const tree_coord *, const flow *,
			      reiser4_item_data *);
void extent_print (const char *, tree_coord *);

/*
 * plugin->u.item.s.file.*
 */
int extent_write (struct inode *, tree_coord *, reiser4_lock_handle *, flow *);
int extent_read (struct inode *, tree_coord *, reiser4_lock_handle *, flow *);
int extent_readpage (void * arg, struct page * page);



/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
