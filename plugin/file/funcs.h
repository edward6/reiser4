void get_exclusive_access(unix_file_info_t *);
void drop_exclusive_access(unix_file_info_t *);
void get_nonexclusive_access(unix_file_info_t *);
void drop_nonexclusive_access(unix_file_info_t *);
void drop_access(unix_file_info_t *uf_info);

int tail2extent(unix_file_info_t *);
int extent2tail(unix_file_info_t *);
int finish_conversion(struct inode *inode);

void hint_init_zero(hint_t *hint, lock_handle *lh);
int find_file_item(hint_t *, const reiser4_key *, znode_lock_mode, __u32 cbk_flags,
		   ra_info_t *, unix_file_info_t *);

int goto_right_neighbor(coord_t *, lock_handle *);
int find_or_create_extent(struct page *page);
write_mode_t how_to_write(uf_coord_t *, const reiser4_key *);
