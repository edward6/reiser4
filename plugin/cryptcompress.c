/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* The object plugin of reiser4 crypto-compressed (crc-)files (see
   http://www.namesys.com/cryptcompress_design.txt for details). */

/* We store all the cryptcompress-specific attributes as following
   non-default plugins in plugin stat-data extension:
   1) file plugin
   2) crypto plugin
   3) digest plugin
   4) compression plugin
*/
   
/* PAGE CLUSTERING TEWRMINOLOGY:
   cluster size  : cluster size in bytes 
   cluster index : index of cluster's first page
   cluster number: offset in cluster size units 
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

int do_readpage_ctail(reiser4_cluster_t *, struct page * page);
int ctail_read_cluster (reiser4_cluster_t *, struct inode *, int);
reiser4_key * append_cluster_key_ctail(const coord_t *, reiser4_key *);
int setattr_reserve(reiser4_tree *);
int reserve_cut_iteration(reiser4_tree *, const char *);

/* get cryptcompress specific portion of inode */
inline cryptcompress_info_t *cryptcompress_inode_data(const struct inode * inode)
{
	return &reiser4_inode_data(inode)->file_plugin_data.cryptcompress_info;
}

/* plugin->create() method for crypto-compressed files 

. install plugins
. set bits in appropriate masks
. attach secret key
. attach crypto info 
. attach cluster info

FIXME-EDWARD: Cipher and hash key-id by the secret key
(open method requires armored identification of the key */

int create_cryptcompress(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
{
	int result;
	scint_t *extmask;
	reiser4_inode * info;
	cryptcompress_info_t * crc_info;
	cryptcompress_data_t * crc_data = data->crc;
	digest_plugin * dplug = digest_plugin_by_id(crc_data->dia);
	crypto_plugin * cplug;
	compression_plugin *coplug;
	crypto_stat_t stat; /* for crypto stat-data */
       	__u8 fip[dplug->digestsize]; /* for fingerprint */ 
	void * digest_ctx = NULL;
	
	assert("edward-23", object != NULL);
	assert("edward-24", parent != NULL);
	assert("edward-26", inode_get_flag(object, REISER4_NO_SD));
	assert("edward-27", data->id == CRC_FILE_PLUGIN_ID);
	
	info = reiser4_inode_data(object);
	crc_info = cryptcompress_inode_data(object);
	
	assert("edward-29", info != NULL);
	
	/* install plugins */
	assert("edward-30", info->pset->crypto == NULL);
	assert("edward-85", info->pset->digest == NULL);
	assert("edward-31", info->pset->compression == NULL);
	
	cplug = crypto_plugin_by_id(crc_data->cra);
	plugin_set_crypto(&info->pset, cplug);

	plugin_set_digest(&info->pset, dplug);

	coplug = compression_plugin_by_id(crc_data->coa);
	plugin_set_compression(&info->pset, coplug);

	/* set bits */
	info->plugin_mask |= (1 << REISER4_FILE_PLUGIN_TYPE) |
		(1 << REISER4_CRYPTO_PLUGIN_TYPE) |
		(1 << REISER4_DIGEST_PLUGIN_TYPE) |
		(1 << REISER4_COMPRESSION_PLUGIN_TYPE);
	extmask = &info->extmask;
	scint_pack(extmask, scint_unpack(extmask) |
		   (1 << PLUGIN_STAT) |
		   (1 << CLUSTER_STAT) |
		   (1 << CRYPTO_STAT), GFP_ATOMIC);

	/* attach secret key */
	assert("edward-84", crc_info->expkey == NULL);
	
	crc_info->expkey = reiser4_kmalloc((cplug->nr_keywords)*sizeof(__u32), GFP_KERNEL);
	if (!crc_info->expkey)
		return RETERR(-ENOMEM);
	result = cplug->set_key(crc_info->expkey, crc_data->key);
	if (result)
		goto destroy_key;
	assert ("edward-34", !inode_get_flag(object, REISER4_SECRET_KEY_INSTALLED));
	inode_set_flag(object, REISER4_SECRET_KEY_INSTALLED);

	/* attach crypto stat */
	assert("edward-87", info->crypt == NULL);
	assert("edward-88", crc_data->keyid != NULL);
	assert("edward-83", crc_data->keyid_size != 0);
	assert("edward-89", crc_data->keysize != 0);
	
	/* fingerprint creation of the pair (@key, @keyid) includes two steps: */
	/* 1. encrypt keyid by key: */
	/* FIXME-EDWARD: add encryption of keyid */

	/* 2. make digest of encrypted keyid */
	result = dplug->alloc(digest_ctx);
	if (result)
		goto destroy_key;
	dplug->init(digest_ctx);
	dplug->update(digest_ctx, crc_data->keyid, crc_data->keyid_size);
	dplug->final(digest_ctx, fip);
	dplug->free(digest_ctx);
	
	stat.keysize = crc_data->keysize;
	stat.keyid = fip;
	info->crypt = &stat;

	/* attach cluster info */
	info->cluster_shift = crc_data->cluster_shift;
	
	result = write_sd_by_inode_common(object);
	if (!result)
		return 0;
	/* FIXME-EDWARD remove me */
	if (info->crypt == &stat) 
		goto destroy_key;

	/* now the pointer was updated to kmalloced data, but save() method
	   for some another sd-extension failed, release attached crypto stat */
	
	assert("edward-32", !memcmp(info->crypt->keyid, stat.keyid, dplug->digestsize));

	reiser4_kfree(info->crypt->keyid, (size_t)dplug->digestsize);
	reiser4_kfree(info->crypt, sizeof(crypto_stat_t));
	inode_clr_flag(object, REISER4_CRYPTO_STAT_LOADED);
	inode_clr_flag(object, REISER4_CLUSTER_KNOWN);
	
 destroy_key:
	xmemset(crc_info->expkey, 0, (cplug->nr_keywords)*sizeof(__u32));
	reiser4_kfree(crc_info->expkey, (cplug->nr_keywords)*sizeof(__u32));
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}

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
static inline int inode_cluster_pages (struct inode * inode)
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
	
	stat = inode_crypto_stat(inode);
	
	assert("edward-98", stat != NULL);
	
	cplug = inode_crypto_plugin(inode);
	
	if (!cplug && src_off == get_key_offset(max_key()))
		return src_off;

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

/* plugin->key_by_inode() */
int
key_by_inode_cryptcompress(struct inode *inode, loff_t off, reiser4_key * key)
{
	assert("edward-64", inode != 0);
	assert("edward-112", !(off & ~(~0ULL << inode_cluster_shift(inode) << PAGE_CACHE_SHIFT)));
	/* don't come here with other offsets */
	
	build_sd_key(inode, key);
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) inode_scaled_offset(inode, off));
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
	assert("edward-149", inode != NULL);
	
	f->length = size;
	f->data = buf;
	f->user = user;
	f->op = op;
	assert("edward-150", inode_file_plugin(inode) != NULL);
	assert("edward-151", inode_file_plugin(inode)->key_by_inode == key_by_inode_cryptcompress);

	if (op == WRITE_OP && user == 1)
		return 0;
	return key_by_inode_cryptcompress(inode, off, &f->key);
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

	if (clust->buf)
		reiser4_kfree(clust->buf, clust->bufsize);
}

