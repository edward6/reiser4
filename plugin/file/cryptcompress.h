/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* See http://www.namesys.com/cryptcompress_design.html */

#if !defined( __FS_REISER4_CRYPTCOMPRESS_H__ )
#define __FS_REISER4_CRYPTCOMPRESS_H__

#include "../../page_cache.h"
#include "../compress/compress.h"
#include "../crypto/cipher.h"

#include <linux/pagemap.h>

#define MIN_CLUSTER_SIZE PAGE_CACHE_SIZE
#define MIN_CLUSTER_SHIFT PAGE_CACHE_SHIFT
#define MAX_CLUSTER_SHIFT 16
#define MAX_CLUSTER_NRPAGES (1U << MAX_CLUSTER_SHIFT >> PAGE_CACHE_SHIFT)
#define DC_CHECKSUM_SIZE 4

/* this mask contains all non-standard plugins that might
   be present in reiser4-specific part of inode managed by
   cryptcompress file plugin */
#define cryptcompress_mask				\
	((1 << PSET_FILE) |				\
	 (1 << PSET_CLUSTER) |				\
	 (1 << PSET_CIPHER) |				\
	 (1 << PSET_DIGEST) |				\
	 (1 << PSET_COMPRESSION) |			\
	 (1 << PSET_COMPRESSION_MODE))

static inline loff_t min_count(loff_t a, loff_t b)
{
	return (a < b ? a : b);
}

static inline loff_t max_count(loff_t a, loff_t b)
{
	return (a > b ? a : b);
}

#if REISER4_DEBUG
static inline int cluster_shift_ok(int shift)
{
	return (shift >= MIN_CLUSTER_SHIFT) && (shift <= MAX_CLUSTER_SHIFT);
}
#endif

typedef struct tfm_stream {
	__u8 *data;
	size_t size;
} tfm_stream_t;

typedef enum {
	INPUT_STREAM,
	OUTPUT_STREAM,
	LAST_STREAM
} tfm_stream_id;

typedef tfm_stream_t *tfm_unit[LAST_STREAM];

static inline __u8 *ts_data(tfm_stream_t * stm)
{
	assert("edward-928", stm != NULL);
	return stm->data;
}

static inline size_t ts_size(tfm_stream_t * stm)
{
	assert("edward-929", stm != NULL);
	return stm->size;
}

static inline void set_ts_size(tfm_stream_t * stm, size_t size)
{
	assert("edward-930", stm != NULL);

	stm->size = size;
}

static inline int alloc_ts(tfm_stream_t ** stm)
{
	assert("edward-931", stm);
	assert("edward-932", *stm == NULL);

	*stm = kmalloc(sizeof **stm, reiser4_ctx_gfp_mask_get());
	if (*stm == NULL)
		return -ENOMEM;
	memset(*stm, 0, sizeof **stm);
	return 0;
}

static inline void free_ts(tfm_stream_t * stm)
{
	assert("edward-933", !ts_data(stm));
	assert("edward-934", !ts_size(stm));

	kfree(stm);
}

static inline int alloc_ts_data(tfm_stream_t * stm, size_t size)
{
	assert("edward-935", !ts_data(stm));
	assert("edward-936", !ts_size(stm));
	assert("edward-937", size != 0);

	stm->data = reiser4_vmalloc(size);
	if (!stm->data)
		return -ENOMEM;
	set_ts_size(stm, size);
	return 0;
}

static inline void free_ts_data(tfm_stream_t * stm)
{
	assert("edward-938", equi(ts_data(stm), ts_size(stm)));

	if (ts_data(stm))
		vfree(ts_data(stm));
	memset(stm, 0, sizeof *stm);
}

/* Write modes for item conversion in flush convert phase */
typedef enum {
	CRC_APPEND_ITEM = 1,
	CRC_OVERWRITE_ITEM = 2,
	CRC_CUT_ITEM = 3
} cryptcompress_write_mode_t;

typedef enum {
	PCL_UNKNOWN = 0,	/* invalid option */
	PCL_APPEND = 1,		/* append and/or overwrite */
	PCL_TRUNCATE = 2	/* truncate */
} page_cluster_op;

/* Reiser4 file write/read transforms page cluster into disk cluster (and back)
   using crypto/compression transforms implemented by reiser4 transform plugins.
   Before each transform we allocate a pair of streams (tfm_unit) and assemble
   page cluster into the input one. After transform we split output stream into
   a set of items (disk cluster).
*/
typedef struct tfm_cluster {
	coa_set coa;
	tfm_unit tun;
	tfm_action act;
	int uptodate;
	int lsize;        /* size of the logical cluster */
	int len;          /* length of the transform stream */
} tfm_cluster_t;

