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

/* cluster lives in many places:
   - in user space
   - in adress space (splitted into pages)
   - in kernel memory (kmalloced buffer to apply inflation/deflation algorithms)
   - on disk (splitted into items)

   the following structure gathers these concepts 
*/
typedef struct reiser4_cluster{
	__u8 * buf;      /* pointer to the (inflated or deflated) kmalloced cluster's data */
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

/* secret key params supposed to be stored on disk */
typedef struct crypto_stat {
	__u16 keysize; /* key size, bits */
	__u8 * keyid;  /* key public id */
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
