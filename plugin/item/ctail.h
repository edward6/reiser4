/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* cryptcompress object item. See ctail.c for description. */

typedef struct ctail_item_format {
	/* cluster shift */
	d8 cluster_shift;
	/* ctail body */
	d8 body[0];
} __attribute__((packed)) ctail_item_format;

typedef struct ctail_squeeze_info {
	struct inode * inode;
	reiser4_cluster_t * clust;
	flow_t flow;
} ctail_squeeze_info_t;

#define CTAIL_MIN_BODY_SIZE MIN_CRYPTO_BLOCKSIZE  

#define list_to_page(head) (list_entry((head)->prev, struct page, list))
#define list_to_next_page(head) (list_entry((head)->prev->prev, struct page, list))

/* plugin->item.b.* */
int can_contain_key_ctail(const coord_t *, const reiser4_key *, const reiser4_item_data *);
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
int write_ctail(flow_t *, coord_t *, lock_handle *, int, crc_write_mode_t, struct inode *);
int read_ctail(struct file *, flow_t *, uf_coord_t *);
int readpage_ctail(void *, struct page *);
int writepage_ctail(reiser4_key *, uf_coord_t *, struct page *, write_mode_t);
void readpages_ctail(void *, struct address_space *, struct list_head *);
reiser4_key *append_key_ctail(const coord_t *, reiser4_key *);
int kill_hook_ctail(const coord_t *, unsigned, unsigned, void *);

/* plugin->u.item.f */
int utmost_child_ctail(const coord_t *, sideof, jnode **);
int scan_ctail(flush_scan *, const coord_t *);
int squeeze_ctail(flush_pos_t *, int child);


__u8 inode_cluster_shift (struct inode *);
size_t inode_cluster_size (struct inode *);
crypto_stat_t * inode_crypto_stat(struct inode *);
unsigned long pg_to_clust(unsigned long, struct inode *);
loff_t clust_to_off(unsigned long, struct inode *);
unsigned long off_to_pg(loff_t);
unsigned off_to_pgoff(loff_t);
unsigned pg_to_off_to_cloff(unsigned long, struct inode *);

void reiser4_cluster_init(reiser4_cluster_t *);
void put_cluster_data(reiser4_cluster_t *, struct inode *);
int cluster_is_uptodate (reiser4_cluster_t *);
size_t inode_scaled_cluster_size(struct inode *);
__u8 inode_cluster_shift (struct inode * inode);
inline unsigned long pg_to_clust_to_pg(unsigned long idx, struct inode *);
int inflate_cluster(reiser4_cluster_t *, struct inode *);
int find_cluster_item(const reiser4_key *, coord_t *, lock_handle *, ra_info_t *, lookup_bias);
int page_of_cluster(struct page *, reiser4_cluster_t *, struct inode *);
int find_cluster(reiser4_cluster_t *, struct inode *, int read, int write);
int flush_cluster_pages(reiser4_cluster_t *, struct inode *);
int deflate_cluster(reiser4_cluster_t *, struct inode *);
void truncate_pages_cryptcompress(struct address_space *, loff_t);

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