static inline coa_t get_coa(tfm_cluster_t * tc, reiser4_compression_id id, tfm_action act)
{
	return tc->coa[id][act];
}

static inline void
set_coa(tfm_cluster_t * tc, reiser4_compression_id id, tfm_action act, coa_t coa)
{
	tc->coa[id][act] = coa;
}

static inline int
alloc_coa(tfm_cluster_t * tc, compression_plugin * cplug)
{
	coa_t coa;

	coa = cplug->alloc(tc->act);
	if (IS_ERR(coa))
		return PTR_ERR(coa);
	set_coa(tc, cplug->h.id, tc->act, coa);
	return 0;
}

static inline int
grab_coa(tfm_cluster_t * tc, compression_plugin * cplug)
{
	return (cplug->alloc && !get_coa(tc, cplug->h.id, tc->act) ?
		alloc_coa(tc, cplug) : 0);
}

static inline void free_coa_set(tfm_cluster_t * tc)
{
	tfm_action j;
	reiser4_compression_id i;
	compression_plugin *cplug;

	assert("edward-810", tc != NULL);

	for (j = 0; j < LAST_TFM; j++)
		for (i = 0; i < LAST_COMPRESSION_ID; i++) {
			if (!get_coa(tc, i, j))
				continue;
			cplug = compression_plugin_by_id(i);
			assert("edward-812", cplug->free != NULL);
			cplug->free(get_coa(tc, i, j), j);
			set_coa(tc, i, j, 0);
		}
	return;
}

static inline tfm_stream_t *tfm_stream(tfm_cluster_t * tc, tfm_stream_id id)
{
	return tc->tun[id];
}

static inline void
set_tfm_stream(tfm_cluster_t * tc, tfm_stream_id id, tfm_stream_t * ts)
{
	tc->tun[id] = ts;
}

static inline __u8 *tfm_stream_data(tfm_cluster_t * tc, tfm_stream_id id)
{
	return ts_data(tfm_stream(tc, id));
}

static inline void
set_tfm_stream_data(tfm_cluster_t * tc, tfm_stream_id id, __u8 * data)
{
	tfm_stream(tc, id)->data = data;
}

static inline size_t tfm_stream_size(tfm_cluster_t * tc, tfm_stream_id id)
{
	return ts_size(tfm_stream(tc, id));
}

static inline void
set_tfm_stream_size(tfm_cluster_t * tc, tfm_stream_id id, size_t size)
{
	tfm_stream(tc, id)->size = size;
}

static inline int
alloc_tfm_stream(tfm_cluster_t * tc, size_t size, tfm_stream_id id)
{
	assert("edward-939", tc != NULL);
	assert("edward-940", !tfm_stream(tc, id));

	tc->tun[id] = kmalloc(sizeof(tfm_stream_t), reiser4_ctx_gfp_mask_get());
	if (!tc->tun[id])
		return -ENOMEM;
	memset(tfm_stream(tc, id), 0, sizeof(tfm_stream_t));
	return alloc_ts_data(tfm_stream(tc, id), size);
}

static inline int
realloc_tfm_stream(tfm_cluster_t * tc, size_t size, tfm_stream_id id)
{
	assert("edward-941", tfm_stream_size(tc, id) < size);
	free_ts_data(tfm_stream(tc, id));
	return alloc_ts_data(tfm_stream(tc, id), size);
}

static inline void free_tfm_stream(tfm_cluster_t * tc, tfm_stream_id id)
{
	free_ts_data(tfm_stream(tc, id));
	free_ts(tfm_stream(tc, id));
	set_tfm_stream(tc, id, 0);
}

static inline unsigned coa_overrun(compression_plugin * cplug, int ilen)
{
	return (cplug->overrun != NULL ? cplug->overrun(ilen) : 0);
}

static inline void free_tfm_unit(tfm_cluster_t * tc)
{
	tfm_stream_id id;
	for (id = 0; id < LAST_STREAM; id++) {
		if (!tfm_stream(tc, id))
			continue;
		free_tfm_stream(tc, id);
	}
}

static inline void put_tfm_cluster(tfm_cluster_t * tc)
{
	assert("edward-942", tc != NULL);
	free_coa_set(tc);
	free_tfm_unit(tc);
}

static inline int tfm_cluster_is_uptodate(tfm_cluster_t * tc)
{
	assert("edward-943", tc != NULL);
	assert("edward-944", tc->uptodate == 0 || tc->uptodate == 1);
	return (tc->uptodate == 1);
}

static inline void tfm_cluster_set_uptodate(tfm_cluster_t * tc)
{
	assert("edward-945", tc != NULL);
	assert("edward-946", tc->uptodate == 0 || tc->uptodate == 1);
	tc->uptodate = 1;
	return;
}

