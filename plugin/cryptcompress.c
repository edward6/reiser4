/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* The object plugin of reiser4 crypto-compressed (crc-)files (see
   http://www.namesys.com/cryptcompress_design.txt for details). */

/* We store all the cryptcompress-specific attributes as following
   non-default plugins in plugin stat-data extension:
   1) file plugin
   2) crypto plugin
   3) digest plugin
   4) compression plugin
*/

#include "../debug.h"
#include "../inode.h"
#include "../jnode.h"
#include "../tree.h"
#include "../page_cache.h"
#include "../readahead.h"
#include "../forward.h"
#include "../super.h"
#include "../context.h"
#include "plugin.h"
#include "object.h"

#include <linux/writeback.h>
#include <linux/pagemap.h>

int do_readpage_ctail(reiser4_cluster_t *, struct page * page);
int ctail_read_cluster (reiser4_cluster_t *, struct inode *, int);
reiser4_key * append_cluster_key_ctail(const coord_t *, reiser4_key *);
int setattr_reserve(reiser4_tree *);
int reserve_cut_iteration(reiser4_tree *, const char *);
int writepage_ctail(struct page *);

/* get cryptcompress specific portion of inode */
cryptcompress_info_t *cryptcompress_inode_data(const struct inode * inode)
{
	return &reiser4_inode_data(inode)->file_plugin_data.cryptcompress_info;
}

static void
destroy_key(__u32 * expkey, crypto_plugin * cplug)
{
	assert("edward-410", cplug != NULL);
	assert("edward-411", expkey != NULL);
	
	xmemset(expkey, 0, (cplug->nr_keywords)*sizeof(__u32));
	reiser4_kfree(expkey, (cplug->nr_keywords)*sizeof(__u32));
}

static void
detach_crypto_stat(crypto_stat_t * stat, digest_plugin * dplug)
{
	assert("edward-412", stat != NULL);
	assert("edward-413",  dplug != NULL);
	
	reiser4_kfree(stat->keyid, (size_t)(dplug->digestsize));
	reiser4_kfree(stat, sizeof(*stat));
}

/*  1) fill cryptcompress specific part of inode
    2) set inode crypto stat which is supposed to be saved in stat-data */
static int
inode_set_crypto(struct inode * object, crypto_data_t * data)
{
	int result;
	crypto_stat_t * stat;
	cryptcompress_info_t * info = cryptcompress_inode_data(object);	
	crypto_plugin * cplug = crypto_plugin_by_id(data->cra);
	digest_plugin * dplug = digest_plugin_by_id(data->dia);
	void * digest_ctx = NULL;

	assert("edward-414", dplug != NULL);
	assert("edward-415", cplug != NULL);	
	assert("edward-416", data != NULL);
	assert("edward-417", data->key!= NULL);
	assert("edward-88", data->keyid != NULL);
	assert("edward-83", data->keyid_size != 0);
	assert("edward-89", data->keysize != 0);

	/* set secret key */
	info->expkey = reiser4_kmalloc((cplug->nr_keywords)*sizeof(__u32), GFP_KERNEL);
	if (!info->expkey)
		return RETERR(-ENOMEM);
	result = cplug->set_key(info->expkey, data->key);
	if (result)
		goto destroy_key;
	assert ("edward-34", !inode_get_flag(object, REISER4_SECRET_KEY_INSTALLED));
	inode_set_flag(object, REISER4_SECRET_KEY_INSTALLED);

        /* attach crypto stat */
	stat = reiser4_kmalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat) {
		result = -ENOMEM;
		goto destroy_key;
	}
	stat->keyid = reiser4_kmalloc((size_t)(dplug->digestsize), GFP_KERNEL);
	if (!stat->keyid) {
		reiser4_kfree(stat, sizeof(*stat));
		result = -ENOMEM;
		goto destroy_key;
	}
	/* fingerprint creation of the pair (@key, @keyid) includes two steps: */
	/* 1. encrypt keyid by key: */
	/* FIXME-EDWARD: add encryption of keyid */
	
	/* 2. make digest of encrypted keyid */
	result = dplug->alloc(digest_ctx);
	if (result)
		goto exit;
	dplug->init(digest_ctx);
	dplug->update(digest_ctx, data->keyid, data->keyid_size);
	dplug->final(digest_ctx, stat->keyid);
	dplug->free(digest_ctx);
	
	stat->keysize = data->keysize;
	reiser4_inode_data(object)->crypt = stat;
	return 0;
 exit:
	detach_crypto_stat(stat, dplug);
 destroy_key:
	destroy_key(info->expkey, cplug);
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}


