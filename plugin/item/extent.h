/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __REISER4_EXTENT_H__
#define __REISER4_EXTENT_H__

/* on disk extent */
typedef struct {
	reiser4_dblock_nr start;
	reiser4_dblock_nr width;
} reiser4_extent;

typedef struct extent_stat {
	int unallocated_units;
	int unallocated_blocks;
	int allocated_units;
	int allocated_blocks;
	int hole_units;
	int hole_blocks;
} extent_stat;

/* macros to set/get fields of on-disk extent */
static inline reiser4_block_nr
extent_get_start(const reiser4_extent * ext)
{
	return dblock_to_cpu(&ext->start);
}

static inline reiser4_block_nr
extent_get_width(const reiser4_extent * ext)
{
	return dblock_to_cpu(&ext->width);
}

extern __u64 reiser4_current_block_count(void);

static inline void
extent_set_start(reiser4_extent * ext, reiser4_block_nr start)
{
	cassert(sizeof (ext->start) == 8);
	assert("nikita-2510", ergo(start > 1, start < reiser4_current_block_count()));
	cpu_to_dblock(start, &ext->start);
}

static inline void
extent_set_width(reiser4_extent * ext, reiser4_block_nr width)
{
	cassert(sizeof (ext->width) == 8);
	cpu_to_dblock(width, &ext->width);
	assert("nikita-2511",
	       ergo(extent_get_start(ext) > 1,
		    extent_get_start(ext) + width <= reiser4_current_block_count()));
}

#define extent_item(coord) 					\
({								\
	assert("nikita-3143", item_is_extent(coord));		\
	((reiser4_extent *)item_body_by_coord (coord));		\
})

#define extent_by_coord(coord)					\
({								\
	assert("nikita-3144", item_is_extent(coord));		\
	(extent_item (coord) + (coord)->unit_pos);		\
})

#define width_by_coord(coord) 					\
({								\
	assert("nikita-3145", item_is_extent(coord));		\
	extent_get_width (extent_by_coord(coord));		\
})

/* plugin->u.item.b.* */
reiser4_key *max_key_inside_extent(const coord_t *, reiser4_key *, void *);
int can_contain_key_extent(const coord_t * coord, const reiser4_key * key, const reiser4_item_data *);
int mergeable_extent(const coord_t * p1, const coord_t * p2);
unsigned nr_units_extent(const coord_t *);
lookup_result lookup_extent(const reiser4_key *, lookup_bias, coord_t *);
void init_coord_extent(const reiser4_key *, coord_t *);
int init_extent(coord_t *, reiser4_item_data *);
int paste_extent(coord_t *, reiser4_item_data *, carry_plugin_info *);
int can_shift_extent(unsigned free_space,
		     coord_t * source, znode * target, shift_direction, unsigned *size, unsigned want);
void copy_units_extent(coord_t * target,
		       coord_t * source,
		       unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space);
int kill_hook_extent(const coord_t *, unsigned from, unsigned count, void *);
int create_hook_extent(const coord_t * coord, void *arg);
int cut_units_extent(coord_t *, unsigned *from,
		     unsigned *to,
		     const reiser4_key * from_key, const reiser4_key * to_key, reiser4_key * smallest_removed, void *);
int kill_units_extent(coord_t *, unsigned *from,
		      unsigned *to,
		      const reiser4_key * from_key, const reiser4_key * to_key, reiser4_key * smallest_removed, void *);
reiser4_key *unit_key_extent(const coord_t * coord, reiser4_key * key);
void print_extent(const char *, coord_t *);
int utmost_child_extent(const coord_t * coord, sideof side, jnode ** child);
int utmost_child_real_block_extent(const coord_t * coord, sideof side, reiser4_block_nr * block);
void item_stat_extent(const coord_t * coord, void *vp);
int check_extent(const coord_t * coord, const char **error);

/* plugin->u.item.s.file.* */
#include "../file/file.h"
int write_extent(struct inode *, coord_t *, lock_handle *, flow_t *, hint_t *, int grabbed);
int read_extent(struct file *, coord_t *, flow_t *);
int readpage_extent(void *, struct page *page);
void readpages_extent(coord_t *, struct address_space *, struct list_head *pages);
int writepage_extent(coord_t *, lock_handle *, struct page *);
reiser4_key *append_key_extent(const coord_t * coord, reiser4_key * key, void *);
int key_in_item_extent(coord_t * coord, const reiser4_key * key, void *);
int get_block_address_extent(const coord_t *, sector_t block, struct buffer_head *);

/* these are used in flush.c
   FIXME-VS: should they be somewhere in item_plugin? */
int allocate_extent_item_in_place(coord_t *, lock_handle *, flush_pos_t * pos);
int allocate_and_copy_extent(znode * left, coord_t * right, flush_pos_t * pos, reiser4_key * stop_key);

int extent_is_unallocated(const coord_t * item);	/* True if this extent is unallocated (i.e., not a hole, not allocated). */
__u64 extent_unit_index(const coord_t * item);	/* Block offset of this unit. */
__u64 extent_unit_width(const coord_t * item);	/* Number of blocks in this unit. */
reiser4_block_nr extent_unit_start(const coord_t * item);	/* Starting block location of this unit. */

/* __REISER4_EXTENT_H__ */
#endif
/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
