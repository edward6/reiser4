/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* on disk extent */
typedef struct {
	reiser4_dblock_nr start;
	reiser4_dblock_nr width;
} reiser4_extent;

/*
 * this structure is used to pass both coord and lock handle from extent_read
 * down to extent_readpage via read_cache_page which can deliver to filler only
 * one parameter specified by its caller
 */
struct readpage_arg {
	coord_t * coord;
	lock_handle * lh;
};


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
reiser4_key * extent_max_key_inside    (const coord_t *, reiser4_key *);
int           extent_can_contain_key   ( const coord_t *coord,
					 const reiser4_key *key,
					 const reiser4_item_data * );
int           extent_mergeable         (const coord_t * p1,
					const coord_t * p2);
unsigned      extent_nr_units          (const coord_t *);
lookup_result extent_lookup            (const reiser4_key *, lookup_bias,
					coord_t *);
int           extent_init              (coord_t *, reiser4_item_data *);
int           extent_paste             (coord_t *, reiser4_item_data *,
					carry_plugin_info *);
int           extent_can_shift         (unsigned free_space,
					coord_t * source,
					znode * target, shift_direction,
					unsigned * size, unsigned want);
void          extent_copy_units        (coord_t * target,
					coord_t * source,
					unsigned from, unsigned count,
					shift_direction where_is_free_space,
					unsigned free_space);
int           extent_kill_item_hook    (const coord_t *, unsigned from,
					unsigned count, void *kill_params);
int           extent_create_hook       (const coord_t * coord, void * arg);
int           extent_cut_units         (coord_t *, unsigned * from,
					unsigned * to,
					const reiser4_key * from_key,
					const reiser4_key * to_key,
					reiser4_key * smallest_removed);
int           extent_kill_units        (coord_t *, unsigned *from,
					unsigned *to,
					const reiser4_key * from_key,
					const reiser4_key * to_key,
					reiser4_key * smallest_removed);
reiser4_key * extent_unit_key          (const coord_t * coord,
					reiser4_key * key);
int           extent_item_data_by_flow (const coord_t *, const flow_t *,
					reiser4_item_data *);
void          extent_print             (const char *, coord_t *);
int           extent_utmost_child      (const coord_t *coord, sideof side,
					jnode **child );
int           extent_utmost_child_dirty ( const coord_t  *coord,
					  sideof side, int *is_dirty );
int           extent_utmost_child_real_block ( const coord_t  *coord,
					       sideof side,
					       reiser4_block_nr  *block );
reiser4_key * extent_max_key            (const coord_t * coord, 
					 reiser4_key * key);
int           extent_key_in_item        ( coord_t *coord,
					  const reiser4_key *key );
int           extent_key_in_unit        ( const coord_t *coord,
					  const reiser4_key *key );

/*
 * plugin->u.item.s.file.*
 */
int extent_write    (struct inode *, coord_t *, lock_handle *,
		     flow_t *, struct page *);
int extent_read     (struct inode *, coord_t *, lock_handle *,
		     flow_t *);
int extent_readpage (void *, struct page * page);
int extent_page_cache_readahead (struct file * file, coord_t * coord,
				 lock_handle * lh UNUSED_ARG,
				 unsigned long start_page,
				 unsigned long intrafile_readahead_amount);

/* these are used in flush.c
 * FIXME-VS: should they be somewhere in item_plugin? */
int allocate_extent_item_in_place (coord_t * item, flush_position *pos);
int allocate_and_copy_extent (znode * left, coord_t * right,
			      flush_position *pos,
			      reiser4_key * stop_key);

int   extent_is_unallocated (const coord_t *item); /* True if this extent is unallocated (i.e., not a hole, not allocated). */
__u64 extent_unit_index   (const coord_t *item); /* Block offset of this unit. */
__u64 extent_unit_width   (const coord_t *item); /* Number of blocks in this unit. */
reiser4_block_nr extent_unit_start (const coord_t *item); /* Starting block location of this unit. */
void  extent_get_inode    (const coord_t *item, struct inode **inode); /* Get the inode: you must iput() it. */

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