/* plugin->create() method for crypto-compressed files

. install plugins
. attach crypto info if specified
. attach compression info if specified
. attach cluster info
*/
int
create_cryptcompress(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
{
	int result;
	scint_t *extmask;
	reiser4_inode * info;
	digest_plugin * dplug = NULL;
	crypto_plugin * cplug = NULL;
	compression_plugin * coplug = NULL;
	
	assert("edward-23", object != NULL);
	assert("edward-24", parent != NULL);
	assert("edward-26", inode_get_flag(object, REISER4_NO_SD));
	assert("edward-27", data->id == CRC_FILE_PLUGIN_ID);
	
	info = reiser4_inode_data(object);
	
	assert("edward-29", info != NULL);
	assert("edward-30", info->pset->crypto == NULL);
	assert("edward-85", info->pset->digest == NULL);
	assert("edward-31", info->pset->compression == NULL);

	extmask = &info->extmask;
	
	if (data->crypto) {
		/* set plugins and crypto stat */
		cplug = crypto_plugin_by_id(data->crypto->cra);
		dplug = digest_plugin_by_id(data->crypto->dia);
		result = inode_set_crypto(object, data->crypto);
		if (result)
			return result;
		result = scint_pack(extmask, scint_unpack(extmask) |
				    (1 << CRYPTO_STAT), GFP_ATOMIC);
		if (result)
			goto exit;
	}
	plugin_set_crypto(&info->pset, cplug);
	plugin_set_digest(&info->pset, dplug);
	
	if (data->compression)
		/* set plugin */
		coplug = compression_plugin_by_id(*data->compression);
	plugin_set_compression(&info->pset, coplug);
	
	/* cluster params always is necessary */
	if(!data->cluster) {
		printk("edward-418, create_cryptcompress: default cluster size (4K) was assigned\n");
		info->cluster_shift = 0;
	}
	else
		info->cluster_shift = *data->cluster;
	result = scint_pack(extmask, scint_unpack(extmask) |
			    (1 << PLUGIN_STAT) |
			    (1 << CLUSTER_STAT), GFP_ATOMIC);
	if (result)
		goto exit;
        /* set bits */
	info->plugin_mask |= (1 << REISER4_FILE_PLUGIN_TYPE) |
		(1 << REISER4_CRYPTO_PLUGIN_TYPE) |
		(1 << REISER4_DIGEST_PLUGIN_TYPE) |
		(1 << REISER4_COMPRESSION_PLUGIN_TYPE);

	/* save everything in disk stat-data */
	result = write_sd_by_inode_common(object);
	if (!result)
		return 0;
	/* save() method failed, release attached crypto info */
	inode_clr_flag(object, REISER4_CRYPTO_STAT_LOADED);
	inode_clr_flag(object, REISER4_CLUSTER_KNOWN);
	
 exit:
	if (info->crypt) {
		destroy_key(cryptcompress_inode_data(object)->expkey, cplug);
		inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
		detach_crypto_stat(info->crypt, dplug);
	}
	return result;
}

static int
save_len_cryptcompress_plugin(struct inode * inode, reiser4_plugin * plugin)
{
	assert("edward-457", inode != NULL);
	assert("edward-458", plugin != NULL);
	assert("edward-459", plugin->h.id == CRC_FILE_PLUGIN_ID);
	return 0;
}

int
load_cryptcompress_plugin(struct inode * inode, reiser4_plugin * plugin, char **area, int *len)
{
	assert("edward-455", inode != NULL);
	assert("edward-456", (reiser4_inode_data(inode)->pset != NULL));

	plugin_set_file(&reiser4_inode_data(inode)->pset, file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	return 0;
}

struct reiser4_plugin_ops cryptcompress_plugin_ops = {load_cryptcompress_plugin, save_len_cryptcompress_plugin, NULL, 8, NULL};

crypto_stat_t * inode_crypto_stat (struct inode * inode)
{
	assert("edward-90", inode != NULL);
	assert("edward-91", reiser4_inode_data(inode) != NULL);
	return (reiser4_inode_data(inode)->crypt);
}
	
__u8 inode_cluster_shift (struct inode * inode)
{
	reiser4_inode * info;
	
	assert("edward-92", inode != NULL);

	info = reiser4_inode_data(inode);

	assert("edward-93", info != NULL);
	assert("edward-94", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	assert("edward-95", info->cluster_shift <= MAX_CLUSTER_SHIFT);

	return info->cluster_shift;
}

/* returns number of pages in the cluster */
int inode_cluster_pages (struct inode * inode)
{
	return (1 << inode_cluster_shift(inode));
}

size_t inode_cluster_size (struct inode * inode)
{
	assert("edward-96", inode != NULL);
	
	return (PAGE_CACHE_SIZE << inode_cluster_shift(inode));
}

/* returns translated offset */
loff_t inode_scaled_offset (struct inode * inode,
			    const loff_t src_off /* input offset */)
{
	crypto_plugin * cplug;
	crypto_stat_t * stat;
	size_t size;
	
	assert("edward-97", inode != NULL);
	
	cplug = inode_crypto_plugin(inode);
	
	if (!cplug || src_off == get_key_offset(max_key()))
		return src_off;

	stat = inode_crypto_stat(inode);
	
	assert("edward-98", stat != NULL);	
	assert("edward-99", stat->keysize != 0);
	
	size = cplug->blocksize(stat->keysize);
	return cplug->scale(inode, size, src_off);
}

static inline loff_t min_count(loff_t a, loff_t b)
{
	return (a < b ? a : b);
}

/* returns disk cluster size */
size_t
inode_scaled_cluster_size (struct inode * inode)
{
	assert("edward-110", inode != NULL);
	assert("edward-111", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	return inode_scaled_offset(inode, inode_cluster_size(inode));
}

void reiser4_cluster_init (reiser4_cluster_t * clust){
	assert("edward-84", clust != NULL);
	xmemset(clust, 0, sizeof *clust);
	clust->stat = DATA_CLUSTER;
}

/* release cluster's data */

void release_cluster_buf(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-121", clust != NULL);
	assert("edward-124", inode != NULL);
	assert("edward-125", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));

	if (clust->buf) {
		assert("edward-615", clust->bsize != 0);
		reiser4_kfree(clust->buf, clust->bsize);
	}
}

void put_cluster_data(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-435", clust != NULL);
	
	release_cluster_buf(clust, inode);
	/* invalidate cluster data */
	xmemset(clust, 0, sizeof *clust);
}

/* returns true if we don't need to read new cluster from disk */
int cluster_is_uptodate (reiser4_cluster_t * clust)
{
	assert("edward-126", clust != NULL);
	return (clust->buf != NULL);
}

unsigned long
pg_to_clust(unsigned long idx, struct inode * inode)
{
	return idx >> inode_cluster_shift(inode);
}

static inline unsigned long
clust_to_pg(unsigned long idx, struct inode * inode)
{
	return idx << inode_cluster_shift(inode);
}

inline unsigned long
pg_to_clust_to_pg(unsigned long idx, struct inode * inode)
{
	return clust_to_pg(pg_to_clust(idx, inode), inode);
}

unsigned long
off_to_pg(loff_t off)
{
	return (off >> PAGE_CACHE_SHIFT);
}

static inline loff_t
pg_to_off(unsigned long idx)
{
	return ((loff_t)(idx) << PAGE_CACHE_SHIFT);
}

static inline unsigned long
off_to_clust(loff_t off, struct inode * inode)
{
	return pg_to_clust(off_to_pg(off), inode);
}

loff_t
clust_to_off(unsigned long idx, struct inode * inode)
{
	return pg_to_off(clust_to_pg(idx, inode));
}

static loff_t
off_to_clust_to_off(loff_t off, struct inode * inode)
{
	return clust_to_off(off_to_clust(off, inode), inode);
}

static inline unsigned long
off_to_clust_to_pg(loff_t off, struct inode * inode)
{
	return clust_to_pg(off_to_clust(off, inode), inode);
}

unsigned
off_to_pgoff(loff_t off)
{
	return off & (PAGE_CACHE_SIZE - 1);
}

static inline unsigned
off_to_cloff(loff_t off, struct inode * inode)
{
	return off & ((loff_t)(inode_cluster_size(inode)) - 1);
}

unsigned
pg_to_off_to_cloff(unsigned long idx, struct inode * inode)
{
	return off_to_cloff(pg_to_off(idx), inode);
}

/* return true if the cluster contains specified page */
int
page_of_cluster(struct page * page, reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-162", page != NULL);
	assert("edward-163", clust != NULL);
	assert("edward-164", inode != NULL);
	assert("edward-165", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	return (pg_to_clust(page->index, inode) == clust->index);
}

int count_to_nrpages(unsigned count)
{
	return (!count ? 0 : off_to_pg(count - 1) + 1);
}

/* set minimal number of cluster pages (start from first one)
   which cover hole and users data */
static void
set_nrpages_by_frame(reiser4_cluster_t * clust)
{
	assert("edward-180", clust != NULL);
	
	if (clust->count + clust->delta == 0) {
		/* nothing to write - nothing to read */
		clust->nr_pages = 0;
		return;
	}
	clust->nr_pages = count_to_nrpages(clust->off + clust->count + clust->delta);
}

static unsigned
off_to_count(loff_t off, unsigned long idx, struct inode * inode)
{
	if(idx > off_to_clust(off, inode))
		return 0;
	return min_count(inode_cluster_size(inode), off - clust_to_off(idx, inode));
}

unsigned
off_to_pgcount(loff_t off, unsigned long idx)
{
	if (idx > off_to_pg(off))
		return 0;
	return min_count(PAGE_CACHE_SIZE, off - pg_to_off(idx));
}

unsigned
fsize_to_count(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-288", clust != NULL);
	assert("edward-289", inode != NULL);
	
	return off_to_count(inode->i_size, clust->index, inode);
}

/* plugin->key_by_inode() */
int
key_by_inode_cryptcompress(struct inode *inode, loff_t off, reiser4_key * key)
{
	assert("edward-64", inode != 0);
	assert("edward-112", ergo(off != get_key_offset(max_key()), !off_to_cloff(off, inode)));
	/* don't come here with other offsets */
	
	build_sd_key(inode, key);
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) (!inode_crypto_stat(inode) ? off : inode_scaled_offset(inode, off)));
	return 0;
}

/* plugin->flow_by_inode */
int
flow_by_inode_cryptcompress(struct inode *inode /* file to build flow for */ ,
			    char *buf /* user level buffer */ ,
			    int user	/* 1 if @buf is of user space, 0 - if it is
					   kernel space */ ,
			    loff_t size /* buffer size */ ,
			    loff_t off /* offset to start io from */ ,
			    rw_op op /* READ or WRITE */ ,
			    flow_t * f /* resulting flow */)
{
	assert("edward-436", f != NULL);
	assert("edward-149", inode != NULL);
	assert("edward-150", inode_file_plugin(inode) != NULL);
	assert("edward-151", inode_file_plugin(inode)->key_by_inode == key_by_inode_cryptcompress);

	
	f->length = size;
	f->data = buf;
	f->user = user;
	f->op = op;
	
	if (op == WRITE_OP && user == 1)
		return 0;
	return key_by_inode_cryptcompress(inode, off, &f->key);
}

int
find_cluster_item(hint_t * hint, /* coord, lh, seal */
		  const reiser4_key *key, /* key of next cluster item to read */
		  znode_lock_mode lock_mode /* which lock */,
		  ra_info_t *ra_info,
		  lookup_bias bias)
{
	int result;
	coord_t *coord;
	
	assert("edward-152", schedulable());
	
	init_lh(hint->coord.lh);
	coord = &hint->coord.base_coord;
	if(hint) {
		result = hint_validate(hint, key, 1 /* check key */, lock_mode);
		if (!result) {
			if (coord->between == AFTER_UNIT && equal_to_rdk(coord->node, key)) {
				result = goto_right_neighbor(coord, hint->coord.lh);
				if (result == -E_NO_NEIGHBOR)
					return RETERR(-EIO);
				if (result)
					return result;
				assert("vs-1152", equal_to_ldk(coord->node, key));
				/* we moved to different node. Invalidate coord extension, zload is necessary to init it
				   again */
				hint->coord.valid = 0;
			}
			return CBK_COORD_FOUND;
		}
	}
	coord_init_zero(coord);
	hint->coord.valid = 0;
	return  coord_by_key(current_tree, key, coord, hint->coord.lh, lock_mode,
			     bias, LEAF_LEVEL, LEAF_LEVEL, CBK_UNIQUE, ra_info);
}

/* This represent reiser4 crypto alignment policy.
   Returns the size > 0 of aligning overhead, if we should align/cut,
   returns 0, if we shouldn't (alignment assumes appinding an overhead of the size > 0) */
static int
crypto_overhead(size_t len /* advised length */,
		reiser4_cluster_t * clust,
		struct inode * inode, rw_op rw)
{
	crypto_plugin * cplug = inode_crypto_plugin(inode);
	crypto_stat_t * stat = inode_crypto_stat (inode);
	size_t size = 0;
	int result = 0;
	int oh;
	
	assert("edward-486", clust != 0);

	if (!cplug || !cplug->align_cluster)
		return 0;
	if (!len)
		size = clust->len;
	
	assert("edward-615", size != 0);
	assert("edward-487", stat != NULL);
	
	switch (rw) {
	case WRITE_OP: /* align */
		assert("edward-488", size <= inode_cluster_size(inode));
		
		oh = size % cplug->blocksize(stat->keysize);
		
		if (!oh && size == fsize_to_count(clust, inode))
			/* cluster don't need alignment and didn't get compressed */
			return 0;
		assert("edward-489", cplug->blocksize != NULL);
		result = (cplug->blocksize(stat->keysize) - oh);
		break;
	case READ_OP: /* cut */
		assert("edward-490", size <= inode_scaled_cluster_size(inode));
		if (size >= inode_scaled_offset(inode, fsize_to_count(clust, inode)))
			/* cluster didn't get aligned */
			return 0;
		assert("edward-491", clust->buf != NULL);
		
		result = *(clust->buf + size - 1);
		break;
	default:
		impossible("edward-493", "bad option for getting alignment");
	}
	return result;
}

static void
alternate_buffers(reiser4_cluster_t * clust, __u8 ** buf, size_t * bufsize)
{
	__u8 * tmp_buf;
	size_t tmp_size;
	
	assert("edward-405", bufsize != NULL);
	assert("edward-406", *bufsize != 0);
	
	tmp_buf = *buf;
	tmp_size = *bufsize;
	
	*buf = clust->buf;
	*bufsize = clust->bsize;
	
	clust->buf = tmp_buf;
	clust->bsize = tmp_size;
}

/* maximal aligning overhead which can be appended
   to the flow before encryption if any */
unsigned
max_crypto_overhead(crypto_plugin * cplug, crypto_stat_t * stat)
{
	if (!cplug || !cplug->align_cluster)
		return 0;
	else {
		assert("edward-494", stat != NULL);
		return cplug->blocksize(stat->keysize);	
	}
}

unsigned
compress_overhead(compression_plugin * cplug)
{
	return (cplug ? cplug->overrun : 0);   
}

/* The following two functions represent reiser4 compression policy */
static int
try_compress(reiser4_cluster_t * clust, struct inode * inode)
{
	return (inode_compression_plugin(inode) && (clust->count >= MIN_SIZE_FOR_COMPRESSION));
}

/* Decide by the lengths of compressed and decompressed cluster, should we save or should
   we discard the result of compression. The policy is that the length of compressed then
   encrypted cluster including _all_ appended infrasrtucture should be _less_ then its lenght
   before compression. */
static int
save_compressed(reiser4_cluster_t * clust, struct inode * inode)
{
/* NOTE: Actually we use max_crypto_overhead instead of precise overhead
   (a bit stronger condition) to avoid divisions */
	return (clust->len + CLUSTER_MAGIC_SIZE +
		max_crypto_overhead(inode_crypto_plugin(inode), inode_crypto_stat(inode)) <
		clust->count);
}

/* guess if the cluster was compressed */
static int
need_decompression(reiser4_cluster_t * clust, struct inode * inode,
		   int encrypted /* is cluster encrypted */)
{
	assert("edward-142", clust != 0);
	assert("edward-143", inode != NULL);

	return (inode_compression_plugin(inode) && clust->len <
		(encrypted ?
		 inode_scaled_offset(inode, fsize_to_count(clust, inode)) :
		                            fsize_to_count(clust, inode)));
}

void set_compression_magic(__u8 * magic)
{
	/* FIXME-EDWARD: If crypto_plugin != NULL, this should be private!
	   Use 4 bytes of decrypted keyid. PARANOID? */
	assert("edward-279", magic != NULL);
	xmemset(magic, 0, CLUSTER_MAGIC_SIZE);
}

/*
  Common cluster deflate manager.
  
  . accept a flow as a single page or cluster of pages assembled into a buffer
    of cluster handle @clust
  . maybe allocate buffer @bf to store temporary results 
  . maybe compress accepted flow and attach compression magic if result of
    compression is acceptable
  . maybe align and encrypt the flow.
  . stores the result in the buffer of cluster handle
                                     _ _ _ _ _ _ _ _ 
				    |               |
                                    |  disk cluster |                                
                                    |_ _ _ _ _ _ _ _|  
                                            ^                 
                                            |  
          _______________            _______|_______       _ _ _ _ _ _ _ _
         |               | <---1--- |               |     |               |
         |     @bf       | ----2--> |     @clust    |<----|  page cluster |
         |_______________| ----3--> |_______________|     |_ _ _ _ _ _ _ _| 
                 ^                          ^
	       4 |      ______________      | 5
                 |     |              |     |
                 +---- |     page     | ----+
	               |______________| 

  1 - compression or encryption
  2 - encryption
  3 - alternation
  4 - compression
  5 - compression or encryption or copy

  FIXME-EDWARD: Currently the only symmetric crypto algorithms with ecb are
  supported 
*/

int
deflate_cluster(reiser4_cluster_t *clust, /* contains data to process */
		struct inode *inode)
{
	int result = 0; 
	__u8 * bf = NULL;
	__u8 * src = NULL;
	__u8 * dst = NULL;
	size_t bfsize = clust->count;
	struct page * pg = NULL; 
	
	assert("edward-401", inode != NULL);
	assert("edward-495", clust != NULL);
	assert("edward-496", clust->count != 0);
	assert("edward-497", clust->len == 0);
	assert("edward-498", clust->buf && clust->bsize);

	if (try_compress(clust, inode)) {
		/* try to compress, discard bad results */
		__u8 * dst;
		__u8 * wbuf;
		compression_plugin * cplug = inode_compression_plugin(inode);

		assert("edward-602", cplug != NULL);

		if (inode_crypto_plugin(inode) || !clust->pages) {
			/* [12], [42], [13], tmp buffer is required */
			bfsize += cplug->overrun;
			bf = reiser4_kmalloc(bfsize, GFP_KERNEL);
			if (!bf)
				return -ENOMEM;
			dst = bf;
		}
		else 
			/* [5] */
			dst = clust->buf;
		if (clust->pages) {
			/* [42], [5] */
			assert("edward-619", clust->nr_pages == 1);
			assert("edward-620", PageDirty(*clust->pages));
			assert("edward-499", inode_cluster_size(inode) == PAGE_CACHE_SIZE); 

			pg = *clust->pages;
			lock_page(pg);
			assert("edward-621", PageDirty(pg));
			src = kmap(pg);
		}
		else
			/* [12], [13] */
			src = clust->buf;
		
		wbuf = reiser4_kmalloc(cplug->mem_req, GFP_KERNEL);
		if (wbuf == NULL) {
			result = -ENOMEM;
			goto exit;
		}
		cplug->compress(wbuf, src, clust->count, dst/* res */, &clust->len/* res */);

		assert("edward-603", clust->len <= (bf ? bfsize : clust->bsize));
		
		/* estimate compression quality to accept or discard
		   the results of our efforts */
		if (save_compressed(clust, inode)) {
			/* Accepted */
			set_compression_magic(dst + clust->len);
			clust->len += CLUSTER_MAGIC_SIZE;
		}
		else 
			/* Discard */
			clust->len = clust->count;
		reiser4_kfree(wbuf, cplug->mem_req);
	}
	
	if (inode_crypto_plugin(inode)) {
		/* align and encrypt */
		int oh; /* ohhh, the crypto alignment overhead */
		int i, icb, ocb;
		__u32 * expkey;
		crypto_plugin * cplug = inode_crypto_plugin(inode);
		crypto_stat_t * stat = inode_crypto_stat(inode);
				
		assert("edward-604", stat != NULL);
		
		icb = cplug->blocksize(stat->keysize);
		ocb = inode_scaled_offset(inode, icb);
		
		assert("edward-605", icb != 0);
		
		/* precise crypto-overhead */
		oh = crypto_overhead(0, clust, inode, WRITE_OP);
		
		if (dst) {
			/* compression is specified */
			assert("edward-622", src != NULL);
			assert("edward-623", bf != NULL && clust->len != 0);
			assert("edward-624", clust->len <= clust->count);
			
			if (clust->len != clust->count)
				/* saved */
				src = dst;
			else
				/* refused */
				;
			if (pg) {
                                /* flush the page */
				assert("edward-625", PageLocked(pg));
				
				kunmap(pg);
				uncapture_page(pg);
				unlock_page(pg);
				page_cache_release(pg);
				reiser4_kfree(clust->pages, sizeof(*clust->pages));
				pg = NULL;
			}
		}
		else {
			/* [13], [5], compression wasn't specified */

			assert("edward-626", !clust->len);
			
			if (!clust->pages) {
				/* [13], tmp buffer required */
				assert("edward-627", !bf);

				bfsize += oh;
				bf = reiser4_kmalloc(bfsize, GFP_KERNEL);
				if (!bf)
					goto exit;
				alternate_buffers(clust, &bf, &bfsize);
				src = bf;
			}
			else {
				/* [5] */
				pg = *clust->pages;
				lock_page(pg);
				assert("edward-628", PageDirty(pg));
				src = kmap(pg);
			}
			clust->len = clust->count;
		}

		dst = clust->buf;
		
		if (oh) {
			/* align the source */
			clust->len += cplug->align_cluster(src + clust->len, clust->len, icb);
			
			assert("edward-402", clust->len <= (pg ? PAGE_CACHE_SIZE : bfsize));

			*(src + clust->len - 1) = oh;
		}
#if REISER4_DEBUG	
		if (clust->len % icb)
			impossible("edward-403", "bad alignment");
#endif
		
		expkey = cryptcompress_inode_data(inode)->expkey;
		
		assert("edward-404", expkey != NULL);
		
		for (i=0; i < clust->len/icb; i++)
			cplug->encrypt(expkey, clust->buf + i*ocb /* dst */, src + i*icb);
	}
	
	else if (bf && clust->len != clust->count)
		/* [13], saved compression, no encryption */
		alternate_buffers(clust, &bf, &bfsize);
	else
		/* not specified or discarded compression, no encryption */
		if (clust->pages) {
			if (!pg) {
				assert("edward-629", !src);
				assert("edward-631", !clust->len);
				/* -not specified, [5] */
				pg = *clust->pages;
				lock_page(pg);
				src = kmap(pg);
				clust->len = clust->count;
			}
			else {
				/* -discarded, [13] */
				assert("edward-630", src != NULL);
				assert("edward-632", clust->len == clust->count);
			}
			xmemcpy(clust->buf, src, clust->count);
		}
 exit:
	if (bf)
		reiser4_kfree(bf, bfsize);
	if (pg) {
		assert("edward-621", PageLocked(pg));
		
		kunmap(pg);
		uncapture_page(pg);
		unlock_page(pg);
		page_cache_release(pg);
		reiser4_kfree(clust->pages, sizeof(*clust->pages));
	}
	return result;
}
          
/* Common inflate cluster manager. Is used in readpage() or readpages() methods of
   cryptcompress object plugins.
   . maybe allocate temporary buffer (@bf)   
   . maybe decrypt disk cluster (assembled in united flow of cluster handle) and
     cut crypto-alignment overhead (if any)
   . maybe check for compression magic and decompress
   
   The final result is stored in the buffer of the cluster handle (@clust)
   (which contained assembled disk cluster at the beginning of this procedure)
   and is supposed to be sliced into page cluster by appropriate fillers, but if
   cluster size is equal PAGE_SIZE we fill the single page (@pg) right here: 

                                      _ _ _ _ _ _ _ _ 
                                     |               | 
                                     |  disk cluster |                                
                                     |_ _ _ _ _ _ _ _|  
                                             |                 
                                             |  
          ________________            _______V_______       _ _ _ _ _ _ _ _
         |                | <---1--- |               |     |               |
         |      @bf       | ----2--> |     @clust    |---->|  page cluster |
         |________________| ----3--> |_______________|     |_ _ _ _ _ _ _ _| 
                 |                           |
	       4 |      _______________      | 5
                 |     |               |     |
                 +---> |      @pg      | <---+
	               |_______________| 

  1, 5 - decryption or decompression
  2, 4 - decompression
  3    - alternation

*/
int
inflate_cluster(reiser4_cluster_t *clust, /* cluster handle, contains assembled
					     disk cluster to process */
		struct inode *inode)
{
	/* FIXME-EDWARD: Need more revisions */
	
	int result = 0;
	__u8 * dst;
	__u8 * bf = NULL;  /* buffer to handle temporary results */
	size_t bfsize = 0; /* size of the buffer above */
	struct page * pg = NULL; /* pointer to a single page if cluster size
				    is equal page size */
	if (clust->stat == FAKE_CLUSTER)
		return 0;
	
	assert("edward-407", clust->buf != NULL);
	assert("edward-408", clust->len != 0);

	if (inode_crypto_plugin(inode) != NULL) {
		/* decrypt */
		int i;
		int oh = 0;
		int icb, ocb;
		__u32 * expkey;
		crypto_plugin * cplug = inode_crypto_plugin(inode);
		crypto_stat_t * stat = inode_crypto_stat(inode);

		assert("edward-616", stat != 0);
		assert("edward-617", cplug != 0);

		if (clust->pages)
			pg = *clust->pages;
		oh = crypto_overhead(0, clust, inode, READ_OP);

		/* input/output crypto blocksizes */
		icb = cplug->blocksize(stat->keysize);
		ocb = inode_scaled_offset(inode, icb);

		assert("edward-608", clust->len % ocb);
		
		if (pg && !need_decompression(clust, inode,
					      1 /* estimate for encrypted cluster */)) {
			/* [5] */
			assert("edward-609", clust->nr_pages == 1);
			assert("edward-610", inode_cluster_size(inode) == PAGE_CACHE_SIZE);
			
			lock_page(pg);
			if (PageUptodate(pg)) {
				/* races with other read/write */
				goto exit;
			}
			dst = kmap(pg);
		}	
		else { /* [12] or [13], tmp buffer is needed, estimate its size */
			bfsize = fsize_to_count(clust, inode);
			bfsize += crypto_overhead(bfsize, clust, inode, WRITE_OP);
			bf = reiser4_kmalloc(bfsize, GFP_KERNEL);
			if (!bf) {
				result = -ENOMEM;
				goto exit;
			}
			dst = bf;
		}
		
		/* decrypt cluster with the simplest mode
		 * FIXME-EDWARD: call here stream mode plugin */		
		
		expkey = cryptcompress_inode_data(inode)->expkey;
		
		assert("edward-141", expkey != NULL);
		
		for (i=0; i < clust->len/ocb; i++)
			cplug->decrypt(expkey, dst + i*icb /* dst */, clust->buf + i*ocb /* src */);
		
                /* cut the alignment overhead */
		clust->len -= crypto_overhead(0, clust, inode, READ_OP);
	}
	if (need_decompression(clust, inode, 0 /* estimate for decrypted cluster */)) {
		__u8 * src = bf;
		__u8 * wbuf;
		__u8 magic[CLUSTER_MAGIC_SIZE];
		compression_plugin * cplug = inode_compression_plugin(inode);
		
		src = bf;

		if (clust->pages)
			pg = *clust->pages;
		
		if (pg) {
			/* [5] or [14] */
			lock_page(pg);
			if (PageUptodate(pg)) {
				/* races with other read/write */
				goto exit;
			}
			dst = kmap(pg);
			if (!bf)
				src = clust->buf;
		}
		else {
			/* [12] or [13] */
			if (!bf) {
				/* [13], tmp buffer is needed, estimate its size */
				bfsize = fsize_to_count(clust, inode);
				bf = reiser4_kmalloc(bfsize, GFP_KERNEL);
				if (!bf) {
					result = -ENOMEM;
					goto exit;
				}
				alternate_buffers(clust, &bf, &bfsize);
			}
			dst = clust->buf;
		}
			
		/* Check compression magic for possible IO errors.
		   
		   End-of-cluster format created before encryption:
		   
		   data
		   compression_magic  (4)   Indicates presence of compression
		                            infrastructure, should be private.
		                            Can be absent.  
		   crypto_overhead          Created by ->align() method of crypto-plugin,
		                            Can be absent.
		
		   Crypto overhead format:			
		
		   data 				
		   tail_size           (1)   size of aligning tail,
		                             1 <= tail_size <= blksize
		*/
		set_compression_magic(magic);
		
		if (memcmp(src + (clust->len - (size_t)CLUSTER_MAGIC_SIZE),
			   magic, (size_t)CLUSTER_MAGIC_SIZE)) {
			printk("edward-156: wrong compression magic\n");
			result = -EIO;
			goto exit;
		}
		/* decompress cluster */
		wbuf = reiser4_kmalloc(cplug->mem_req, GFP_KERNEL);
		if (wbuf == NULL) {
			result = -ENOMEM;
			goto exit;
		}
		cplug->decompress(wbuf, src, clust->len, dst, &clust->len);

		/* check the length of decompressed data */
		assert("edward-157", clust->len == fsize_to_count(clust, inode));
		
		reiser4_kfree(wbuf, cplug->mem_req);
	}
 exit:
	if (bf)
		reiser4_kfree(bf, bfsize);
	if (pg) {
		assert("edward-611", PageLocked(pg));
		
		if (!PageUptodate(pg)) {
			
			assert("edward-618", clust->len <= PAGE_CACHE_SIZE);
			
			xmemset(pg, 0, PAGE_CACHE_SIZE - clust->len);
			kunmap(pg);
			SetPageUptodate(pg);
		}
		unlock_page(pg);
	}
	return result;	
}

/* plugin->read() :
 * generic_file_read()
 * All key offsets don't make sense in traditional unix semantics unless they
 * represent the beginning of clusters, so the only thing we can do is start
 * right from mapping to the address space (this is precisely what filemap
 * generic method does) */

/* plugin->readpage() */
int
readpage_cryptcompress(void *vp, struct page *page)
{
	reiser4_cluster_t clust;
	struct file * file;
	item_plugin * iplug;
	int result;
	
	assert("edward-88", PageLocked(page));
	assert("edward-89", page->mapping && page->mapping->host);
	
	file = vp;
	if (file)
		assert("edward-113", page->mapping == file->f_dentry->d_inode->i_mapping);
	
	if (PageUptodate(page)) {
		printk("readpage_cryptcompress: page became already uptodate\n");
		unlock_page(page);
		return 0;
	}
	reiser4_cluster_init(&clust);
	
	iplug = item_plugin_by_id(CTAIL_ID);
	if (!iplug->s.file.readpage)
		return -EINVAL;
	
	result = iplug->s.file.readpage(&clust, page);
	
	assert("edward-64", ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
	/* if page has jnode - that jnode is mapped
	   assert("edward-65", ergo(result == 0 && PagePrivate(page),
	   jnode_mapped(jprivate(page))));
	*/
	return result;
}

/* plugin->readpages() */
void
readpages_cryptcompress(struct file *file UNUSED_ARG, struct address_space *mapping,
			struct list_head *pages)
{
	item_plugin *iplug;
	
	iplug = item_plugin_by_id(CTAIL_ID);
	iplug->s.file.readpages(NULL, mapping, pages);
	return;
}

static void
set_cluster_pages_dirty(reiser4_cluster_t * clust, int * num)
{
	int i, nr;
	struct page * pg;

	nr = (num ? *num : clust->nr_pages);
	
	for (i=0; i < nr; i++) {
		
		pg = clust->pages[i];
		
		lock_page(pg);
		
		set_page_dirty_internal(pg);
		SetPageUptodate(pg);
		if (!PageReferenced(pg))
			SetPageReferenced(pg);	
		
		unlock_page(pg);
		
		page_cache_release(pg);
	}
}

/* This is the interface to capture cluster nodes via their struct page reference.
   Any two blocks of the same cluster contain dependent modification and should
   commit at the same time */
static int
try_capture_cluster(reiser4_cluster_t * clust, int * num)
{
	int i, nr;
	int result = 0;

	nr = (num ? *num : clust->nr_pages);
	
	for (i=0; i < nr; i++) {
		jnode * node;
		struct page *pg;
		
		pg = clust->pages[i];
		node = jprivate(pg);
		
		assert("edward-220", node != NULL);
		
		LOCK_JNODE(node);
		
		result = try_capture(node, ZNODE_WRITE_LOCK, 0/* not non-blocking */, 0 /* no can_coc */);
		if (result) {
			UNLOCK_JNODE(node);
			jput(node);
			break;
		}
		UNLOCK_JNODE(node);
	}
	if(result)
		/* drop nodes */
		while(i) {
			i--;
			uncapture_jnode(jprivate(clust->pages[i]));
		}
	return result;
}

static void
make_cluster_jnodes_dirty(reiser4_cluster_t * clust, int *num)
{
	int i, nr;
	jnode * node;

	nr = (num? *num : clust->nr_pages);
	
	for (i=0; i < nr; i++) {
		node = jprivate(clust->pages[i]);
		
		assert("edward-221", node != NULL);
		
		LOCK_JNODE(node);
		jnode_make_dirty_locked(node);
		UNLOCK_JNODE(node);
		
		jput(node);
	}
}

/* collect unlocked cluster pages and jnodes */
static int
grab_cache_cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	jnode * node;
	
	assert("edward-182", clust != NULL);
	assert("edward-183", clust->pages != NULL);
	assert("edward-437", clust->nr_pages != 0);
	assert("edward-184", 0 < clust->nr_pages <= inode_cluster_pages(inode));
	
	for (i = 0; i < clust->nr_pages; i++) {
		clust->pages[i] = grab_cache_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		if (!(clust->pages[i])) {
			result = RETERR(-ENOMEM);
			break;
		}
		node = jnode_of_page(clust->pages[i]);
		unlock_page(clust->pages[i]);
		if (IS_ERR(node)) {
			page_cache_release(clust->pages[i]);
			result = PTR_ERR(node);
			break;
		}
		JF_SET(node, JNODE_CLUSTER_PAGE);
	}
	if (result) {
		while(i) {
			i--;
			page_cache_release(clust->pages[i]);
			assert("edward-222", jprivate(clust->pages[i]) != NULL);
			jput(jprivate(clust->pages[i]));
		}
	}
	return result;
}

static void
put_cluster_jnodes(reiser4_cluster_t * clust)
{
	int i;
	
	assert("edward-223", clust != NULL);
	
	for (i=0; i < clust->nr_pages; i++) {
		
		assert("edward-208", clust->pages[i] != NULL);
		assert("edward-224", jprivate(clust->pages[i]) != NULL);
		
		jput(jprivate(clust->pages[i]));
	}
}

/* put cluster pages and jnodes */
static void
release_cluster_pages(reiser4_cluster_t * clust, int from)
{
	int i;
	
	assert("edward-447", clust != NULL);
	assert("edward-448", from < clust->nr_pages);
	
	for (i = from; i < clust->nr_pages; i++) {
		
		assert("edward-449", clust->pages[i] != NULL);

		page_cache_release(clust->pages[i]);
	}
}

static void
release_cluster(reiser4_cluster_t * clust)
{
	int i;
	
	assert("edward-445", clust != NULL);
	
	for (i=0; i < clust->nr_pages; i++) {
		
		assert("edward-446", clust->pages[i] != NULL);
		assert("edward-447", jprivate(clust->pages[i]) != NULL);
		
		page_cache_release(clust->pages[i]);
		jput(jprivate(clust->pages[i]));
	}
}

/* debugging purposes */
int
cluster_invariant(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-279", clust != NULL);
	
	return (clust->pages != NULL &&
		clust->off < inode_cluster_size(inode) &&
		ergo(clust->delta != 0, clust->stat == HOLE_CLUSTER) &&
		clust->off + clust->count + clust->delta <= inode_cluster_size(inode));
}

/* guess next cluster status */
static inline reiser4_cluster_status
next_cluster_stat(reiser4_cluster_t * clust)
{
	return (clust->stat == HOLE_CLUSTER && clust->delta == 0 /* no non-zero data */ ? HOLE_CLUSTER : DATA_CLUSTER);
}

/* guess next cluster params */
static void
update_cluster(struct inode * inode, reiser4_cluster_t * clust, loff_t file_off, loff_t to_file)
{
	assert ("edward-185", clust != NULL);
	assert ("edward-438", clust->pages != NULL);
	assert ("edward-281", cluster_invariant(clust, inode));
	
	switch (clust->stat) {
	case DATA_CLUSTER:
		/* increment */
		clust->stat = DATA_CLUSTER;
		clust->off = 0;
		clust->index++;
		clust->count = min_count(inode_cluster_size(inode), to_file);
		xmemset(clust->pages, 0, sizeof(clust->pages) << inode_cluster_shift(inode));
		break;
	case HOLE_CLUSTER:
		switch(next_cluster_stat(clust)) {
		case HOLE_CLUSTER:
			/* skip */
			clust->stat = HOLE_CLUSTER;
			clust->off = 0;
			clust->index = off_to_clust(file_off, inode);
			clust->count = off_to_cloff(file_off, inode);
			clust->delta = min_count(inode_cluster_size(inode) - clust->count, to_file);
			xmemset(clust->pages, 0, sizeof(clust->pages) << inode_cluster_shift(inode));
			break;
		case DATA_CLUSTER:
			/* keep immovability */
			clust->stat = DATA_CLUSTER;
			clust->off = clust->off + clust->count;
			clust->count = clust->delta;
			clust->delta = 0;
			break;
		default:
			impossible ("edward-282", "wrong next cluster status");
		}
	default:
		impossible ("edward-283", "wrong current cluster status");
	}
}

static int
__reserve4cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	int result = 0;
	jnode * j;

	assert("edward-439", inode != NULL);
	assert("edward-440", clust != NULL);
	assert("edward-441", clust->pages != NULL);
	assert("edward-442", jprivate(clust->pages[0]) != NULL);
	
	j = jprivate(clust->pages[0]);
	
	LOCK_JNODE(j);
	if (JF_ISSET(j, JNODE_CREATED)) {
		/* jnode mapped <=> space reserved */
		UNLOCK_JNODE(j);
		return 0;
	}
	result = reiser4_grab_space_force(
		/* estimate_insert_flow(current_tree->height) + estimate_one_insert_into_item(current_tree) */
		estimate_insert_cluster(inode), 0);
	if (result)
		return result;
	JF_SET(j, JNODE_CREATED);
	
	grabbed2cluster_reserved(estimate_insert_cluster(inode));

	UNLOCK_JNODE(j);
	return 0;
}

#if REISER4_TRACE
#define reserve4cluster(inode, clust, msg)    __reserve4cluster(inode, clust)
#else
#define reserve4cluster(inode, clust, msg)    __reserve4cluster(inode, clust)
#endif

static void
free_reserved4cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	jnode * j;
	
	j = jprivate(clust->pages[0]);

	LOCK_JNODE(j);
	
	assert("edward-443", jnode_is_cluster_page(j));
	assert("edward-444", JF_ISSET(j, JNODE_CREATED));

	cluster_reserved2free(estimate_insert_cluster(inode));
	JF_CLR(j, JNODE_CREATED);
	UNLOCK_JNODE(j);
}

