/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * plugin->u.item.common.*
 */

#if !defined( __REISER4_TAIL_H__ )
#define __REISER4_TAIL_H__

#include "../../forward.h"
#include "../../seal.h"
#include <linux/fs.h> /* for struct inode */

reiser4_key * tail_max_key_inside  (const coord_t *, reiser4_key *);
int           tail_can_contain_key (const coord_t *coord,
				    const reiser4_key *key,
				    const reiser4_item_data *);
int           tail_mergeable       (const coord_t * p1,
				    const coord_t * p2);
unsigned      tail_nr_units        (const coord_t *);
lookup_result tail_lookup          (const reiser4_key *, lookup_bias,
				    coord_t *);
int           tail_paste           (coord_t *, reiser4_item_data *,
				    carry_plugin_info *);
int           tail_can_shift       (unsigned free_space, coord_t * source,
				    znode * target, shift_direction,
				    unsigned * size, unsigned want);
void          tail_copy_units      (coord_t * target, coord_t * source,
				    unsigned from, unsigned count,
				    shift_direction, unsigned free_space);
int           tail_cut_units       (coord_t * item, unsigned *from,
				    unsigned *to,
				    const reiser4_key * from_key,
				    const reiser4_key * to_key,
				    reiser4_key * smallest_removed);
reiser4_key * tail_unit_key        (const coord_t * coord,
				    reiser4_key * key);
reiser4_key * tail_max_key         (const coord_t * coord,
				    reiser4_key * key);
int           tail_key_in_item     (coord_t * coord, const reiser4_key * key);

/*
 * plugin->u.item.s.*
 */
int tail_write (struct inode *, struct sealed_coord *, flow_t *);
int tail_read  (struct inode *, struct sealed_coord *, flow_t *);

/* __REISER4_TAIL_H__ */
#endif
