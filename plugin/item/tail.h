/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __REISER4_TAIL_H__ )
#define __REISER4_TAIL_H__

typedef struct {
	int not_used;
} tail_coord_extension_t;


/* plugin->u.item.b.* */
reiser4_key *max_key_inside_tail(const coord_t *, reiser4_key *);
int can_contain_key_tail(const coord_t * coord, const reiser4_key * key, const reiser4_item_data *);
int mergeable_tail(const coord_t * p1, const coord_t * p2);
pos_in_item_t nr_units_tail(const coord_t *);
lookup_result lookup_tail(const reiser4_key *, lookup_bias, coord_t *);
int paste_tail(coord_t *, reiser4_item_data *, carry_plugin_info *);
int can_shift_tail(unsigned free_space, coord_t * source,
		   znode * target, shift_direction, unsigned *size, unsigned want);
void copy_units_tail(coord_t * target, coord_t * source,
		     unsigned from, unsigned count, shift_direction, unsigned free_space);
int cut_units_tail(coord_t * item, unsigned *from,
		   unsigned *to,
		   const reiser4_key * from_key, const reiser4_key * to_key, reiser4_key * smallest_removed, void *);
reiser4_key *unit_key_tail(const coord_t * coord, reiser4_key * key);

/* plugin->u.item.s.* */
int write_tail(struct inode *, flow_t *, hint_t *, int grabbed, write_mode_t);
int read_tail(struct file *, flow_t *, uf_coord_t *);
reiser4_key *append_key_tail(const coord_t *, reiser4_key *);
void init_coord_extension_tail(uf_coord_t *, loff_t offset);

#if REISER4_DEBUG
int key_in_item_tail(const uf_coord_t *, const reiser4_key *);
#endif

/* __REISER4_TAIL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