static inline void tfm_cluster_clr_uptodate(tfm_cluster_t * tc)
{
	assert("edward-947", tc != NULL);
	assert("edward-948", tc->uptodate == 0 || tc->uptodate == 1);
	tc->uptodate = 0;
	return;
}

static inline int tfm_stream_is_set(tfm_cluster_t * tc, tfm_stream_id id)
{
	return (tfm_stream(tc, id) &&
		tfm_stream_data(tc, id) && tfm_stream_size(tc, id));
}

static inline int tfm_cluster_is_set(tfm_cluster_t * tc)
{
	int i;
	for (i = 0; i < LAST_STREAM; i++)
		if (!tfm_stream_is_set(tc, i))
			return 0;
	return 1;
}

static inline void alternate_streams(tfm_cluster_t * tc)
{
	tfm_stream_t *tmp = tfm_stream(tc, INPUT_STREAM);

	set_tfm_stream(tc, INPUT_STREAM, tfm_stream(tc, OUTPUT_STREAM));
	set_tfm_stream(tc, OUTPUT_STREAM, tmp);
}

/* a kind of data that we can write to the window */
typedef enum {
	DATA_WINDOW,		/* the data we copy form user space */
	HOLE_WINDOW		/* zeroes if we write hole */
} window_stat;

/* Sliding window of cluster size which should be set to the approprite position
   (defined by cluster index) in a file before page cluster modification by
   file_write. Then we translate file size, offset to write from, number of
   bytes to write, etc.. to the following configuration needed to estimate
   number of pages to read before write, etc...
*/
typedef struct reiser4_slide {
	unsigned off;		/* offset we start to write/truncate from */
	unsigned count;		/* number of bytes (zeroes) to write/truncate */
	unsigned delta;		/* number of bytes to append to the hole */
	window_stat stat;	/* a kind of data to write to the window */
} reiser4_slide_t;

/* The following is a set of possible disk cluster states */
typedef enum {
	INVAL_DISK_CLUSTER,	/* unknown state */
	PREP_DISK_CLUSTER,	/* disk cluster got converted by flush
				   at least 1 time */
	UNPR_DISK_CLUSTER,	/* disk cluster just created and should be
				   converted by flush */
	FAKE_DISK_CLUSTER	/* disk cluster doesn't exist neither in memory
				   nor on disk */
} disk_cluster_stat;

/*
   While implementing all transforms (from page to disk cluster, and back)
   reiser4 cluster manager fills the following structure incapsulating pointers
   to all the clusters for the same index including the sliding window above
*/
typedef struct reiser4_cluster {
	tfm_cluster_t tc;	/* transform cluster */
	int nr_pages;		/* number of pages */
	struct page **pages;	/* page cluster */
	page_cluster_op op;	/* page cluster operation */
	struct file *file;
	hint_t *hint;		/* disk cluster item for traversal */
	disk_cluster_stat dstat;	/* state of the current disk cluster */
	cloff_t index;		/* offset in the units of cluster size */
	reiser4_slide_t *win;	/* sliding window of cluster size */
	int reserved;		/* this indicates that space for disk
				   cluster modification is reserved */
#if REISER4_DEBUG
	reiser4_context *ctx;
	int reserved_prepped;
	int reserved_unprepped;
#endif

} reiser4_cluster_t;

static inline __u8 * tfm_input_data (reiser4_cluster_t * clust)
{
	return tfm_stream_data(&clust->tc, INPUT_STREAM);
}

static inline __u8 * tfm_output_data (reiser4_cluster_t * clust)
{
	return tfm_stream_data(&clust->tc, OUTPUT_STREAM);
}

static inline int reset_cluster_pgset(reiser4_cluster_t * clust, int nrpages)
{
	assert("edward-1057", clust->pages != NULL);
	memset(clust->pages, 0, sizeof(*clust->pages) * nrpages);
	return 0;
}

static inline int alloc_cluster_pgset(reiser4_cluster_t * clust, int nrpages)
{
	assert("edward-949", clust != NULL);
	assert("edward-1362", clust->pages == NULL);
	assert("edward-950", nrpages != 0 && nrpages <= MAX_CLUSTER_NRPAGES);

	clust->pages =
		kmalloc(sizeof(*clust->pages) * nrpages,
			reiser4_ctx_gfp_mask_get());
	if (!clust->pages)
		return RETERR(-ENOMEM);
	reset_cluster_pgset(clust, nrpages);
	return 0;
}

static inline void free_cluster_pgset(reiser4_cluster_t * clust)
{
	assert("edward-951", clust->pages != NULL);
	kfree(clust->pages);
	clust->pages = NULL;
}

