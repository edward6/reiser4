/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */
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

/* cluster lives in many places:
   - in user space
   - in adress space (splitted into pages)
   - in kernel memory (kmalloced buffer to apply inflation/deflation algorithms)
   - on disk (splitted into items)

   the following structure gathers these concepts 
*/
typedef struct reiser4_cluster{
	__u8 * buf;      /* pointer to the (inflated or deflated) kmalloced cluster's data */
	int bufsize;
	__u8 nr_pages;            /* number of cluster pages */
	struct page ** pages; /* cluster pages */
	struct file * file;
	size_t len;      /* size of the processed (i.e compressed,
			    aligned and encrypted cluster) */
	unsigned long index; /* cluster index (index of the first page) */
	size_t tlen;     /* size of updated buffer to release */
	reiser4_cluster_status stat;
	unsigned off;    /* write position in the (user space) cluster */  
	unsigned count;  /* bytes to write to the (user space) cluster */
	unsigned delta;  /* bytes of user's data to append to the hole */
} reiser4_cluster_t;

inline struct cryptcompress_info *cryptcompress_inode_data(const struct inode * inode);
int equal_to_rdk(znode *, const reiser4_key *);
int equal_to_ldk(znode *, const reiser4_key *);
int goto_right_neighbor(coord_t *, lock_handle *);
int load_file_hint(struct file *, hint_t *, lock_handle *);
void save_file_hint(struct file *, const hint_t *);

/* declarations of functions implementing file plugin for unix file plugin */
int create_cryptcompress(struct inode *, struct inode *, reiser4_object_create_data *);
int truncate_cryptcompress(struct inode *, loff_t size);
int readpage_cryptcompress(void *, struct page *);
int capture_cryptcompress(struct page *);
ssize_t write_cryptcompress(struct file *, const char *buf, size_t size, loff_t *off);
int release_cryptcompress(struct inode *inode, struct file *);
int mmap_cryptcompress(struct file *, struct vm_area_struct *vma);
int get_block_cryptcompress(struct inode *, sector_t block, struct buffer_head *bh_result, int create);
int flow_by_inode_cryptcompress(struct inode *, char *buf, int user, loff_t, loff_t, rw_op, flow_t *);
int cluster_key_by_inode(struct inode *, loff_t off, reiser4_key *);
int delete_cryptcompress(struct inode *);
int owns_item_cryptcompress(const struct inode *, const coord_t *);
int setattr_cryptcompress(struct inode *, struct iattr *);
void readpages_cryptcompress(struct file *, struct address_space *, struct list_head *pages);
void init_inode_data_cryptcompress(struct inode *, reiser4_object_create_data *, int create);
int pre_delete_cryptcompress(struct inode *);


/* secret key params supposed to be stored on disk */
typedef struct crypto_stat {
	__u8 * keyid;  /* key public id */
	__u16 keysize; /* key size, bits */
} crypto_stat_t; 

typedef struct cryptcompress_info {
	/* cpu-key words */
	__u32 * expkey;
} cryptcompress_info_t;

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