int
update_inode_cryptcompress(struct inode *inode,
			      loff_t new_size,
			      int update_i_size, int update_times,
			      int do_update)
{
	int result = 0;
	int old_grabbed;
	reiser4_context *ctx = get_current_context();
	reiser4_super_info_data * sbinfo = get_super_private(ctx->super);
	
	old_grabbed = ctx->grabbed_blocks;
	
	grab_space_enable();

	result = reiser4_grab_space(/* one for stat data update */
		estimate_update_common(inode),
		0/* flags */);
	if (result)
		return result;
	result = update_inode_and_sd_if_necessary(inode, new_size, update_i_size, update_times, do_update);
	grabbed2free(ctx, sbinfo, ctx->grabbed_blocks - old_grabbed);
	return result;
}

/* stick pages into united flow, then release the ones */
int
flush_cluster_pages(reiser4_cluster_t * clust, struct inode * inode)
{
	int i;

	assert("edward-236", inode != NULL);
	assert("edward-237", clust != NULL);
	assert("edward-238", clust->off == 0);
	assert("edward-239", clust->count == 0);	
	assert("edward-240", clust->delta == 0);
	assert("edward-241", schedulable());
	
	clust->count = fsize_to_count(clust, inode);
	set_nrpages_by_frame(clust);

	cluster_reserved2grabbed(estimate_insert_cluster(inode));

	/* estimate max size of the cluster after compression and encryption
	   including all appended infrastructure, and allocate a buffer */
	clust->bsize = clust->count +
		max_crypto_overhead(inode_crypto_plugin(inode), inode_crypto_stat(inode));
	clust->bsize = inode_scaled_offset(inode, clust->bsize);
	
	if (clust->bsize > inode_scaled_cluster_size(inode))
		clust->bsize = inode_scaled_cluster_size(inode);
	
	clust->bsize += compress_overhead(inode_compression_plugin(inode));
	
	clust->buf = reiser4_kmalloc(clust->bsize, GFP_KERNEL);
	if (!clust->buf)
		return -ENOMEM;
	
	if (inode_cluster_size(inode) == PAGE_CACHE_SIZE) {
		/* do not flush single page */
		assert("edward-612", clust->nr_pages == 1);

		clust->pages = reiser4_kmalloc(sizeof(*clust->pages), GFP_KERNEL);
		if (!clust->pages)
			return -ENOMEM;
		
		*clust->pages = find_get_page(inode->i_mapping, clust_to_pg(clust->index, inode));
		
		assert("edward-613", *clust->pages != NULL);
		assert("edward-614", PageDirty(*clust->pages));
		
		return 0;
	}

        /* flush more then one page after its assembling in united flow */
	for(i=0; i < clust->nr_pages; i++){
		struct page * page;
		char * data;
		
		page = find_get_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		
		assert("edward-242", page != NULL);
		assert("edward-243", PageDirty(page));
		/* FIXME_EDWARD: Make sure that jnodes are from the same dirty list */
		
		lock_page(page);
		data = kmap(page);
		xmemcpy(clust->buf + (i << PAGE_CACHE_SHIFT), data, PAGE_CACHE_SIZE);
		kunmap(page);
		uncapture_page(page);
		unlock_page(page);
		page_cache_release(page);
	}
	return 0;
}

