/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* The object plugin of reiser4 crypto-compressed (crc-)files. */

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

extern int common_file_save(struct inode *inode);

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
	
	result = common_file_save(object);
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

static crypto_stat_t * inode_crypto_stat (struct inode * inode)
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
	assert("edward-112", !(off & ~(~0UL << inode_cluster_shift(inode))));
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
	return cluster_key_by_inode(inode, off, &f->key);
}

void reiser4_cluster_init (reiser4_cluster_t * clust)
{
	assert("edward-84", clust != NULL);
	xmemset(clust, 0, sizeof *clust);
}

/* release cluster's data */
void put_cluster_data(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-121", clust != NULL);
	assert("edward-122", clust->buf != NULL);
	assert("edward-123", clust->stat != HOLE_CLUSTER);
	assert("edward-124", inode != NULL);
	assert("edward-125", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	reiser4_kfree(clust->buf, inode_scaled_cluster_size(inode));
	/* invalidate cluster data */
	clust->buf = NULL;
}

/* returns true if we need to read new cluster from disk */
int cluster_is_required (reiser4_cluster_t * clust)
{
	assert("edward-126", clust != NULL);
	return (clust->buf == NULL && clust->stat != HOLE_CLUSTER);
}

/* returns offset of the first page of the cluster that @page belows to */
inline loff_t
cluster_offset_by_page (struct page * page, struct inode * inode)
{
	return (page->index >> inode_cluster_shift(inode) <<
		inode_cluster_shift(inode) << PAGE_CACHE_SHIFT);
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

/* decrypt and inflate cluster data if @op == READ
   deflate and encrypt cluster data if @op == WRITE */
int process_cluster(reiser4_cluster_t *clust, struct inode *inode, rw_op op)
{
	return 0;
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
