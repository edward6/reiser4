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

int do_readpage_ctail(reiser4_cluster_t *, struct page * page);
int ctail_read_cluster (reiser4_cluster_t *, struct inode *);

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

 __attribute__((unused)) static int cryptcompress_create(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
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
	assert("edward-27", data->id = CRC_FILE_PLUGIN_ID);
	
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

/* return true if the cluster contains specified page */
int
page_of_cluster(struct page * page, reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-162", page != NULL);
	assert("edward-163", clust != NULL);
	assert("edward-164", inode != NULL);
	assert("edward-165", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	return ((clust->index << PAGE_CACHE_SHIFT) <= page->index &&
		page->index < ((clust->index + (1 << inode_cluster_shift(inode))) << PAGE_CACHE_SHIFT));
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
	assert("edward-99", stat->keysize != 0);

	cplug = inode_crypto_plugin(inode);
	
	assert("edward-109", cplug != NULL);
	
	size = cplug->blocksize(stat->keysize);
	return cplug->scale(inode, size, src_off);
}

static inline unsigned min_count(unsigned a, unsigned b)
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
static int
cluster_key_by_inode(struct inode *inode, loff_t off, reiser4_key * key)
{
	assert("edward-64", inode != 0);
	assert("edward-112", !(off & ~(~0UL << inode_cluster_shift(inode) << PAGE_CACHE_SHIFT)));
	/* don't come here with other offsets */
	
	build_sd_key(inode, key);
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) inode_scaled_offset(inode, off));
	return 0;
}

/* plugin->flow_by_inode */
int
cryptcompress_build_flow(struct inode *inode /* file to build flow for */ ,
		     char *buf /* user level buffer */ ,
		     int user	/* 1 if @buf is of user space, 0 - if it is
				   kernel space */ ,
		     size_t size /* buffer size */ ,
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
	assert("edward-151", inode_file_plugin(inode)->key_by_inode == cluster_key_by_inode);

	if (op == WRITE_OP && user == 1)
		return 0;
	return cluster_key_by_inode(inode, off, &f->key);
}

void reiser4_cluster_init (reiser4_cluster_t * clust)
{
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
		reiser4_kfree(clust->buf, (clust->tlen ?
					   /* buf was updated */
					   clust->tlen :
					   inode_scaled_cluster_size(inode)));
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

/* returns index of the cluster that @page belongs to */
inline unsigned long
cluster_index_by_page (struct page * page, struct inode * inode)
{
	return (page->index >> inode_cluster_shift(inode) <<
		inode_cluster_shift(inode));
}

static inline unsigned long
cluster_index_by_offset(struct inode * inode, loff_t offset)
{
	return (offset >>
		PAGE_CACHE_SHIFT >>
		inode_cluster_shift(inode) <<
		inode_cluster_shift(inode));
}

/* set minimal number of cluster pages (start from first one)
   which cover hole and users data */
static void
set_cluster_nr_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	assert("edward-180", clust != NULL);

	if (clust->count + clust->delta == 0) {
		/* nothing to write - nothing to read */
		assert ("edward-199", clust->stat == DATA_CLUSTER);
		assert ("edward-200", clust->off == 0);
		clust->nr_pages = 0;
		return;
	}
	clust->nr_pages = ((clust->off + clust->count + clust->delta - 1) >> PAGE_CACHE_SHIFT) + 1;
}

int
find_cluster_item(const reiser4_key *key, /* key of next cluster item to read */
		  coord_t *coord,
		  lock_handle *lh,
		  ra_info_t *ra_info)
{
	assert("edward-152", schedulable());
	
	return  coord_by_key(current_tree, key, coord, lh, ZNODE_READ_LOCK,
			     FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, CBK_UNIQUE, ra_info);
}

void get_cluster_magic(__u8 * magic)
{
	/* FIXME-EDWARD: fill this by first 4 bytes of decrypted keyid
	   PARANOID? */ 
}

/* @op == READ_OP:
   . decrypt cluster
   . check for end-of-cluster magic (NOTE-EDWARD: this magic should be private)
   . decompress cluster
   
   @op == WRITE_OP:
*/
int process_cluster(reiser4_cluster_t *clust, /* contains data to process */
		    struct inode *inode, rw_op op)
{
	int result = 0;
	int i, nr_fips;
	__u8 tail_size;
	size_t size, blksize;
	crypto_plugin * cr_plug;
	compression_plugin * co_plug;
	__u8 magic[CLUSTER_MAGIC_SIZE];
	__u8 * buf = NULL;
	__u32 * expkey;
	