/* set zeroes to the cluster, update it, and maybe, try to capture its pages */
static int
write_hole(struct inode *inode, reiser4_cluster_t * clust, loff_t file_off, loff_t to_file)
{
	char * data;
	int result = 0;
	unsigned cl_off, cl_count = 0;
	unsigned to_pg, pg_off;
	
	assert ("edward-190", clust != NULL);
	assert ("edward-191", inode != NULL);
	assert ("edward-192", cluster_invariant(clust, inode));
	assert ("edward-201", clust->stat == HOLE_CLUSTER);
	
	if (clust->off == 0 && clust->count == inode_cluster_size(inode)) {
		/* fake cluster, just update it */
		goto update;
	}
	
	if (clust->count == 0) {
		/* nothing to write */
		goto update;
	}
	cl_count = clust->count; /* number of zeroes to write */
	cl_off = clust->off;
	pg_off = off_to_pgoff(clust->off);
	
	while (cl_count) {
		struct page * page;
		page = clust->pages[off_to_pg(cl_off)];

		assert ("edward-284", page != NULL);
		
		to_pg = min_count(PAGE_CACHE_SIZE - pg_off, cl_count);
		lock_page(page);
		data = kmap_atomic(page, KM_USER0);
		xmemset(data + pg_off, 0, to_pg);
		kunmap_atomic(data, KM_USER0);
		unlock_page(page);
		
		cl_off += to_pg;
		cl_count -= to_pg;
		pg_off = 0;
	}
	if (!clust->delta) {
		/* only zeroes, try to flush */
		
		set_cluster_pages_dirty(clust, NULL);
		result = try_capture_cluster(clust, NULL);
		if (result)
			return result;
		make_cluster_jnodes_dirty(clust, NULL);
		result = update_inode_cryptcompress(inode, clust_to_off(clust->index, inode) + clust->off + clust->count, 1, 1, 1);
		if (result)
			return result;
		balance_dirty_pages_ratelimited(inode->i_mapping);
	}
 update:
	update_cluster(inode, clust, file_off, to_file);
	return 0;
}

