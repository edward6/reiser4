/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* cryptcompress object item. See ctail.c for description. */

typedef struct ctail_item_format {
	/* (PAGE_SIZE << cluster_shift) translated by ->scale()
	   method of crypto plugin */
	d32 disk_cluster_size;
	/* ctail body */
	d8 body[0];
} __attribute__((packed)) ctail_item_format;

#define CTAIL_MIN_BODY_SIZE MIN_CRYPTO_BLOCKSIZE  

#define prev_list_to_page(head) (list_entry((head)->prev->prev, struct page, list))

/* plugin->item.b.* */
int mergeable_ctail(const coord_t * p1, const coord_t * p2);
pos_in_item_t nr_units_ctail(const coord_t * coord);
int estimate_ctail(const coord_t * coord, const reiser4_item_data * data);
void print_ctail(const char *prefix, coord_t * coord);
lookup_result lookup_ctail(const reiser4_key *, lookup_bias, coord_t *);
int paste_ctail(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG);
int can_shift_ctail(unsigned free_space, coord_t * coord,
		  znode * target, shift_direction pend, unsigned *size, unsigned want);
void copy_units_ctail(coord_t * target, coord_t * source,
		    unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space);
int cut_units_ctail(coord_t * coord, unsigned *from, unsigned *to,
		  const reiser4_key * from_key, const reiser4_key * to_key, reiser4_key * smallest_removed, void *);
/*int check_check(const coord_t * coord, const char **error);*/

/* plugin->u.item.s.* */
int write_ctail(struct inode *, flow_t *, hint_t *, int, write_mode_t);
int read_ctail(struct file *, flow_t *, uf_coord_t *);
int readpage_ctail(void *, struct page *);
int writepage_ctail(uf_coord_t *, struct page *, write_mode_t);
void readpages_ctail(void *, struct address_space *, struct list_head *);
reiser4_key *append_key_ctail(const coord_t *, reiser4_key *);
#if REISER4_DEBUG
int key_in_item_ctail(const uf_coord_t *, const reiser4_key *);
#endif

__u8 inode_cluster_shift (struct inode *);
size_t inode_cluster_size (struct inode *);
crypto_stat_t * inode_crypto_stat(struct inode *);

void reiser4_cluster_init(reiser4_cluster_t *);
void put_cluster_data(reiser4_cluster_t *, struct inode *);
int cluster_is_uptodate (reiser4_cluster_t *);
loff_t inode_scaled_offset(struct inode *, const loff_t);
size_t inode_scaled_cluster_size(struct inode *);
unsigned long cluster_index_by_page(struct page *, struct inode *);
int process_cluster(reiser4_cluster_t *, struct inode *, rw_op);
int find_cluster_item(const reiser4_key *, coord_t *, lock_handle *, ra_info_t *);
int page_of_cluster(struct page *, reiser4_cluster_t *, struct inode *);

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
