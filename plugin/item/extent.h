/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* on disk extent */
typedef struct {
	reiser4_dblock_nr start;
	reiser4_dblock_nr width;
} reiser4_extent;


/* macros to set/get fields of on-disk extent */
static inline reiser4_block_nr extent_get_start(const reiser4_extent *ext)
{
	return dblock_to_cpu (& ext->start);
}

static inline reiser4_block_nr extent_get_width(const reiser4_extent *ext)
{
	return dblock_to_cpu (& ext->width);
}

static inline void extent_set_start(reiser4_extent *ext, reiser4_block_nr start)
{
	cassert (sizeof (ext->start) == 8);
	cpu_to_dblock (start, & ext->start);
}

static inline void extent_set_width(reiser4_extent *ext, reiser4_block_nr width)
{
	cassert (sizeof (ext->width) == 8);
	cpu_to_dblock (width, & ext->width);
}

#define extent_item(coord) ((reiser4_extent *)item_body_by_coord (coord))
#define extent_by_coord(coord) (extent_item (coord) + (coord)->unit_pos)
#define width_by_coord(coord) extent_get_width (extent_by_coord(coord))

/* FIXME: hmm */
/*#define extent_item_is_dirty(item) 1*/


/*
 * plugin->item.common.*
 */
reiser4_key * extent_max_key_inside    (const tree_coord *, reiser4_key *);
int           extent_can_contain_key   ( const tree_coord *coord,
					 const reiser4_key *key,
					 const reiser4_item_data * );
int           extent_mergeable         (const tree_coord * p1,
					const tree_coord * p2);
unsigned      extent_nr_units          (const tree_coord *);
lookup_result extent_lookup            (const reiser4_key *, lookup_bias,
					tree_coord *);
int           extent_init              (tree_coord *, reiser4_item_data *);
int           extent_paste             (tree_coord *, reiser4_item_data *,
					carry_level *);
int           extent_can_shift         (unsigned free_space,
					tree_coord * source,
					znode * target, shift_direction,
					unsigned * size, unsigned want);
void          extent_copy_units        (tree_coord * target,
					tree_coord * source,
					unsigned from, unsigned count,
					shift_direction where_is_free_space,
					unsigned free_space);
int           extent_kill_item_hook    (const tree_coord *, unsigned from,
					unsigned count, void *kill_params);
int           extent_create_hook       (const tree_coord * coord, void * arg);
int           extent_cut_units         (tree_coord *, unsigned * from,
					unsigned * to,
					const reiser4_key * from_key,
					const reiser4_key * to_key,
					reiser4_key * smallest_removed);
int           extent_kill_units        (tree_coord *, unsigned *from,
					unsigned *to,
					const reiser4_key * from_key,
					const reiser4_key * to_key,
					reiser4_key * smallest_removed);
reiser4_key * extent_unit_key          (const tree_coord * coord,
					reiser4_key * key);
int           extent_item_data_by_flow (const tree_coord *, const flow_t *,
					reiser4_item_data *);
void          extent_print             (const char *, tree_coord *);
int           extent_utmost_child      (const tree_coord *coord, sideof side,
					jnode **child );
int           extent_utmost_child_dirty ( const tree_coord  *coord,
					  sideof side, int *is_dirty );
int           extent_utmost_child_real_block ( const tree_coord  *coord,
					       sideof side,
					       reiser4_block_nr  *block );
reiser4_key * extent_max_key            (const tree_coord * coord, 
					 reiser4_key * key);

/*
 * plugin->u.item.s.file.*
 */
int extent_write    (struct inode *, tree_coord *, lock_handle *,
		     flow_t *, struct page *);
int extent_read     (struct inode *, tree_coord *, lock_handle *,
		     flow_t *);
int extent_readpage (void * arg, struct page * page);

/* these are used in flush.c
 * FIXME-VS: should they be somewhere in item_plugin? */
int allocate_extent_item_in_place (tree_coord * item, reiser4_blocknr_hint * preceder);
int allocate_and_copy_extent (znode * left, tree_coord * right,
			      reiser4_blocknr_hint * preceder,
			      reiser4_key * stop_key);

int   extent_is_allocated (tree_coord *item); /* True if this extent is allocated (i.e., not a hole, not unallocated). */
__u64 extent_unit_index   (tree_coord *item); /* Block offset of this unit. */
__u64 extent_unit_width   (tree_coord *item); /* Number of blocks in this unit. */
int   extent_get_inode    (tree_coord *item, struct inode **inode); /* Get the inode: you must iput() it. */

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