void put_cluster_data(reiser4_cluster_t * clust, struct inode * inode)
{
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

static inline unsigned long
off_to_clust_to_pg(loff_t off, struct inode * inode)
{
	return clust_to_pg(off_to_clust(off, inode), inode);
}

static inline unsigned
off_to_pgoff(loff_t off)
{
	return off & (PAGE_CACHE_SIZE - 1);
}

static inline unsigned
off_to_cloff(loff_t off, struct inode * inode)
{
	return off & ((loff_t)(inode_cluster_size(inode)) - 1);
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

/* set minimal number of cluster pages (start from first one)
   which cover hole and users data */
static void
set_cluster_nr_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	assert("edward-180", clust != NULL);
	
	if (clust->count + clust->delta == 0) {
		/* nothing to write - nothing to read */
		clust->nr_pages = 0;
		return;
	}
	clust->nr_pages = off_to_pg(clust->off + clust->count + clust->delta - 1) + 1;
}

static unsigned
file_to_clust_count(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-288", clust != NULL);
	assert("edward-289", inode != NULL);

	if (clust->index > off_to_clust(inode->i_size, inode))
		return 0;
	return min_count(inode_cluster_size(inode), inode->i_size - clust_to_off(clust->index, inode));   
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

void get_cluster_magic(__u8 * magic)
{
	/* FIXME-EDWARD: fill this by first 4 bytes of decrypted keyid
	   PARANOID? */
	assert("edward-279", magic != NULL);
	xmemset(magic, 0, CLUSTER_MAGIC_SIZE);
}

static int
need_decompression(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-142", clust != 0);
	assert("edward-143", inode != NULL);
	
	return (inode_compression_plugin(inode) &&
		clust->len < min_count(inode->i_size - clust_to_off(clust->index, inode), inode_cluster_size(inode)));
}

static void
alternate_buffers(reiser4_cluster_t * clust, __u8 * buff, size_t * size)
{
	__u8 * tmp_buf;
	size_t tmp_size;

	assert("edward-405", size != NULL);
	assert("edward-406", *size != 0);
	
	tmp_buf = buff;
	tmp_size = *size;
	buff = clust->buf;
	*size = clust->bufsize;
	clust->buf = tmp_buf;
	clust->bufsize = tmp_size;
}

/*
  . decrypt cluster was read from disk
  . check for cluster magic
  . decompress
*/
int inflate_cluster(reiser4_cluster_t *clust, /* contains data to process */
		    struct inode *inode)
{
	int result = 0;
	__u8 * buff = 0;
	size_t buff_size = 0;
	
	assert("edward-407", clust->buf != NULL);
	assert("edward-408", clust->bufsize != 0);
	
	if (inode_crypto_plugin(inode) != NULL) {
		/* decrypt */
		int i, nr_fips;
		__u32 * expkey;
		crypto_plugin * cplug = inode_crypto_plugin(inode);
		size_t cra_bsize = cplug->blocksize(inode_crypto_stat(inode)->keysize);
		
		assert("edward-154", clust->len <= inode_scaled_offset(inode, file_to_clust_count(clust, inode)));
		
		/* FIXME-EDWARD optimize size of kmalloced buffer */
		buff_size = inode_cluster_size(inode);
		buff = reiser4_kmalloc(buff_size, GFP_KERNEL);
		if (!buff) 
			return -ENOMEM;
		
		/* decrypt cluster with the simplest mode
		 * FIXME-EDWARD: call here stream mode plugin */		
		
		nr_fips = clust->len/cra_bsize;
		expkey = cryptcompress_inode_data(inode)->expkey;
		
		assert("edward-141", expkey != NULL);
		
		for (i=0; i < nr_fips; i++)
			cplug->decrypt(expkey, buff + i*cra_bsize, /* dst */ clust->buf + i*cra_bsize /* src */);
	}
	if (need_decompression(clust, inode)) {
		__u8 * wbuf;
		int tail_size;
		__u8 magic[CLUSTER_MAGIC_SIZE];
		compression_plugin * cplug = inode_compression_plugin(inode);
		
		if (buff == NULL) {
			/* no encryption */
			buff_size = inode_cluster_size(inode);
			buff = reiser4_kmalloc(buff_size, GFP_KERNEL);
			if (buff == NULL)
				return -ENOMEM;
			alternate_buffers(clust, buff, &buff_size);
		}
		/* check for end-of-cluster signature, this allows to hold read
		   errors when second or other next items of the cluster are missed 
		   
		   end-of-cluster format created before encryption:
		   
		   data
		   cluster_magic  (4)   indicates presence of compression
		                        infrastructure, should be private.
		   aligning_tail        created by ->align() method of crypto-plugin,
		                        we don't align non-compressed clusters 
		   
		   Aligning tail format:			
		   
		   data 				
		   tail_size      (1)   size of aligning tail,
		                        1 <= tail_size <= blksize  
		*/
		get_cluster_magic(magic);
		tail_size = *(buff + (clust->len - 1));
		if (memcmp(buff + clust->len - (size_t)CLUSTER_MAGIC_SIZE - tail_size,
			   magic, (size_t)CLUSTER_MAGIC_SIZE)) {
			printk("edward-156: inflate_cluster: wrong end-of-cluster magic\n");
			result = -EIO;
			goto exit;
		}
		/* decompress cluster */
		wbuf = reiser4_kmalloc(cplug->mem_req, GFP_KERNEL);
		if (wbuf == NULL) {
			result = -ENOMEM;
			goto exit;
		}
		cplug->decompress(wbuf, buff, clust->len, clust->buf, &clust->len);
		/* check the length of decompressed data */
		assert("edward-157", clust->len == file_to_clust_count(clust, inode));
	}
	return 0;
 exit:
	if (buff != NULL)
		reiser4_kfree(buff, buff_size);
	return result;	
}
	
/* the two following functions reflect our policy of compression quality */
static inline int
try_compress(reiser4_cluster_t * clust, struct inode * inode)
{
	return (inode_compression_plugin(inode) && clust->count >= MIN_SIZE_FOR_COMPRESSION);
}

static inline int
save_compressed(reiser4_cluster_t * clust, struct inode * inode)
{
	return (clust->len +
		inode_crypto_plugin(inode)->blocksize(inode_crypto_stat(inode)->keysize) +
		CLUSTER_MAGIC_SIZE < clust->count);
}

/*
  . maybe compress cluster
  . maybe encrypt the result
  FIXME-EDWARD: the only symmetric algorithms with ecb are supported for a while */
int
deflate_cluster(reiser4_cluster_t *clust, /* contains data to process */
		struct inode *inode)
{
	int result = 0;
	__u8 * buff = NULL;
	size_t buff_size = 0;
	
	assert("edward-401", clust->buf != 0);
	assert("edward-401", clust->bufsize != 0);
	
	if (try_compress(clust, inode)) {
		/* try to compress, discard bad results */
		__u8 * wbuf;
		compression_plugin * cplug = inode_compression_plugin(inode);

		buff_size = inode_scaled_cluster_size(inode);
		buff = reiser4_kmalloc(buff_size, GFP_KERNEL);
		if (buff == NULL)
			return -ENOMEM;
		wbuf = reiser4_kmalloc(cplug->mem_req, GFP_KERNEL);
		if (wbuf == NULL) {
			result = -ENOMEM;
			goto exit;
		}
		cplug->compress(wbuf, clust->buf, clust->count, buff /* result */, &clust->len /* result */);

		if (save_compressed(clust, inode)) {
			/* Accepted, attach magic */
			get_cluster_magic(buff + clust->len);
			goto next;
		}
		/* Discard */
		reiser4_kfree(wbuf, cplug->mem_req);
	}
	clust->len = clust->count;
 next:
	if (inode_crypto_plugin(inode) != NULL) {
		/* align and encrypt */
		int i;
		int tail_size;
		int nr_fips;
		__u32 * expkey;
		crypto_plugin * cplug = inode_crypto_plugin(inode);
		size_t cra_bsize = cplug->blocksize(inode_crypto_stat(inode)->keysize);
		
		if (buff == NULL || clust->count == clust->len) {
			/* encryption is present, compression is absent */
			if (buff == NULL) {
				buff_size = inode_scaled_cluster_size(inode);
				buff = reiser4_kmalloc(buff_size, GFP_KERNEL);
				if (buff == NULL)
					return -ENOMEM;
			}
			alternate_buffers(clust, buff, &buff_size);
		}
		tail_size = cplug->align_cluster(buff + clust->len, clust->len, cra_bsize);
		clust->len += tail_size;
		
		assert("edward-402", clust->len <= inode_cluster_size(inode));

		if (clust->len % cra_bsize != 0)
			assert("edward-403", 0);
		
		*(buff + clust->len - 1) = tail_size;
		nr_fips = clust->len/cra_bsize;
		expkey = cryptcompress_inode_data(inode)->expkey;
		
		assert("edward-404", expkey != NULL);
		
		for (i=0; i < nr_fips; i++)
			cplug->encrypt(expkey, clust->buf /* dst */ + i*cra_bsize, buff /* src */ + i*cra_bsize);
	}
	else if (buff != NULL && clust->len != clust->count)
		/* encryption is absent, compression is present */
		alternate_buffers(clust, buff, &buff_size);
	/* encryption is absent, compression is absent */
 exit:
	if (buff != NULL) 
		reiser4_kfree(buff, buff_size);
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
	/* if page has jnode - that jnode is mapped */
	assert("edward-65", ergo(result == 0 && PagePrivate(page), 
				 jnode_mapped(jprivate(page))));
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
		
		reiser4_lock_page(pg);
		
		set_page_dirty_internal(pg);
		SetPageUptodate(pg);
		if (!PageReferenced(pg))
			SetPageReferenced(pg);	
		
		reiser4_unlock_page(pg);
		
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
		
		result = try_capture(node, ZNODE_WRITE_LOCK, 0/* not non-blocking */);
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
	assert("edward-184", 0 < clust->nr_pages && clust->nr_pages <= ( 1 << inode_cluster_shift(inode)));
	
	for (i = 0; i < clust->nr_pages; i++) {
		clust->pages[i] = grab_cache_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		if (!(clust->pages[i])) {
			result = RETERR(-ENOMEM);
			break;
		}
		node = jnode_of_page(clust->pages[i]);
		reiser4_unlock_page(clust->pages[i]);
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
release_cache_cluster(reiser4_cluster_t * clust)
{
	int i;

	assert("edward-223", clust != NULL);
	
	for (i=0; i < clust->nr_pages; i++) {
		
		assert("edward-208", clust->pages[i] != NULL);
		assert("edward-224", jprivate(clust->pages[i]) != NULL);
		
		jput(jprivate(clust->pages[i]));
		page_cache_release(clust->pages[i]);
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
	assert ("edward-281", cluster_invariant(clust, inode));
	
	switch (clust->stat) {
	case DATA_CLUSTER:
		/* increment */
		clust->stat = DATA_CLUSTER;
		clust->off = 0;
		clust->index++;
		clust->count = min_count(inode_cluster_size(inode), to_file);
		xmemset(*clust->pages, 0, sizeof(clust->pages) << inode_cluster_shift(inode));
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
			xmemset(*clust->pages, 0, sizeof(clust->pages) << inode_cluster_shift(inode));
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
		
/* stick pages into united flow, then release the ones */
int
flush_cluster_pages(reiser4_cluster_t * clust, struct inode * inode)
{
	int i;
	struct page * page;
		
	assert("edward-236", inode != NULL);
	assert("edward-237", clust != NULL);
	assert("edward-238", clust->off == 0);
	assert("edward-239", clust->count == 0);	
	assert("edward-240", clust->delta == 0);
	assert("edward-241", schedulable());

	clust->count = file_to_clust_count(clust, inode);
	set_cluster_nr_pages(inode, clust);
	
	clust->buf = reiser4_kmalloc(inode_scaled_cluster_size(inode), GFP_KERNEL);
	if (!clust->buf) 
		return -ENOMEM;
	for(i=0; i < clust->nr_pages; i++){
		char * data;
		page = find_get_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);

		assert("edward-242", page != NULL);
		assert("edward-243", PageDirty(page));
		/* FIXME_EDWARD: Make sure that jnodes are from the same dirty list */ 
		
		reiser4_lock_page(page);
		data = kmap(page);
		memcpy(clust->buf + (i << PAGE_CACHE_SHIFT), data, PAGE_CACHE_SIZE);
		kunmap(page);
		uncapture_page(page);
		reiser4_unlock_page(page);
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
		reiser4_lock_page(page);
		data = kmap_atomic(page, KM_USER0);
		memset(data + pg_off, 0, to_pg);
		kunmap_atomic(data, KM_USER0);
		reiser4_unlock_page(page);
		
		cl_off += to_pg;
		cl_count -= to_pg;
		pg_off = 0;
	}
	if (!clust->delta) {
		/* only zeroes, try to flush */
		
		set_cluster_pages_dirty(clust, NULL);
		result = try_capture_cluster(clust, NULL);
		if (result) {
			return result;
		}
		make_cluster_jnodes_dirty(clust, NULL);
		result = update_inode_and_sd_if_necessary(inode, clust_to_off(clust->index, inode) + clust->off + clust->count, 1, 1, 1);
		if (result)
			return result;
		balance_dirty_pages_ratelimited(inode->i_mapping);
	}
 update:
	update_cluster(inode, clust, file_off, to_file);
	return 0;
}

/* find all cluster's items, read and(or) write each one */ 
int find_cluster(reiser4_cluster_t * clust,
		 struct inode * inode,
		 int read /* read items */,
		 int write /* write items */)
{
	flow_t f;
	lock_handle lh;
	hint_t hint;
	int result;
	unsigned long cl_idx;
	ra_info_t ra_info;
	file_plugin * fplug;
	item_plugin * iplug;

	assert("edward-225", read || write);
	assert("edward-226", schedulable());
	assert("edward-137", inode != NULL);
	assert("edward-138", clust != NULL);
	
	cl_idx = clust->index;
	fplug = inode_file_plugin(inode);
	iplug = item_plugin_by_id(CTAIL_ID);
	/* build flow for the cluster */
	fplug->flow_by_inode(inode, clust->buf, 0 /* kernel space */,
			     inode_scaled_cluster_size(inode), clust_to_off(cl_idx, inode), READ_OP, &f);
	result = load_file_hint(clust->file, &hint, &lh);
	if (result)
		return result;
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));
	
	while (f.length) {
		result = find_cluster_item(&hint, &f.key, (write ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK), &ra_info, FIND_EXACT);
		switch (result) {
		case CBK_COORD_NOTFOUND:
			if (inode_scaled_offset(inode, clust_to_off(cl_idx, inode)) == get_key_offset(&f.key)) {
				/* first item not found: hole cluster */
				clust->stat = FAKE_CLUSTER;
				result = 0;
				goto out2;
			}
			/* we are outside the cluster, stop search here */
			assert("edward-146", f.length != inode_scaled_cluster_size(inode));
			result = 0;
			goto ok;
		case CBK_COORD_FOUND:
			assert("edward-147", item_plugin_by_coord(&hint.coord.base_coord) == iplug);
			assert("edward-148", hint.coord.base_coord.between != AT_UNIT);
			coord_clear_iplug(&hint.coord.base_coord);
			result = zload_ra(hint.coord.base_coord.node, &ra_info);
			if (unlikely(result))
				goto out2;
			if (read) {
				result = iplug->s.file.read(NULL, &f, &hint.coord);
				if(result)
					goto out;
			}
			if (write) 
				znode_make_dirty(hint.coord.base_coord.node);
			zrelse(hint.coord.base_coord.node);
			done_lh(&lh);
			break;
		default:
			goto out2;
		}
	}
 ok:
	/* gathering finished with number of items > 0 */
	/* NOTE-EDWARD: Handle the cases when cluster is incomplete (-EIO) */	
	clust->len = inode_scaled_cluster_size(inode) - f.length;
 out:
	zrelse(hint.coord.base_coord.node);
 out2:
	done_lh(&lh);
	save_file_hint(clust->file, &hint);
	return result;
}


/* we don't take an interest in how much bytes was written when error occures */
static int
read_some_cluster_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	unsigned to_read;
	item_plugin * iplug;
	
	iplug = item_plugin_by_id(CTAIL_ID);
	/* bytes to read, we start read from the beginning of the cluster
	   for obvious reason */
	to_read = clust->off + clust->count + clust->delta;
	
	for (i = 0; i < clust->nr_pages; i++) {
		
		/* assert ("edward-218", !PageLocked(clust->pages[i])); */
		/* FIXME_EDWARD lock pages before each checking */
		
		if (clust->off <= (i << PAGE_CACHE_SHIFT) && (i << PAGE_CACHE_SHIFT) <= to_read)
			/* page will be completely overwritten, skip this */
			continue;
		if (PageUptodate(clust->pages[i]))
			continue;
		if (!cluster_is_uptodate(clust)) {
			/* read cluster and mark leaf znodes dirty */ 
			result = ctail_read_cluster(clust, inode, 1 /* write */);
			if (result)
				goto out;
		}
		reiser4_lock_page(clust->pages[i]);
		result =  do_readpage_ctail(clust, clust->pages[i]);
		reiser4_unlock_page(clust->pages[i]);
		if (result) {
			impossible("edward-219", "do_readpage_ctail returned crap");
			goto out;
		}
	}
	if (!cluster_is_uptodate(clust))
		/* disk cluster is unclaimed, make its znodes dirty */
		find_cluster(clust, inode, 0 /* do not read */, 1 /*write */);
 out:
	release_cluster_buf(clust, inode);
	return result;
}

/* Helper function called by write_cryptcompress_flow()
    grab cluster pages,
    maybe read them from disk,
    maybe write hole
*/
static int
prepare_cluster(struct inode *inode,
		loff_t file_off /* write position in the file */,
		loff_t to_file, /* bytes of users data to write to the file */
		reiser4_cluster_t *clust)
{
	char *data;
	int result = 0;
	unsigned to_write;

	assert("edward-177", inode != NULL);
	assert("edward-280", cluster_invariant(clust, inode));

	to_write = clust->count + clust->delta;
	
	set_cluster_nr_pages(inode, clust);
	if(!clust->nr_pages)
		/* do nothing */
		return 0;
	/* collect unlocked pages and jnodes */
	result = grab_cache_cluster(inode, clust);
	if (result)
		return result;
	if (clust->off == 0 && inode->i_size <= clust_to_off(clust->index, inode) + to_write) {
		/* we don't need to read cluster from disk, just
		   align the current chunk of data up to nr_pages */
		int i;
		unsigned off = off_to_pgoff(to_write);
		for(i = off_to_pg(to_write); i < clust->nr_pages; i++, off = 0) {
			
			crypto_plugin * cplug = inode_crypto_plugin(inode);

			assert("edward-285", clust->pages[i] != NULL);
			
			reiser4_lock_page(clust->pages[i]);
			data = kmap_atomic(clust->pages[i], KM_USER0);
			cplug->align_cluster(data + off, off, PAGE_CACHE_SIZE);
			kunmap_atomic(data, KM_USER0);
			reiser4_unlock_page(clust->pages[i]);
		}
	}
	result = read_some_cluster_pages(inode, clust);
	if (result) {
		release_cache_cluster(clust);
		return result;
	}
	if (clust->stat == HOLE_CLUSTER)
		return (write_hole(inode, clust, file_off, to_file));
	
 	return 0;
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

	xmemset(*clust->pages, 0, sizeof(clust->pages) << inode_cluster_shift(inode));
	
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
		result = prepare_cluster(inode, file_off, f.length, &clust);
		if (result)
			goto exit;
	}
	do {
		unsigned page_off, page_count;
		
		result = prepare_cluster(inode, file_off, f.length, &clust);
		if (result)
			goto exit1;
		assert("edward-204", clust.stat == DATA_CLUSTER);
		assert("edward-161", schedulable());
		
		/* set write position in page */
		page_off = off_to_pgoff(clust.off);
		
                /* copy user's data to cluster pages */
		for (i = off_to_pg(clust.off); i <= off_to_pg(clust.off + clust.count); i++) {
			page_count = min_count(PAGE_CACHE_SIZE - page_off, clust.count);

			assert("edward-287", pages[i] != NULL);
			
			reiser4_lock_page(pages[i]);
			result = __copy_from_user((char *)kmap(pages[i]) + page_off, f.data, page_count);
			kunmap(pages[i]);
			if (unlikely(result)) {
				reiser4_unlock_page(pages[i]);
				result = -EFAULT;
				goto exit2;
			}
			reiser4_unlock_page(pages[i]);
			page_off = 0;
		}
		
		set_cluster_pages_dirty(&clust, NULL);
		
		result = try_capture_cluster(&clust, NULL);
		if (result)
			goto exit2;
		
		make_cluster_jnodes_dirty(&clust, NULL);
		
		result = update_inode_and_sd_if_necessary(inode,
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
		release_cache_cluster(&clust);
	exit1:
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
	int result;
	
	assert("edward-293", inode_file_plugin(inode)->key_by_inode == key_by_inode_cryptcompress);
	key_by_inode_cryptcompress(inode, off_to_clust(new_size, inode) + 1, &from_key);
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
			result = update_inode_and_sd_if_necessary
				(inode, get_key_offset(&smallest_removed), 1, 1, update_sd);
			if (result)
				break;

			all_grabbed2free(__FUNCTION__);
			reiser4_release_reserved(inode->i_sb);

			continue;
		}
		break;
	}
	all_grabbed2free(__FUNCTION__);
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
	if (!off_to_cloff(old_size, inode))
		/* truncated to cluster boundary */
		return 0;
	/* FIXME-EDWARD: reserve partial page */
	pages = reiser4_kmalloc(sizeof(*pages) << inode_cluster_shift(inode), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	reiser4_cluster_init(&clust);
	clust.pages = pages;
	clust_by_offs(&clust, inode, new_size, old_size);

	result = prepare_cluster(inode, 0, 0, &clust);
	if (result)
		goto exit;
	/* truncate cluster, so flush will deal with new number of pages */

	assert("edward-294", clust.stat == DATA_CLUSTER);

	pg_padd = PAGE_CACHE_SIZE - off_to_pgoff(clust.off);
	
	truncate_pages_cryptcompress(inode->i_mapping, off_to_pg(clust_to_off(clust.index, inode) + clust.off + pg_padd));

	reiser4_lock_page(pages[off_to_pg(clust.off)]);
	kaddr = kmap_atomic(pages[off_to_pg(clust.off)], KM_USER0);
	cplug->align_cluster(kaddr + off_to_pgoff(clust.off), off_to_pgoff(clust.off), PAGE_CACHE_SIZE);
	reiser4_unlock_page(pages[off_to_pg(clust.off)]);

	nrpages = off_to_pg(clust.off) + 1;
	set_cluster_pages_dirty(&clust, &nrpages);
	result = try_capture_cluster(&clust, &nrpages);
	if(result)
		goto exit;
	make_cluster_jnodes_dirty(&clust, &nrpages);

	result = update_inode_and_sd_if_necessary(inode, new_size, 1, 1, update_sd);
 exit:
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
	unsigned long old_idx = off_to_clust(old_size, inode);
	unsigned long new_idx = off_to_clust(new_size, inode);
	
	/* inode->i_size != new size */

	/* without decompression we can specify
	   real file offsets only up to cluster size */ 
	result = find_file_idx(inode, &idx);

	assert("edward-278", idx <= old_idx);

	if (result)
		return result;
	if (idx <= new_idx) {
		/* do not deal with items */
		if (update_sd) {
			result = setattr_reserve(tree_by_inode(inode));
			if (!result)
				result = update_inode_and_sd_if_necessary(inode, new_size, 1, 1, 1);
			all_grabbed2free(__FUNCTION__);	
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

/* plugin->u.file.capture */
int
capture_cryptcompress(struct page *page)
{
	return 0;
}

/* plugin->u.file.release */
int
release_cryptcompress(struct inode *inode, struct file * file)
{
	return 0;
}

/* plugin->u.file.mmap */
int
mmap_cryptcompress(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

/* plugin->u.file.get_block */
int
get_block_cryptcompress(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create UNUSED_ARG)
{
	return 0;
}

/* plugin->u.file.delete */
int
delete_cryptcompress(struct inode *inode)
{
	return 0;
}

/* plugin->u.file.init_inode_data */
void init_inode_data_cryptcompress(struct inode *inode, reiser4_object_create_data *crd, int create)
{
}

/* plugin->u.file.owns_item */
int
owns_item_cryptcompress(const struct inode *inode	/* object to check against */ ,
			const coord_t *coord /* coord to check */ )
{
	return 0;
}

/* plugin->u.file.pre_delete */
int
pre_delete_cryptcompress(struct inode *inode)
{
	return 0;
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
			all_grabbed2free(__FUNCTION__);
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
