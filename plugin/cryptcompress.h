/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __FS_REISER4_CRYPTCOMPRESS_H__ )
#define __FS_REISER4_CRYPTCOMPRESS_H__


#include <linux/pagemap.h>

#define MIN_CLUSTER_SIZE PAGE_CACHE_SIZE
#define MAX_CLUSTER_SHIFT 4
#define MIN_SIZE_FOR_COMPRESSION 64
#define MIN_CRYPTO_BLOCKSIZE 8
#define CLUSTER_MAGIC_SIZE (MIN_CRYPTO_BLOCKSIZE >> 1)

typedef enum {
	DATA_CLUSTER = 0,
	HOLE_CLUSTER = 1,
	FAKE_CLUSTER = 2
} reiser4_cluster_status;

typedef enum {
	CRC_FIRST_ITEM = 1,
	CRC_APPEND_ITEM = 2,
	CRC_OVERWRITE_ITEM = 3,
	CRC_CUT_ITEM = 4
} crc_write_mode_t;

/* reiser4 cluster manager transforms pages into set of items (and back) via
   assembling/splitting contunuous regions where crypto/compression algorithm live.
   This manager contains mostly operations with the following object: */
typedef struct reiser4_cluster{
	__u8 * buf;      /* pointer to the beginning of region where crypto/compression
			    algorithms live; this region contains only one cluster */
	size_t bsize;    /* the length of this region */
	size_t len;      /* current length of the processed cluster */
	int nr_pages;    /* number of attached pages */
	struct page ** pages; /* attached pages */
	struct file * file;
	reiser4_cluster_status stat;
	/* sliding frame of cluster size in loff_t-space to translate main file 'offsets'
	   like read/write position, size, new size (for truncate), etc.. into number
	   of pages, cluster status, etc..*/
	unsigned long index; /* cluster index, coord of the frame */
	unsigned off;    /* offset we want to read/write/eraze from */
	unsigned count;  /* bytes to read/write/eraze */
	unsigned delta;  /* bytes of user's data to append to the hole */
} reiser4_cluster_t;

/* secret key params supposed to be stored on disk */
typedef struct crypto_stat {
	__u8 * keyid;  /* key public id */
	__u16 keysize; /* key size, bits */
} crypto_stat_t;

typedef struct cryptcompress_info {
	/* cpu-key words */
	__u32 * expkey;
} cryptcompress_info_t;

cryptcompress_info_t *cryptcompress_inode_data(const struct inode * inode);
int equal_to_rdk(znode *, const reiser4_key *);
int equal_to_ldk(znode *, const reiser4_key *);
int goto_right_neighbor(coord_t *, lock_handle *);
int load_file_hint(struct file *, hint_t *, lock_handle *);
void save_file_hint(struct file *, const hint_t *);

/* declarations of functions implementing file plugin for unix file plugin */
int create_cryptcompress(struct inode *, struct inode *, reiser4_object_create_data *);
int truncate_cryptcompress(struct inode *, loff_t size);
int readpage_cryptcompress(void *, struct page *);
int capture_cryptcompress(struct inode *inode, struct writeback_control *wbc);
ssize_t write_cryptcompress(struct file *, const char *buf, size_t size, loff_t *off);
int release_cryptcompress(struct inode *inode, struct file *);
int mmap_cryptcompress(struct file *, struct vm_area_struct *vma);
int get_block_cryptcompress(struct inode *, sector_t block, struct buffer_head *bh_result, int create);
int flow_by_inode_cryptcompress(struct inode *, char *buf, int user, loff_t, loff_t, rw_op, flow_t *);
int key_by_inode_cryptcompress(struct inode *, loff_t off, reiser4_key *);
int delete_cryptcompress(struct inode *);
int owns_item_cryptcompress(const struct inode *, const coord_t *);
int setattr_cryptcompress(struct inode *, struct iattr *);
void readpages_cryptcompress(struct file *, struct address_space *, struct list_head *pages);
void init_inode_data_cryptcompress(struct inode *, reiser4_object_create_data *, int create);
int pre_delete_cryptcompress(struct inode *);
void hint_init_zero(hint_t *, lock_handle *);

#endif /* __FS_REISER4_CRYPTCOMPRESS_H__ */

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