/*
  This is the main disk search procedure for cryptcompress plugins, which
  . finds all items of a disk cluster,
  . maybe reads each of them to the flow (if @read != 0)
  . maybe makes each znode dirty (if @write != 0)
*/
int
find_cluster(reiser4_cluster_t * clust,
	     struct inode * inode,
	     int read,
	     int write)
{
	flow_t f;
	lock_handle lh;
	hint_t hint;
	int result;
	unsigned long cl_idx;
	ra_info_t ra_info;
	file_plugin * fplug;
	item_plugin * iplug;
	static int cnt = 0;

	cnt ++;

	assert("edward-225", read || write);
	assert("edward-226", schedulable());
	assert("edward-137", inode != NULL);
	assert("edward-138", clust != NULL);
	assert("edward-461", ergo(read, clust->buf != NULL));
	assert("edward-462", ergo(!read, !cluster_is_uptodate(clust)));
	assert("edward-474", get_current_context()->grabbed_blocks == 0);
	
	cl_idx = clust->index;
	fplug = inode_file_plugin(inode);
	iplug = item_plugin_by_id(CTAIL_ID);
	/* build flow for the cluster */
	fplug->flow_by_inode(inode, clust->buf, 0 /* kernel space */,
			     inode_scaled_cluster_size(inode), clust_to_off(cl_idx, inode), READ_OP, &f);
	result = load_file_hint(clust->file, &hint, &lh);
	if (result)
		return result;
	if (write) {
		result = reiser4_grab_space_force(estimate_disk_cluster(inode), 0);
		if (result)
			goto out2;
	}
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));
	
	while (f.length) {
		result = find_cluster_item(&hint, &f.key, (write ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK), &ra_info, FIND_EXACT);
		switch (result) {
		case CBK_COORD_NOTFOUND:
			if (inode_scaled_offset(inode, clust_to_off(cl_idx, inode)) == get_key_offset(&f.key)) {
				/* first item not found */
				if (read)
					/* hole cluster */
					clust->stat = FAKE_CLUSTER;
				result = 0;
				goto out2;
			}
			/* we are outside the cluster, stop search here */
			assert("edward-146", f.length != inode_scaled_cluster_size(inode));
			done_lh(&lh);
			goto ok;
		case CBK_COORD_FOUND:
			assert("edward-148", hint.coord.base_coord.between == AT_UNIT);
			assert("edward-460", hint.coord.base_coord.unit_pos == 0);
			
			coord_clear_iplug(&hint.coord.base_coord);
			result = zload_ra(hint.coord.base_coord.node, &ra_info);
			if (unlikely(result))
				goto out2;
			assert("edward-147", item_plugin_by_coord(&hint.coord.base_coord) == iplug);
			if (read) {
				result = iplug->s.file.read(NULL, &f, &hint);
				if(result)
					goto out;
			}
			if (write) {
				znode_make_dirty(hint.coord.base_coord.node);
				znode_set_squeezable(hint.coord.base_coord.node);
				if (!read)
					move_flow_forward(&f, iplug->b.nr_units(&hint.coord.base_coord));
			}
			zrelse(hint.coord.base_coord.node);
			done_lh(&lh);
			break;
		default:
			goto out2;
		}
	}
 ok:
	/* at least one item was found  */
	/* NOTE-EDWARD:
	   Callers should handle the case when disk cluster is incomplete (-EIO) */
	clust->len = inode_scaled_cluster_size(inode) - f.length;
	save_file_hint(clust->file, &hint);
	all_grabbed2free();
	return 0;
 out:
	zrelse(hint.coord.base_coord.node);
 out2:
	done_lh(&lh);
	save_file_hint(clust->file, &hint);
	all_grabbed2free();
	return result;
}