	cr_plug = inode_crypto_plugin(inode);
	co_plug = inode_compression_plugin(inode);
	/* calculate actual cluster size before compression */
	size = min_count(inode->i_size - (clust->index << PAGE_CACHE_SHIFT), inode_cluster_size(inode));
	blksize = cr_plug->blocksize(inode_crypto_stat(inode)->keysize);
	
	assert("edward-154", clust->len <= size);
	assert("edward-155", clust->tlen == 0);
	
	/* decrypt cluster with the simplest ecb-mode
	 * NOTE-EDWARD: input cluster should be processed as a whole one
	 * FIXME-EDWARD: call here stream mode plugin for decompression */
	buf = reiser4_kmalloc(size, GFP_KERNEL);
	if (!buf) 
		return -ENOMEM;
	nr_fips = clust->len/blksize;
	expkey = cryptcompress_inode_data(inode)->expkey;

	for (i=0; i < nr_fips; i++)
		cr_plug->decrypt(expkey, buf + i*blksize, /* dst */
				 clust->buf + i*blksize /* src */);
	if (size == clust->len) {
		/* cluster was not compressed, alternate buffers */
		__u8 * tbuf;
		tbuf = clust->buf;
		clust->buf = buf;
		clust->tlen = size;
		buf = tbuf;
		size = inode_scaled_cluster_size(inode);
		goto exit;
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
	tail_size = *(buf + (clust->len - 1)); 
	if (memcmp(buf + clust->len - (size_t)CLUSTER_MAGIC_SIZE - tail_size,
		   magic, (size_t)CLUSTER_MAGIC_SIZE)) {
		printk("edward-156: process_cluster: wrong end-of-cluster magic\n");
		result = -EIO;
		goto exit;
	}
	{
		/* decompress cluster */
		__u8 wbuf[co_plug->mem_req];
		co_plug->decompress(wbuf, buf, clust->len, clust->buf, &clust->len);
		/* check the length of decompressed data */
		assert("edward-157", size == clust->len);
	}
 exit:
	reiser4_kfree(buf, size);
	return result;	
}

/* plugin->read() :
 * generic_file_read()   
 * All key offsets don't make sense in traditional unix semantics unless they
 * represent the beginning of clusters, so the only thing we can do is start
 * right from mapping to the address space (this is precisely what filemap
 * generic method does) */

/* plugin->readpage() */
__attribute__((unused)) static int
cryptcompress_readpage(void *vp, struct page *page)
{
	reiser4_cluster_t clust;
	struct file * file;
	item_plugin * iplug;
	int result;
	
	assert("edward-88", PageLocked(page));
	assert("edward-89", page->mapping && page->mapping->host);

	file = vp;
	assert("edward-113", page->mapping == file->f_dentry->d_inode->i_mapping);
	
	if (PageUptodate(page)) {
		printk("cryptcompress_readpage: page became already uptodate\n");
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
__attribute__((unused)) static void
cryptcompress_readpages(struct file *file UNUSED_ARG, struct address_space *mapping,
			struct list_head *pages)
{
	item_plugin *iplug;
	
	iplug = item_plugin_by_id(CTAIL_ID);
	iplug->s.file.readpages(NULL, mapping, pages);
	return;
}

static void
set_cluster_pages_dirty(reiser4_cluster_t * clust)
{
	int i;
	struct page * pg;

	for (i=0; i < clust->nr_pages; i++) {

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
try_capture_cluster(reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	
	for (i=0; i < clust->nr_pages; i++) {
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
make_cluster_jnodes_dirty(reiser4_cluster_t * clust)
{
	int i;
	jnode * node;
	
	for (i=0; i < clust->nr_pages; i++) {
		node = jprivate(clust->pages[i]);
		
		assert("edward-221", node != NULL);
		
		LOCK_JNODE(node);
		unformatted_jnode_make_dirty(node);
		UNLOCK_JNODE(node);
		
		jput(node);
	}
}

/* collect some unlocked cluster pages and jnodes */
static int
grab_cache_cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	jnode * node;
	
	assert("edward-182", clust != NULL);
	assert("edward-183", clust->pages != NULL);
	assert("edward-158", !(clust->index & ~(~0UL << inode_cluster_shift(inode))));
	assert("edward-184", 0 < clust->nr_pages && clust->nr_pages <= ( 1 << inode_cluster_shift(inode)));
	
	for (i = 0; i < clust->nr_pages; i++) {
		clust->pages[i] = grab_cache_page(inode->i_mapping, clust->index + i);
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


/* guess next cluster params */
static void
update_cluster(struct inode * inode, reiser4_cluster_t * clust, loff_t file_off, loff_t to_file)
{
	assert ("edward-185", clust != NULL);
	
	if (clust->stat == HOLE_CLUSTER) {
		/* set params for next cluster */
		assert ("edward-186", ergo(clust->count != 0, clust->off == 0));
		assert ("edward-187", clust->delta == 0);
		clust->stat = DATA_CLUSTER;
		clust->off = 0;
		clust->index = cluster_index_by_offset(inode, file_off);
		clust->count = (unsigned)(file_off & (inode_cluster_size(inode)));
		clust->delta = min_count(inode_cluster_size(inode) - clust->count, to_file);
		return;
	}
	/* DATA_CLUSTER (supposed to be updated first or second time) */
	assert ("edward-188", clust->index == cluster_index_by_offset(inode, file_off));
	assert ("edward-205", clust->count == 0);
	assert ("edward-206", clust->off < inode_cluster_size(inode));
	clust->delta = 0;
}

/* set zeroes to the cluster, update it, and maybe, try to capture its pages */
static int
write_hole(struct inode *inode, reiser4_cluster_t * clust, loff_t file_off, loff_t to_file)
{
	char * data;
	int result = 0;
	unsigned to_page, page_off;
	
	assert ("edward-190", clust != NULL);
	assert ("edward-191", ergo(clust->stat == HOLE_CLUSTER, clust->delta == 0));
	assert ("edward-191", ergo(clust->count == 0, clust->stat == DATA_CLUSTER));
	assert ("edward-192", clust->off + clust->count + clust->delta <= inode_cluster_size(inode));
	
	if (clust->off == 0 && clust->count == inode_cluster_size(inode)) {
		assert ("edward-201", clust->stat == HOLE_CLUSTER);
		/* fake cluster, just update it */
		goto update;
	}
	
	if (clust->count == 0) {
		/* nothing to write */
		assert ("edward-202", clust->off == 0);
		assert ("edward-203", clust->stat == DATA_CLUSTER);
		goto update;
	}
	page_off = clust->off & (PAGE_CACHE_SIZE - 1);
	
	while (clust->count) {
		struct page * page;
		page = clust->pages[clust->off >> PAGE_CACHE_SHIFT];
		to_page = min_count(PAGE_CACHE_SIZE - page_off, clust->count);
		reiser4_lock_page(page);
		data = kmap_atomic(page, KM_USER0);
		memset(data + page_off, 0, to_page);
		kunmap_atomic(data, KM_USER0);
		reiser4_unlock_page(page);
		
		clust->off += to_page;
		clust->count -= to_page;
		page_off = 0;
	}
	if (clust->index != cluster_index_by_offset(inode, file_off)) {
		/* cluster is over, try to flush it */
		assert ("edward-189", clust->stat == HOLE_CLUSTER);

		set_cluster_pages_dirty(clust);
		result = try_capture_cluster(clust);
		if (result) {
			return result;
		}
		make_cluster_jnodes_dirty(clust);
	}
 update:
	update_cluster(inode, clust, file_off, to_file);
	return 0;
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

		assert ("edward-218", !PageLocked(clust->pages[i]));
		
		if (clust->off <= (i << PAGE_CACHE_SHIFT) && (i << PAGE_CACHE_SHIFT) <= to_read)
			/* page will be completely overwritten, skip this */
			continue;
		if (PageUptodate(clust->pages[i]))
			continue;
		if (!cluster_is_uptodate(clust)) {
			result = ctail_read_cluster(clust, inode);
			if (result)
				break;
		}
		reiser4_lock_page(clust->pages[i]);
		result =  do_readpage_ctail(clust, clust->pages[i]);
		reiser4_unlock_page(clust->pages[i]);
		if (result) {
			impossible("edward-219", "do_readpage_ctail returned crap");
			break;
		}
	}
	release_cluster_buf(clust, inode);
	return result;
}

/* grab and read cluster pages, maybe write hole */
static int
prepare_cluster(struct inode *inode,
		loff_t file_off /* write position in the file */,
		loff_t to_file, /* bytes of users data to write to the file */
		reiser4_cluster_t *clust)
{
	char *data;
	int result = 0;
	unsigned to_write;

	assert ("edward-177", inode != NULL);
	assert ("edward-178", clust->pages != NULL);
	assert ("edward-170", clust->off < inode_cluster_size(inode));
	assert ("edward-193", ergo(clust->delta != 0, clust->stat == DATA_CLUSTER));

	to_write = clust->off + clust->count + clust->delta;
	assert ("edward-194", to_write <= inode_cluster_size(inode));
	
	set_cluster_nr_pages(inode, clust);
	/* collect unlocked pages and jnodes */
	result = grab_cache_cluster(inode, clust);
	if (result)
		return result;
	if (clust->off == 0 && (clust->index << PAGE_CACHE_SHIFT) + to_write >= inode->i_size) {
		/* we don't need to read cluster from disk, just
		   align last page starting from this offset: */
		unsigned off = to_write & (PAGE_CACHE_SIZE - 1);
		if (off) {
			crypto_plugin * cplug = inode_crypto_plugin(inode);

			reiser4_lock_page(clust->pages[clust->nr_pages - 1]);
			data = kmap_atomic(clust->pages[clust->nr_pages - 1], KM_USER0);
			cplug->align_cluster(data + off, off, PAGE_CACHE_SIZE);
			kunmap_atomic(data, KM_USER0);
			reiser4_unlock_page(clust->pages[clust->nr_pages - 1]);
		}
		return 0;
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

static void
set_cluster_params(struct inode * inode, reiser4_cluster_t * clust, flow_t * f, loff_t file_off)
{
	assert("edward-197", clust != NULL);
	assert("edward-198", inode != NULL);

	if (file_off > inode->i_size) {
		/* Uhmm, hole in crypto-file... */
		loff_t hole_size;
		hole_size = file_off - inode->i_size;
		printk("edward-176, Warning: Hole of size %llu in "
		       "cryptocompressed file (inode %llu, offset %llu) \n",
		       hole_size, get_inode_oid(inode), file_off);
		
		clust->index = cluster_index_by_offset(inode, inode->i_size);
		clust->off = (unsigned)(inode->i_size & inode_cluster_size(inode));
		clust->count = min_count(inode_cluster_size(inode) - clust->off, hole_size);
		
		if (clust->off + hole_size >= inode_cluster_size(inode))
			clust->stat = HOLE_CLUSTER;
		else /* size of users data to write to the cluster, delta >= 0 */
			clust->delta = min_count(inode_cluster_size(inode) - (clust->off + clust->count), f->length);
		return;
	}
	
	clust->index = cluster_index_by_offset(inode, file_off);
	clust->off = (unsigned)(file_off & (inode_cluster_size(inode) - 1));
	clust->count = min_count(inode_cluster_size(inode) - clust->off, f->length);
}

/* This slices user's data into clusters and copies to page cache.
   If error occures, returns number of bytes in successfully written clusters  */
/* FIXME_EDWARD replace flow by something lightweigth */

static loff_t 
write_flow(struct file * file , struct inode * inode, const char *buf, size_t count, loff_t pos)
{
	int i, result;
	flow_t f;
	size_t to_write;
	loff_t file_off;
	reiser4_cluster_t clust;
	struct page * pages[1 << inode_cluster_shift(inode)];

	assert("edward-159", current_blocksize == PAGE_CACHE_SIZE);
		
	result = cryptcompress_build_flow(inode, (char *)buf, 1, count, pos, WRITE_OP, &f);
	if (result)
		return result;
	to_write = f.length;

        /* current write position in file */
	file_off = pos;
	reiser4_cluster_init(&clust);
	clust.pages = pages;
	
	set_cluster_params(inode, &clust, &f, file_off);
	
	if (clust.stat == HOLE_CLUSTER) {
		result = prepare_cluster(inode, file_off, f.length, &clust);
		if (result)
			goto exit1;
	}
	do {
		unsigned page_off, page_count;

		assert("edward-204", clust.stat == DATA_CLUSTER);
		result = prepare_cluster(inode, file_off, f.length, &clust);
		if (result)
			goto exit1;
		assert("edward-161", schedulable());
		
		/* set write position in page */
		page_off = clust.off & (PAGE_CACHE_SIZE - 1);
		
                /* copy user's data to cluster pages */
		for (i = clust.off >> PAGE_CACHE_SHIFT;
		     i <= (clust.off + clust.count) >> PAGE_CACHE_SHIFT; i++) {
			page_count = min_count(PAGE_CACHE_SIZE - page_off, clust.count);
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
		
		set_cluster_pages_dirty(&clust);
		
		result = try_capture_cluster(&clust);
		if (result)
			goto exit2;
		
		make_cluster_jnodes_dirty(&clust);
		
		clust.off = 0;
		clust.stat = DATA_CLUSTER;
		file_off += clust.count;
		move_flow_forward(&f, clust.count);
		clust.count = min_count(inode_cluster_size(inode) - clust.off, f.length);
		continue;
	exit2:
		release_cache_cluster(&clust);
	exit1:
		break;
	} while (f.length);
	
	if (result == -EEXIST)
		printk("write returns EEXIST!\n");
	/* if nothing were written - there must be an error */
	assert("edward-195", ergo((to_write == f.length), result < 0));

	return (to_write - f.length) ? (to_write - f.length) : result;
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
	written = write_flow(file, inode, (char *)buf, count, pos);
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
