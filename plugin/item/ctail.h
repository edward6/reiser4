/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* cryptcompress object item. See ctail.c for description. */

typedef struct ctail_item_format {
	d32 disk_cluster_size;
	d8 body[0];
} __attribute__((packed)) ctail_item_format;


/* plugin->item.b.* */
int ctail_mergeable(const coord_t * p1, const coord_t * p2);
unsigned ctail_nr_units(const coord_t * coord);
int ctail_estimate(const coord_t * coord, const reiser4_item_data * data);
void ctail_print(const char *prefix, coord_t * coord);
lookup_result ctail_lookup(const reiser4_key * key, lookup_bias bias, coord_t * coord);
int ctail_paste(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG);
int ctail_can_shift(unsigned free_space, coord_t * coord,
		  znode * target, shift_direction pend, unsigned *size, unsigned want);
void ctail_copy_units(coord_t * target, coord_t * source,
		    unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space);
int ctail_cut_units(coord_t * coord, unsigned *from, unsigned *to,
		  const reiser4_key * from_key, const reiser4_key * to_key, reiser4_key * smallest_removed, void *);
void ctail_print(const char *prefix, coord_t * coord);
int ctail_check(const coord_t * coord, const char **error);

/* plugin->u.item.s.* */
int ctail_write(struct inode *, coord_t *, lock_handle *, flow_t *, struct sealed_coord *, int);
int ctail_read(struct file *, coord_t *, flow_t *);
int ctail_readpage(coord_t *, struct page *);
int ctail_writepage(coord_t *, lock_handle *, struct page *);
void ctail_readpages(coord_t *, struct address_space *, struct list_head *);
reiser4_key *ctail_append_key(const coord_t * coord, reiser4_key * key, void *);
int ctail_key_in_item(coord_t * coord, const reiser4_key * key, void *);

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