/* Read before write.
   We don't take an interest in how much bytes was written when error occures */
static int
read_some_cluster_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	unsigned to_read;
	item_plugin * iplug;
	
	iplug = item_plugin_by_id(CTAIL_ID);

	if (clust_to_off(clust->index, inode) >= inode->i_size)
		/* new cluster, nothing to read */
		return 0;
	/* bytes we wanna read starting from the beginning of cluster
	   to keep first @off ones */
	to_read = clust->off + clust->count + clust->delta;
	
	assert("edward-298", to_read <= inode_cluster_size(inode));
	
	for (i = 0; i < clust->nr_pages; i++) {
		struct page * pg = clust->pages[i];
		
		if (clust->off <= pg_to_off(i) && pg_to_off(i) <= to_read - 1)
			/* page will be completely overwritten */
			continue;
		lock_page(pg);
		if (PageUptodate(pg)) {
			unlock_page(pg);
			continue;
		}
		unlock_page(pg);

		if (!cluster_is_uptodate(clust)) {
			/* read cluster and mark its znodes dirty */
			result = ctail_read_cluster(clust, inode, 1 /* write */);
			if (result)
				goto out;
		}
		lock_page(pg);
		result =  do_readpage_ctail(clust, pg);
		unlock_page(pg);
		if (result) {
			impossible("edward-219", "do_readpage_ctail returned crap");
			goto out;
		}
	}
	if (!cluster_is_uptodate(clust))
		/* disk cluster unclaimed, make its znodes dirty */
		find_cluster(clust, inode, 0 /* do not read */, 1 /*write */);
 out:
	release_cluster_buf(clust, inode);
	return result;
}

/* Prepare before write. Called by write, writepage, truncate, etc..
   . grab cluster pages,
   . maybe read pages from disk,
   . maybe write hole
*/
static int
prepare_cluster(struct inode *inode,
		loff_t file_off /* write position in the file */,
		loff_t to_file, /* bytes of users data to write to the file */
		int * nr_pages, /* advised number of pages */
		reiser4_cluster_t *clust,
		const char * msg)

{
	char *data;
	int result = 0;
	unsigned o_c_d;

	assert("edward-177", inode != NULL);
	assert("edward-280", cluster_invariant(clust, inode));

	o_c_d = clust->count + clust->delta;
	
	if (nr_pages != NULL) {
		assert("edward-422", *nr_pages <= inode_cluster_pages(inode));
		clust->nr_pages = *nr_pages;
	}
	else
		/* wasn't advised, guess by frame */
		set_nrpages_by_frame(clust);
	if(!clust->nr_pages)
		/* do nothing */
		return 0;
	/* collect unlocked pages and jnodes */
	result = grab_cache_cluster(inode, clust);
	if (result)
		return result;
	if (clust->off == 0 && inode->i_size <= clust_to_off(clust->index, inode) + o_c_d) {
		/* we don't need to read cluster from disk, just
		   align the current chunk of data up to nr_pages */
		unsigned off = off_to_pgcount(o_c_d, clust->nr_pages - 1);
		struct page * pg = clust->pages[clust->nr_pages - 1];
		crypto_plugin * cplug = inode_crypto_plugin(inode);
		
		assert("edward-285", pg != NULL);
		
		lock_page(pg);
		data = kmap_atomic(pg, KM_USER0);
		if (cplug)
			cplug->align_cluster(data + off, off, PAGE_CACHE_SIZE);
		else
			xmemset(data + off, 0, PAGE_CACHE_SIZE - off);
		kunmap_atomic(data, KM_USER0);
		unlock_page(pg);
	}
	result = reserve4cluster(inode, clust, msg);
	if (result) 
		goto exit1;
	result = read_some_cluster_pages(inode, clust);
	if (result)
		goto exit2;
	if (clust->stat == HOLE_CLUSTER)
		result = write_hole(inode, clust, file_off, to_file);
	if (!result)
		return 0;
 exit2:
	free_reserved4cluster(inode, clust);
 exit1:
	put_cluster_jnodes(clust);	
	return result;
}

/* get cluster handle params by two offsets */
static void
clust_by_offs(reiser4_cluster_t * clust, struct inode * inode, loff_t o1, loff_t o2)
{
	assert("edward-295", clust != NULL);
	assert("edward-296", inode != NULL);
	assert("edward-297", o1 <= o2);

	clust->index = off_to_clust(o1, inode);
	clust->off = off_to_cloff(o1, inode);
	clust->count = min_count(inode_cluster_size(inode) - clust->off, o2 - o1);
	clust->delta = 0;
}

static void
set_cluster_params(struct inode * inode, reiser4_cluster_t * clust, flow_t * f, loff_t file_off)
{
	assert("edward-197", clust != NULL);
	assert("edward-286", clust->pages != NULL);
	assert("edward-198", inode != NULL);

	xmemset(clust->pages, 0, sizeof(clust->pages) << inode_cluster_shift(inode));
	
	if (file_off > inode->i_size) {
		/* Uhmm, hole in crypto-file... */
		loff_t hole_size;
		hole_size = file_off - inode->i_size;

		printk("edward-176, Warning: Hole of size %llu in "
		       "cryptocompressed file (inode %llu, offset %llu) \n",
		       hole_size, get_inode_oid(inode), file_off);
		
		clust_by_offs(clust, inode, inode->i_size, file_off);
		clust->stat = HOLE_CLUSTER;
		if (clust->off + hole_size < inode_cluster_size(inode))
			/* besides there is also user's data to write to this cluster */
			clust->delta = min_count(inode_cluster_size(inode) - (clust->off + clust->count), f->length);
		return;
	}
	clust_by_offs(clust, inode, file_off, file_off + f->length);
	clust->stat = DATA_CLUSTER;
}

/* Main write procedure for cryptcompress objects,
   this slices user's data into clusters and copies to page cache.
   If @buf != NULL, returns number of bytes in successfully written clusters,
   otherwise returns error */
/* FIXME_EDWARD replace flow by something lightweigth */