static inline void put_cluster_handle(reiser4_cluster_t * clust)
{
	assert("edward-435", clust != NULL);

	put_tfm_cluster(&clust->tc);
	if (clust->pages)
		free_cluster_pgset(clust);
	memset(clust, 0, sizeof *clust);
}

static inline void inc_keyload_count(crypto_stat_t * data)
{
 	assert("edward-1410", data != NULL);
 	data->keyload_count++;
}

static inline void dec_keyload_count(crypto_stat_t * data)
{
 	assert("edward-1411", data != NULL);
 	assert("edward-1412", data->keyload_count > 0);
 	data->keyload_count--;
}

/* cryptcompress specific part of reiser4_inode */
typedef struct cryptcompress_info {
	struct rw_semaphore lock;
	crypto_stat_t *crypt;
	int compress_toggle;      /* current status of compressibility
				     is set by compression mode plugin */
#if REISER4_DEBUG
	int pgcount;              /* number of captured pages */
#endif
} cryptcompress_info_t;

static inline void toggle_compression (cryptcompress_info_t * info, int val)
{
	info->compress_toggle = val;
}

static inline int compression_is_on (cryptcompress_info_t * info)
{
	return info->compress_toggle;
}

cryptcompress_info_t *cryptcompress_inode_data(const struct inode *);
int equal_to_rdk(znode *, const reiser4_key *);
int goto_right_neighbor(coord_t *, lock_handle *);
int cryptcompress_inode_ok(struct inode *inode);
int coord_is_unprepped_ctail(const coord_t * coord);
extern int ctail_read_disk_cluster (reiser4_cluster_t *, struct inode *,
				    znode_lock_mode mode);
extern int do_readpage_ctail(struct inode *, reiser4_cluster_t *,
			     struct page * page, znode_lock_mode mode);
extern int ctail_insert_unprepped_cluster(reiser4_cluster_t * clust,
					  struct inode * inode);
extern int readpages_cryptcompress(struct file*, struct address_space*,
				   struct list_head*, unsigned);
int bind_cryptcompress(struct inode *child, struct inode *parent);
void destroy_inode_cryptcompress(struct inode * inode);
int grab_cluster_pages(struct inode *inode, reiser4_cluster_t * clust);
int write_conversion_hook(struct file *file, struct inode * inode, loff_t pos,
 			  reiser4_cluster_t * clust, int * progress);
crypto_stat_t * inode_crypto_stat (struct inode * inode);
void inherit_crypto_stat_common(struct inode * parent, struct inode * object,
				int (*can_inherit)(struct inode * child,
						   struct inode * parent));
void reiser4_attach_crypto_stat(struct inode * inode, crypto_stat_t * info);
void change_crypto_stat(struct inode * inode, crypto_stat_t * new);
crypto_stat_t * reiser4_alloc_crypto_stat (struct inode * inode);

static inline reiser4_tfma_t *
info_get_tfma (crypto_stat_t * info, reiser4_tfm id)
{
	return &info->tfma[id];
}

static inline struct crypto_tfm *
info_get_tfm (crypto_stat_t * info, reiser4_tfm id)
{
	return info_get_tfma(info, id)->tfm;
}

static inline void
info_set_tfm (crypto_stat_t * info, reiser4_tfm id, struct crypto_tfm * tfm)
{
	info_get_tfma(info, id)->tfm = tfm;
}

static inline struct crypto_tfm *
info_cipher_tfm (crypto_stat_t * info)
{
	return info_get_tfm(info, CIPHER_TFM);
}

static inline struct crypto_tfm *
info_digest_tfm (crypto_stat_t * info)
{
	return info_get_tfm(info, DIGEST_TFM);
}

static inline cipher_plugin *
info_cipher_plugin (crypto_stat_t * info)
{
	return &info_get_tfma(info, CIPHER_TFM)->plug->cipher;
}

static inline digest_plugin *
info_digest_plugin (crypto_stat_t * info)
{
	return &info_get_tfma(info, DIGEST_TFM)->plug->digest;
}

static inline void
info_set_plugin(crypto_stat_t * info, reiser4_tfm id, reiser4_plugin * plugin)
{
	info_get_tfma(info, id)->plug = plugin;
}

static inline void
info_set_cipher_plugin(crypto_stat_t * info, cipher_plugin * cplug)
{
	info_set_plugin(info, CIPHER_TFM, cipher_plugin_to_plugin(cplug));
}

static inline void
info_set_digest_plugin(crypto_stat_t * info, digest_plugin * plug)
{
	info_set_plugin(info, DIGEST_TFM, digest_plugin_to_plugin(plug));
}

#endif				/* __FS_REISER4_CRYPTCOMPRESS_H__ */

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