static loff_t
write_cryptcompress_flow(struct file * file , struct inode * inode, const char *buf, size_t count, loff_t pos)
{
	int i;
	flow_t f;
	int result = 0;
	size_t to_write = 0;
	loff_t file_off;
	reiser4_cluster_t clust;
	struct page ** pages;
	static int cnt = 0;

	cnt++; /* FIXME-EDWARD: Remove me */
	
	assert("edward-159", current_blocksize == PAGE_CACHE_SIZE);

	pages = reiser4_kmalloc(sizeof(*pages) << inode_cluster_shift(inode), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	result = flow_by_inode_cryptcompress(inode, (char *)buf, 1 /* user space */, count, pos, WRITE_OP, &f);
	if (result)
		goto exit;
	to_write = f.length;

        /* current write position in file */
	file_off = pos;
	reiser4_cluster_init(&clust);
	clust.file = file;
	clust.pages = pages;
		
	set_cluster_params(inode, &clust, &f, file_off);
	
	if (next_cluster_stat(&clust) == HOLE_CLUSTER) {
		result = prepare_cluster(inode, file_off, f.length, NULL, &clust, "write cryptcompress hole");
		if (result)
			goto exit;
	}
	do {
		unsigned page_off, page_count;
		
		result = prepare_cluster(inode, file_off, f.length, NULL, &clust, "write cryptcompress flow");  /* jp+ */
		if (result)
			goto exit;
		assert("edward-204", clust.stat == DATA_CLUSTER);
		assert("edward-161", schedulable());
		
		/* set write position in page */
		page_off = off_to_pgoff(clust.off);
		
                /* copy user's data to cluster pages */
		for (i = off_to_pg(clust.off); i < count_to_nrpages(clust.off + clust.count); i++) {
			page_count = min_count(PAGE_CACHE_SIZE - page_off, clust.count);

			assert("edward-287", pages[i] != NULL);
			
			lock_page(pages[i]);
			result = __copy_from_user((char *)kmap(pages[i]) + page_off, f.data + (i << PAGE_CACHE_SHIFT) , page_count);
			kunmap(pages[i]);
			if (unlikely(result)) {
				unlock_page(pages[i]);
				result = -EFAULT;
				release_cluster(&clust);                            /* jp- */
				goto exit1;
			}
			unlock_page(pages[i]);
			page_off = 0;
		}
		
		set_cluster_pages_dirty(&clust, NULL);                              /* p- */
		
		result = try_capture_cluster(&clust, NULL);
		if (result)
			goto exit2;
		
		make_cluster_jnodes_dirty(&clust, NULL);                              /* j- */
		
		result = update_inode_cryptcompress(inode,
						    clust_to_off(clust.index, inode) + clust.off + clust.count /* new_size */,
						    (clust_to_off(clust.index, inode) + clust.off + clust.count > inode->i_size) ? 1 : 0,
						    1,
						    1/* update stat data */);
		if (result) 
			goto exit1;
		balance_dirty_pages_ratelimited(inode->i_mapping);
		
		move_flow_forward(&f, clust.count);
		update_cluster(inode, &clust, 0, f.length);
		continue;
	exit2:
		put_cluster_jnodes(&clust);                                         /* j- */
	exit1:
		free_reserved4cluster(inode, &clust);
		break;
	} while (f.length);
	
 exit:
	if (result == -EEXIST)
		printk("write returns EEXIST!\n");

	reiser4_kfree(pages, sizeof(*pages) << inode_cluster_shift(inode));

	if (buf) {
		/* if nothing were written - there must be an error */
		assert("edward-195", ergo((to_write == f.length), result < 0));
		return (to_write - f.length) ? (to_write - f.length) : result;
	}
	return result;
}

static ssize_t
write_file(struct file * file, /* file to write to */
	   struct inode *inode, /* inode */
	   const char *buf, /* address of user-space buffer */
	   size_t count, /* number of bytes to write */
	   loff_t * off /* position to write which */)
{
	
	int result;
	loff_t pos;
	ssize_t written;

	assert("edward-196", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	result = generic_write_checks(inode, file, off, &count, 0);
	if (unlikely(result != 0))
		return result;
	
	if (unlikely(count == 0))
		return 0;

        /* FIXME-EDWARD: other UNIX features */

	pos = *off;
	written = write_cryptcompress_flow(file, inode, (char *)buf, count, pos);
	if (written < 0) {
		if (written == -EEXIST)
			printk("write_file returns EEXIST!\n");
		return written;
	}

        /* update position in a file */
	*off = pos + written;
	/* return number of written bytes */
	return written;
}

/* plugin->u.file.write */
ssize_t
write_cryptcompress(struct file * file, /* file to write to */
		    const char *buf, /* address of user-space buffer */
		    size_t count, /* number of bytes to write */
		    loff_t * off /* position to write which */)
{
	ssize_t result;
	struct inode *inode;
	
	inode = file->f_dentry->d_inode;
	
	down(&inode->i_sem);
	
	result = write_file(file, inode, buf, count, off);

	up(&inode->i_sem);
	return result;
}

/* Helper function for cryptcompress_truncate.
   If this returns 0, then @idx is minimal cluster
   index that isn't contained in this file */
static int
find_file_idx(struct inode *inode, unsigned long * idx)
{
	int result;
	reiser4_key key;
	hint_t hint;
	coord_t *coord;
	lock_handle lh;
	item_plugin *iplug;
	
	assert("edward-276", inode_file_plugin(inode)->key_by_inode == key_by_inode_cryptcompress);
	key_by_inode_cryptcompress(inode, get_key_offset(max_key()), &key);

	hint_init_zero(&hint, &lh);
	/* find the last item of this file */
	result = find_cluster_item(&hint, &key, ZNODE_READ_LOCK, 0/* ra_info */, FIND_MAX_NOT_MORE_THAN);
	if (result == CBK_COORD_NOTFOUND) {
		/* there are no items of this file */
		done_lh(&lh);
		*idx = 0;
		return 0;
	}
	if (result != CBK_COORD_FOUND) {
		/* error occured */
		done_lh(&lh);
		return result;
	}
	coord = &hint.coord.base_coord;
	
	/* there are items of this file (at least one) */
	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (unlikely(result)) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(coord);
	assert("edward-277", iplug == item_plugin_by_id(CTAIL_ID));

	append_cluster_key_ctail(coord, &key);
	
	*idx = off_to_clust(get_key_offset(&key), inode);

	zrelse(coord->node);
	done_lh(&lh);

	return 0;
}

static int
cut_items_cryptcompress(struct inode *inode, loff_t new_size, int update_sd)
{
	reiser4_key from_key, to_key;
	reiser4_key smallest_removed;
	int result = 0;
	
	assert("edward-293", inode_file_plugin(inode)->key_by_inode == key_by_inode_cryptcompress);
	key_by_inode_cryptcompress(inode, off_to_clust_to_off(new_size, inode), &from_key);
	to_key = from_key;
	set_key_offset(&to_key, get_key_offset(max_key()));
	
	while (1) {
		result = reserve_cut_iteration(tree_by_inode(inode), __FUNCTION__);
		if (result)
			break;
		
		result = cut_tree_object(current_tree, &from_key, &to_key,
					 &smallest_removed, inode);
		if (result == -E_REPEAT) {
			/* -E_REPEAT is a signal to interrupt a long file truncation process */
			/* FIXME(Zam) cut_tree does not support that signaling.*/
			result = update_inode_cryptcompress
				(inode, get_key_offset(&smallest_removed), 1, 1, update_sd);
			if (result)
				break;

			all_grabbed2free();
			reiser4_release_reserved(inode->i_sb);

			{
				reiser4_context * ctx;
				long long_ret;

				ctx = get_current_context();
				long_ret = txn_end(ctx);
				txn_begin(ctx);
				if (long_ret < 0) {
					result = (int)long_ret;
					break;
				}
			}		
			continue;
		}
		if (result)
			break;
		result = update_inode_cryptcompress
			(inode, get_key_offset(&smallest_removed), 1, 1, update_sd);
		break;
	}

	all_grabbed2free();
	reiser4_release_reserved(inode->i_sb);
	return result;
}

/* The following two procedures are called when truncate decided
   to deal with real items */
static int
cryptcompress_append_hole(struct inode * inode, loff_t new_size)
{
	return write_cryptcompress_flow(0, inode, 0, 0, new_size);
}

/* safe taking down pages */
void truncate_pages_cryptcompress(struct address_space * mapping, unsigned long index)
{
	truncate_inode_pages(mapping, pg_to_off(index));
}

static int
shorten_cryptcompress(struct inode * inode, loff_t new_size, int update_sd)
{
	int result;
	int nrpages;
	struct page ** pages;
	loff_t old_size;
	char * kaddr;
	pgoff_t pg_padd;
	reiser4_cluster_t clust;
	crypto_plugin * cplug;
	
	assert("edward-290", inode->i_size > new_size);
	
	old_size = inode->i_size;
	cplug = inode_crypto_plugin(inode);
	result = cut_items_cryptcompress(inode, new_size, update_sd);
	if(result)
		return result;
	if (!off_to_cloff(new_size, inode))
		/* truncated to cluster boundary */
		return 0;
	/* FIXME-EDWARD: reserve partial page */
	pages = reiser4_kmalloc(sizeof(*pages) << inode_cluster_shift(inode), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	reiser4_cluster_init(&clust);
	clust.pages = pages;
	clust_by_offs(&clust, inode, new_size, old_size);

	/* read the whole cluster */
	result = prepare_cluster(inode, 0, 0, NULL, &clust, "shorten cryptcompress");
	if (result)
		goto exit2;
	/* truncate cluster, so flush will deal with new number of pages */

	assert("edward-294", clust.stat == DATA_CLUSTER);

	pg_padd = PAGE_CACHE_SIZE - off_to_pgoff(clust.off);

	/* release last truncated pages */
	release_cluster_pages(&clust, off_to_pg(clust.off) + 1 /* ?? */);
	
	truncate_pages_cryptcompress(inode->i_mapping, off_to_pg(clust_to_off(clust.index, inode) + clust.off + pg_padd));

	/* align last non-truncated page */
	lock_page(pages[off_to_pg(clust.off)]);
	kaddr = kmap_atomic(pages[off_to_pg(clust.off)], KM_USER0);
	
	if (cplug)
		cplug->align_cluster(kaddr + off_to_pgoff(clust.off), off_to_pgoff(clust.off), PAGE_CACHE_SIZE);
	else
		xmemset(kaddr + off_to_pgoff(clust.off), 0, PAGE_CACHE_SIZE - off_to_pgoff(clust.off));
	unlock_page(pages[off_to_pg(clust.off)]);
	
	nrpages = off_to_pg(clust.off) + 1;
	set_cluster_pages_dirty(&clust, &nrpages);
	result = try_capture_cluster(&clust, &nrpages);
	if(result)
		goto exit;
	make_cluster_jnodes_dirty(&clust, &nrpages);

	result = update_inode_cryptcompress(inode, new_size, 1, 1, update_sd);
	if(!result)
		goto exit2;	
 exit:
	free_reserved4cluster(inode, &clust);
 exit2:	
	put_cluster_jnodes(&clust);
	reiser4_kfree(pages, sizeof(*pages) << inode_cluster_shift(inode));
	return result;
}

/* This is called in setattr_cryptcompress when it is used to truncate,
   and in delete_cryptcompress */

static int
cryptcompress_truncate(struct inode *inode, /* old size */
		       loff_t new_size, /* new size */
		       int update_sd)
{
	int result;
	loff_t old_size = inode->i_size;
	unsigned long idx;
//	unsigned long old_idx = off_to_clust(old_size, inode);
	unsigned long new_idx = off_to_clust(new_size, inode);
	
	/* inode->i_size != new size */

	/* without decompression we can specify
	   real file offsets only up to cluster size */
	result = find_file_idx(inode, &idx);

//	assert("edward-278", idx <= old_idx);

	if (result)
		return result;
	if (idx <= new_idx) {
		/* do not deal with items */
		if (update_sd) {
			result = setattr_reserve(tree_by_inode(inode));
			if (!result)
				result = update_inode_cryptcompress(inode, new_size, 1, 1, 1);
			all_grabbed2free();	
		}
		return result;
	}
	result = (old_size < new_size ? cryptcompress_append_hole(inode, new_size) :
		  shorten_cryptcompress(inode, new_size, update_sd));
	return result;
}

/* plugin->u.file.truncate */
int
truncate_cryptcompress(struct inode *inode, loff_t new_size)
{
	return 0;
}

#if 0
static int
cryptcompress_writepage(struct page * page, reiser4_cluster_t * clust)
{
	int result = 0;
	int nrpages;
	struct inode * inode;
	
	assert("edward-423", page->mapping && page->mapping->host);

	inode = page->mapping->host;
	reiser4_cluster_init(&clust);

        /* read all cluster pages if necessary */
	clust.pages = reiser4_kmalloc(sizeof(*clust.pages) << inode_cluster_shift(inode), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	clust.index = pg_to_clust(page->index, inode);
	clust.off = pg_to_off_to_cloff(page->index, inode);
	clust.count = PAGE_CACHE_SIZE;
	nrpages = count_to_nrpages(fsize_to_count(&clust, inode));
	
	result = prepare_cluster(page->mapping->host, 0, 0, &nrpages, &clust, "cryptcompress_writepage");  /* jp+ */
	if(result)
		goto exit;
	
	set_cluster_pages_dirty(&clust, NULL);                                  /* p- */
	result = try_capture_cluster(&clust, NULL);
	if (result) {
		free_reserved4cluster(inode, &clust);
		put_cluster_jnodes(&clust);                                     /* j- */
		goto exit;
	}
	lock_page(page);
	make_cluster_jnodes_dirty(&clust, NULL);
	put_cluster_jnodes(&clust);                                             /* j- */
 exit:
	reiser4_kfree(clust.pages, sizeof(*clust.pages) << inode_cluster_shift(inode));
	return result;	
}

/* make sure for each page the whole cluster was captured */
static int
writepages_cryptcompress(struct address_space * mapping)
{
	struct list_head *mpages;
	int result;
	int nr;
	int nrpages;
	int captured = 0, clean = 0, writeback = 0;
	reiser4_cluster_t * clust;

	reiser4_cluster_init(clust);
	result = 0;
	nr = 0;

	spin_lock (&mapping->page_lock);

	mpages = get_moved_pages(mapping);
	while ((result == 0 || result == 1) && !list_empty (mpages) && nr < CAPTURE_APAGE_BURST) {
		struct page *pg = list_to_page(mpages);

		assert("edward-481", PageDirty(pg));

		if (!clust->nr_pages || !page_of_cluster(pg, &clust, inode)) {
			/* update cluster handle */
			clust.index = pg_to_clust(pg->index, inode);
			clust.off = pg_to_off_to_cloff(pg->index, inode);
			clust.count = PAGE_CACHE_SIZE;
			/* advice number of pages */ 
			nrpages = count_to_nrpages(fsize_to_count(&clust, inode));	
			
			result = prepare_cluster(mapping->host, 0, 0, &nrpages, &clust, 
		}
		result = capture_anonymous_page(pg, 0);
		if (result == 1) {
			++ nr;
			result = 0;
		}
	}
	spin_unlock(&mapping->page_lock);

	if (result) {
		warning("vs-1454", "Cannot capture anon pages: %i (%d %d %d)\n", result, captured, clean, writeback);
		return result;
	}


	if (nr >= CAPTURE_APAGE_BURST)
		redirty_inode(mapping->host);

	if (result == 0)
		result = capture_anonymous_jnodes(mapping->host);
	
	if (result != 0)
		warning("nikita-3328", "Cannot capture anon pages: %i\n", result);
	return result;
}

#endif
	
/* plugin->u.file.capture
   FIXME: capture method of file plugin is called by reiser4_writepages. It has to capture all
   anonymous pages and jnodes of the mapping. See capture_unix_file, for example
 */
int
capture_cryptcompress(struct inode *inode, struct writeback_control *wbc)
{

#if 0
	int result;
	struct inode *inode;
	
	assert("edward-424", PageLocked(page));
	assert("edward-425", PageUptodate(page));
	assert("edward-426", page->mapping && page->mapping->host);

	inode = page->mapping->host;
	assert("edward-427", pg_to_off(page->index) < inode->i_size);
	
	unlock_page(page);
	if (pg_to_off(page->index) >= inode->i_size) {
		/* race with truncate? */
		lock_page(page);
		page_cache_release(page);
		return RETERR(-EIO);
	}
	/* FIXME-EDWARD: Estimate insertion */
	result = cryptcompress_writepage(page);
	assert("edward-428", PageLocked(page));
	return result;

	int result;
	reiser4_context ctx;

	if (!inode_has_anonymous_pages(inode))
		return 0;
	
	init_context(&ctx, inode->i_sb);
	
	ctx.nobalance = 1;
	assert("edward-482", lock_stack_isclean(get_current_lock_stack()));
	
	result = 0;

	do {
		result = writepages_cryptcompress(inode->i_mapping);
		if (result != 0 || wbc->sync_mode != WB_SYNC_ALL)
			break;
		result = txnmgr_force_commit_all(inode->i_sb, 0);
	} while (result == 0 && inode_has_anonymous_pages(inode));
	
	reiser4_exit_context(&ctx);
	return result;
#endif
	return 0;
}

static inline void
validate_extended_coord(uf_coord_t *uf_coord, loff_t offset)
{
	assert("edward-418", uf_coord->valid == 0);
	assert("edward-419", item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension);
	
	/* FIXME: */
	item_body_by_coord(&uf_coord->base_coord);
	item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension(uf_coord, offset);
}

/* plugin->u.file.mmap:
   generic_file_mmap */

/* plugin->u.file.release */
/* plugin->u.file.get_block */
int
get_block_cryptcompress(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create UNUSED_ARG)
{	
	if (current_blocksize != inode_cluster_size(inode))
		return RETERR(-EINVAL);
	else {
		int result;
		reiser4_key key;
		hint_t hint;
		lock_handle lh;
		item_plugin *iplug;

		assert("edward-420", create == 0);
		key_by_inode_cryptcompress(inode, (loff_t)block * current_blocksize, &key);
		hint_init_zero(&hint, &lh);
		result = find_cluster_item(&hint, &key, ZNODE_READ_LOCK, 0, FIND_EXACT);
		if (result != CBK_COORD_FOUND) {
			done_lh(&lh);
			return result;
		}
		result = zload(hint.coord.base_coord.node);
		if (result) {
			done_lh(&lh);
			return result;
		}
		iplug = item_plugin_by_coord(&hint.coord.base_coord);
		
		assert("edward-421", iplug == item_plugin_by_id(CTAIL_ID));
		
		if (!hint.coord.valid)
			validate_extended_coord(&hint.coord,
						(loff_t) block << PAGE_CACHE_SHIFT);
		if (iplug->s.file.get_block)
			result = iplug->s.file.get_block(&hint.coord, block, bh_result);
		else
			result = RETERR(-EINVAL);
		
		zrelse(hint.coord.base_coord.node);
		done_lh(&lh);
		return result;
	}
}

/* plugin->u.file.delete */
int
delete_cryptcompress(struct inode *inode)
{
	int result;
	
	assert("edward-429", inode->i_nlink == 0);	
	
	if (inode->i_size) {
		result = cryptcompress_truncate(inode, 0, 0);
		if (result) {
			warning("edward-430", "cannot truncate cryptcompress file  %lli: %i",
				get_inode_oid(inode), result);
			return result;
		}
	}
	return delete_file_common(inode);
}

/* plugin->u.file.init_inode_data */
/* plugin->u.file.owns_item:
   owns_item_common */
/* plugin->u.file.pre_delete */
int
pre_delete_cryptcompress(struct inode *inode)
{
	return cryptcompress_truncate(inode, 0, 0);
}

/* plugin->u.file.setattr method */
int
setattr_cryptcompress(struct inode *inode,	/* Object to change attributes */
		      struct iattr *attr /* change description */ )
{
	int result;
	
	if (attr->ia_valid & ATTR_SIZE) {
		/* truncate does reservation itself and requires exclusive access obtained */
		if (inode->i_size != attr->ia_size) {
			loff_t old_size;
			
			inode_check_scale(inode, inode->i_size, attr->ia_size);
			
			old_size = inode->i_size;
			
			result = cryptcompress_truncate(inode, attr->ia_size, 1/* update stat data */);
			
			if (!result) {
				/* items are removed already. inode_setattr will call vmtruncate to invalidate truncated
				   pages and truncate_cryptcompress which will do nothing. FIXME: is this necessary? */
				INODE_SET_FIELD(inode, i_size, old_size);
				result = inode_setattr(inode, attr);
			}
		} else
			result = 0;
	} else {
		result = setattr_reserve(tree_by_inode(inode));
		if (!result) {
			result = inode_setattr(inode, attr);
			if (!result)
				/* "capture" inode */
				result = reiser4_mark_inode_dirty(inode);
			all_grabbed2free();
		}
	}
	return result;	
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 120
  scroll-step: 1
  End:
*/
