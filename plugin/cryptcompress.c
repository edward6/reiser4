/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README

This file contains all cluster operations and methods of the reiser4
cryptcompress object plugin (see http://www.namesys.com/cryptcompress_design.html
for details).

Cryptcompress specific fields of reiser4 inode/stat-data:

                 Incore inode                               Disk stat-data
********************************************************************************************
* data structure       *         field        * data structure       *          field      *
********************************************************************************************
* plugin_set           *file plugin id        * reiser4_plugin_stat  *file plugin id       *
*                      *crypto plugin id      *                      *crypto plugin id     *
*                      *digest plugin id      *                      *digest plugin id     *
*                      *compression plugin id *                      *compression plugin id*
********************************************************************************************
* crypto_stat_t        *      keysize         * reiser4_crypto_stat  *      keysize        *
*                      *      keyid           *                      *      keyid          *
********************************************************************************************
* cluster_stat_t       *      cluster_shift   * reiser4_cluster_stat *      cluster_shift  *
********************************************************************************************
* cryptcompress_info_t *      crypto_tfm      *                      *                     *
********************************************************************************************
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
#include "../cluster.h"
#include "../seal.h"
#include "../vfs_ops.h"
#include "plugin.h"
#include "object.h"
#include "file/funcs.h"

#include <asm/scatterlist.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/crypto.h>
#include <linux/swap.h>
#include <linux/hardirq.h>

int do_readpage_ctail(reiser4_cluster_t *, struct page * page);
int ctail_read_cluster (reiser4_cluster_t *, struct inode *, int);
reiser4_key * append_cluster_key_ctail(const coord_t *, reiser4_key *);
int setattr_reserve(reiser4_tree *);
int reserve_cut_iteration(reiser4_tree *);
int writepage_ctail(struct page *);
int truncate_jnodes_range(struct inode *inode, unsigned long from, int count);
int cut_file_items(struct inode *inode, loff_t new_size, int update_sd, loff_t cur_size, int mode);
int delete_object(struct inode *inode, int mode);
__u8 cluster_shift_by_coord(const coord_t * coord);
int ctail_make_unprepped_cluster(reiser4_cluster_t * clust, struct inode * inode);
unsigned long clust_by_coord(const coord_t * coord);
int hint_is_set(const hint_t *hint);
reiser4_plugin * get_default_plugin(pset_member memb);

/* get cryptcompress specific portion of inode */
reiser4_internal cryptcompress_info_t *
cryptcompress_inode_data(const struct inode * inode)
{
	return &reiser4_inode_data(inode)->file_plugin_data.cryptcompress_info;
}

/* plugin->u.file.init_inode_data */
reiser4_internal void
init_inode_data_cryptcompress(struct inode *inode,
			      reiser4_object_create_data *crd, int create)
{
	cryptcompress_info_t * data;

	data = cryptcompress_inode_data(inode);
	assert("edward-685", data != NULL);

	xmemset(data, 0, sizeof (*data));
	init_inode_ordering(inode, crd, create);
}

#if REISER4_DEBUG
reiser4_internal int
crc_inode_ok(struct inode * inode)
{
	reiser4_inode * info = reiser4_inode_data(inode);
	cryptcompress_info_t * data = cryptcompress_inode_data(inode);

	if ((info->cluster_shift <= MAX_CLUSTER_SHIFT) &&
	    (data->tfm[CRYPTO_TFM] == NULL) &&
	    (data->tfm[DIGEST_TFM] == NULL))
		return 1;
	assert("edward-686", 0);
	return 0;
}
#endif

reiser4_internal crypto_stat_t * inode_crypto_stat (struct inode * inode)
{
	assert("edward-90", inode != NULL);
	assert("edward-91", reiser4_inode_data(inode) != NULL);
	return (reiser4_inode_data(inode)->crypt);
}

/* NOTE-EDWARD: Do not use crypto without digest */
static int
alloc_crypto_tfm(struct inode * inode, crypto_data_t * data)
{
	int result;
	crypto_plugin * cplug = crypto_plugin_by_id(data->cra);
	digest_plugin * dplug = digest_plugin_by_id(data->dia);

	assert("edward-414", dplug != NULL);
	assert("edward-415", cplug != NULL);

	result = dplug->alloc(inode);
	if (result)
		return result;
	result = cplug->alloc(inode);
	if (result) {
		dplug->free(inode);
		return result;
	}
	return 0;
}

static void
free_crypto_tfm(struct inode * inode)
{
	reiser4_inode * info;

	assert("edward-410", inode != NULL);

	info = reiser4_inode_data(inode);

	if (!inode_get_crypto(inode))
		return;

	assert("edward-411", inode_crypto_plugin(inode));
	assert("edward-763", inode_digest_plugin(inode));

	inode_crypto_plugin(inode)->free(inode);
	inode_digest_plugin(inode)->free(inode);
}

static int
attach_crypto_stat(struct inode * inode, crypto_data_t * data)
{
	__u8 * txt;

	crypto_stat_t * stat;
	struct scatterlist sg;
	struct crypto_tfm * dtfm;

	assert("edward-690", inode_get_crypto(inode));
	assert("edward-766", inode_get_digest(inode));

	dtfm =  inode_get_digest(inode);

	stat = reiser4_kmalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat)
		return -ENOMEM;

	stat->keyid = reiser4_kmalloc((size_t)crypto_tfm_alg_digestsize(dtfm), GFP_KERNEL);
	if (!stat->keyid) {
		reiser4_kfree(stat);
		return -ENOMEM;
	}
	txt = reiser4_kmalloc(data->keyid_size, GFP_KERNEL);
	if (!txt) {
		reiser4_kfree(stat->keyid);
		reiser4_kfree(stat);
		return -ENOMEM;
	}
	xmemcpy(txt, data->keyid, data->keyid_size);
	sg.page = virt_to_page (txt);
	sg.offset = offset_in_page (txt);
	sg.length = data->keyid_size;

	crypto_digest_init (dtfm);
	crypto_digest_update (dtfm, &sg, 1);
	crypto_digest_final (dtfm, stat->keyid);

	reiser4_inode_data(inode)->crypt = stat;
	reiser4_kfree(txt);

	return 0;
}

static void
detach_crypto_stat(struct inode * object)
{
	crypto_stat_t * stat;

	stat = inode_crypto_stat(object);

	assert("edward-691", crc_inode_ok(object));

	if (!inode_get_crypto(object))
		return;

	assert("edward-412", stat != NULL);

	reiser4_kfree(stat->keyid);
	reiser4_kfree(stat);
}

static void
init_default_crypto(crypto_data_t * data)
{
	assert("edward-692", data != NULL);

	xmemset(data, 0, sizeof(*data));

	data->cra = get_default_plugin(PSET_CRYPTO)->h.id;
	data->dia = get_default_plugin(PSET_DIGEST)->h.id;
	return;
}

static void
init_default_compression(compression_data_t * data)
{
	assert("edward-693", data != NULL);

	xmemset(data, 0, sizeof(*data));

	data->coa = get_default_plugin(PSET_COMPRESSION)->h.id;
}

static void
init_default_cluster(cluster_data_t * data)
{
	assert("edward-694", data != NULL);

	*data = DEFAULT_CLUSTER_SHIFT;
}

/*  1) fill crypto specific part of inode
    2) set inode crypto stat which is supposed to be saved in stat-data */
static int
inode_set_crypto(struct inode * object, crypto_data_t * data)
{
	int result;
	crypto_data_t def;
	struct crypto_tfm * tfm;
	crypto_plugin * cplug;
	digest_plugin * dplug;
	reiser4_inode * info = reiser4_inode_data(object);

	if (!data) {
		init_default_crypto(&def);
		data = &def;
	}
	cplug = crypto_plugin_by_id(data->cra);
	dplug = digest_plugin_by_id(data->dia);

	plugin_set_crypto(&info->pset, cplug);
	plugin_set_digest(&info->pset, dplug);

	result = alloc_crypto_tfm(object, data);
	if (!result)
		return result;

	if (!inode_get_crypto(object))
		/* nothing to do anymore */
		return 0;

	assert("edward-416", data != NULL);
	assert("edward-414", dplug != NULL);
	assert("edward-415", cplug != NULL);
	assert("edward-417", data->key!= NULL);
	assert("edward-88", data->keyid != NULL);
	assert("edward-83", data->keyid_size != 0);
	assert("edward-89", data->keysize != 0);

	tfm = inode_get_tfm(object, CRYPTO_TFM);
	assert("edward-695", tfm != NULL);

	result = cplug->setkey(tfm, data->key, data->keysize);
	if (result) {
		free_crypto_tfm(object);
		return result;
	}
	assert ("edward-34", !inode_get_flag(object, REISER4_SECRET_KEY_INSTALLED));
	inode_set_flag(object, REISER4_SECRET_KEY_INSTALLED);

	info->extmask |= (1 << CRYPTO_STAT);

	result = attach_crypto_stat(object, data);
	if (result)
		goto error;

	info->plugin_mask |= (1 << PSET_CRYPTO) | (1 << PSET_DIGEST);

	return 0;
 error:
	free_crypto_tfm(object);
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}

static void
inode_set_compression(struct inode * object, compression_data_t * data)
{
	compression_data_t def;
	reiser4_inode * info = reiser4_inode_data(object);

	if (!data) {
		init_default_compression(&def);
		data = &def;
	}
	plugin_set_compression(&info->pset, compression_plugin_by_id(data->coa));
	info->plugin_mask |= (1 << PSET_COMPRESSION);

	return;
}

static int
inode_set_cluster(struct inode * object, cluster_data_t * data)
{
	int result = 0;
	cluster_data_t def;
	reiser4_inode * info;

	assert("edward-696", object != NULL);

	info = reiser4_inode_data(object);

	if(!data) {
		/* NOTE-EDWARD:
		   this is necessary parameter for cryptcompress objects! */
		printk("edward-418, create_cryptcompress: default cluster size (4K) was assigned\n");

		init_default_cluster(&def);
		data = &def;
	}
	assert("edward-697", *data <= MAX_CLUSTER_SHIFT);

	info->cluster_shift = *data;
	info->extmask |= (1 << CLUSTER_STAT);
	return result;
}

/* plugin->create() method for crypto-compressed files

. install plugins
. attach crypto info if specified
. attach compression info if specified
. attach cluster info
*/
reiser4_internal int
create_cryptcompress(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
{
	int result;
	reiser4_inode * info;

	assert("edward-23", object != NULL);
	assert("edward-24", parent != NULL);
	assert("edward-30", data != NULL);
	assert("edward-26", inode_get_flag(object, REISER4_NO_SD));
	assert("edward-27", data->id == CRC_FILE_PLUGIN_ID);

	info = reiser4_inode_data(object);

	assert("edward-29", info != NULL);

	/* set file bit */
	info->plugin_mask |= (1 << PSET_FILE);

	/* set crypto */
	result = inode_set_crypto(object, data->crypto);
	if (result)
		goto error;

	/* set compression */
	inode_set_compression(object, data->compression);

	/* set cluster info */
	result = inode_set_cluster(object, data->cluster);
	if (result)
		goto error;
	/* set plugin mask */
	info->extmask |= (1 << PLUGIN_STAT);

	/* save everything in disk stat-data */
	result = write_sd_by_inode_common(object);
	if (!result)
		return 0;
	/* save() method failed, release attached crypto info */
	inode_clr_flag(object, REISER4_CRYPTO_STAT_LOADED);
	inode_clr_flag(object, REISER4_CLUSTER_KNOWN);
 error:
	free_crypto_tfm(object);
	detach_crypto_stat(object);
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}

reiser4_internal int open_cryptcompress(struct inode * inode, struct file * file)
{
	/* FIXME-EDWARD: should be powered by key management */
	assert("edward-698", inode_file_plugin(inode) == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	return 0;
}

/* plugin->destroy_inode() */
reiser4_internal void
destroy_inode_cryptcompress(struct inode * inode)
{
	assert("edward-802", inode_file_plugin(inode) == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	assert("edward-803", !is_bad_inode(inode) && is_inode_loaded(inode));
	assert("edward-804", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));

	free_crypto_tfm(inode);
	if (inode_get_flag(inode, REISER4_CRYPTO_STAT_LOADED))
		detach_crypto_stat(inode);
	inode_clr_flag(inode, REISER4_CLUSTER_KNOWN);
	inode_clr_flag(inode, REISER4_CRYPTO_STAT_LOADED);
	inode_clr_flag(inode, REISER4_SECRET_KEY_INSTALLED);
}

/* returns translated offset */
reiser4_internal loff_t inode_scaled_offset (struct inode * inode,
					     const loff_t src_off /* input offset */)
{
	assert("edward-97", inode != NULL);

	if (!inode_get_crypto(inode) || src_off == get_key_offset(max_key()))
		return src_off;

	return inode_crypto_plugin(inode)->scale(inode, crypto_blocksize(inode), src_off);
}

/* returns disk cluster size */
reiser4_internal size_t
inode_scaled_cluster_size (struct inode * inode)
{
	assert("edward-110", inode != NULL);
	assert("edward-111", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));

	return inode_scaled_offset(inode, inode_cluster_size(inode));
}

/* return true if the cluster contains specified page */
reiser4_internal int
page_of_cluster(struct page * page, reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-162", page != NULL);
	assert("edward-163", clust != NULL);
	assert("edward-164", inode != NULL);
	assert("edward-165", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));

	return (pg_to_clust(page->index, inode) == clust->index);
}

reiser4_internal int count_to_nrpages(unsigned count)
{
	return (!count ? 0 : off_to_pg(count - 1) + 1);
}

static int
new_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	return (clust_to_off(clust->index, inode) >= inode->i_size);
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

/* cluster index should be valid */
reiser4_internal void
set_nrpages_by_inode(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-785", clust != NULL);
	assert("edward-786", inode != NULL);

	clust->nr_pages = count_to_nrpages(fsize_to_count(clust, inode));
}

/* plugin->key_by_inode() */
/* see plugin/plugin.h for details */
reiser4_internal int
key_by_inode_cryptcompress(struct inode *inode, loff_t off, reiser4_key * key)
{
	loff_t clust_off;
	
	assert("edward-64", inode != 0);
	//	assert("edward-112", ergo(off != get_key_offset(max_key()), !off_to_cloff(off, inode)));
	/* don't come here with other offsets */

	clust_off = (off == get_key_offset(max_key()) ? get_key_offset(max_key()) : off_to_clust_to_off(off, inode));

	key_by_inode_and_offset_common(inode, 0, key);
	set_key_offset(key, (__u64) (!inode_crypto_stat(inode) ? clust_off : inode_scaled_offset(inode, clust_off)));
	return 0;
}

/* plugin->flow_by_inode */
reiser4_internal int
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

reiser4_internal int
hint_prev_cluster(reiser4_cluster_t * clust)
{
	assert("edward-699", clust != NULL);
	assert("edward-700", clust->hint != NULL);

	if (!clust->hint->coord.valid)
		return 0;
	assert("edward-701", clust->file != NULL);
	assert("edward-702", clust->file->f_dentry != NULL);
	assert("edward-703", clust->file->f_dentry->d_inode != NULL);

	return (clust->index == off_to_clust(clust->hint->offset, clust->file->f_dentry->d_inode));
}

static int
crc_hint_validate(hint_t *hint, const reiser4_key *key, int check_key, znode_lock_mode lock_mode)
{
	assert("edward-704", hint != NULL);
	
	if (hint->coord.valid) {
		assert("edward-705", znode_is_any_locked(hint->coord.base_coord.node)); 
		return 0;
	}
	assert("edward-706", hint->coord.lh->owner == NULL);
	if (!hint || !hint_is_set(hint) || hint->mode != lock_mode)
		/* hint either not set or set by different operation */
		return RETERR(-E_REPEAT);

	if (check_key) {
		if (get_key_offset(key) != hint->offset)
			/* hint is set for different key */
			return RETERR(-E_REPEAT);
	}
	else {
		/* unexpected */
		coord_t * coord = &hint->coord.base_coord;
		assert("edward-963", coord->between == AFTER_ITEM /* was not found */||
		       coord->between == AFTER_UNIT /* next unprepped cluster */ ||
		       coord->between == AT_UNIT /* after find disk cluster(last item was't read */ ||
		       coord->between == BEFORE_ITEM /* after find disk cluster(last item was read */);	
		if (coord->between == AT_UNIT || coord->between == BEFORE_ITEM) {
			/* AFTER_CLUSTER */
			assert("edward-964", coord->item_pos > 0);
			coord->item_pos --;
			coord->between = AT_UNIT;
#if REISER4_DEBUG
			hint->seal.coord.item_pos --;
			hint->seal.coord.between = AT_UNIT;
#endif
		}
	}	
	assert("edward-707", schedulable());
	
	return seal_validate(&hint->seal, &hint->coord.base_coord, key,
			     hint->level, hint->coord.lh, FIND_MAX_NOT_MORE_THAN, lock_mode, ZNODE_LOCK_LOPRI);
}

static inline void
crc_validate_extended_coord(uf_coord_t *uf_coord, loff_t offset)
{
	assert("edward-708", item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension);
	
	item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension(uf_coord, offset);
}

static inline void
crc_invalidate_extended_coord(uf_coord_t *uf_coord)
{
	coord_clear_iplug(&uf_coord->base_coord);
	uf_coord->valid = 0;
}

static int all_but_offset_key_eq(const reiser4_key *k1, const reiser4_key *k2)
{
	return (get_key_locality(k1) == get_key_locality(k2) &&
		get_key_type(k1) == get_key_type(k2) &&
		get_key_band(k1) == get_key_band(k2) &&
		get_key_ordering(k1) == get_key_ordering(k2) &&
		get_key_objectid(k1) == get_key_objectid(k2));
}

static int
__reserve4cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	int result = 0;
	int reserved = 0;
	
	assert("edward-965", schedulable());
	assert("edward-439", inode != NULL);
	assert("edward-440", clust != NULL);
	assert("edward-441", clust->pages != NULL);
	assert("edward-442", jprivate(clust->pages[0]) != NULL);
	
	reserved = estimate_insert_cluster(inode, 0/* prepped */);
	result = reiser4_grab_space_force(reserved, BA_CAN_COMMIT);
	if (result)
		return result;
	clust->reserved = 1;
	grabbed2cluster_reserved(reserved);
	all_grabbed2free();
	
#if REISER4_DEBUG
	{
		reiser4_context * ctx = get_current_context();
		assert("edward-966", ctx->grabbed_blocks == 0);
	}
#endif
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
	assert("edward-967", clust->reserved == 1);
	
	cluster_reserved2free(estimate_insert_cluster(inode, 0));
	clust->reserved = 0;
}

/* Search a disk cluster item.
   If result is not cbk_errored current znode is locked */
reiser4_internal int
find_cluster_item(hint_t * hint, /* coord, lh, seal */
		  const reiser4_key *key, /* key of next cluster item to read */
		  int check_key,
		  znode_lock_mode lock_mode /* which lock */,
		  ra_info_t *ra_info,
		  lookup_bias bias)
{
	int result;
	reiser4_key ikey;
	coord_t * coord = &hint->coord.base_coord;
	
	assert("edward-152", hint != NULL);
	
	if (hint->coord.valid) {
#if REISER4_DEBUG
		result = zload(coord->node);
		
		assert("edward-889", !result);
		assert("edward-890", item_plugin_by_coord(coord) == item_plugin_by_id(CTAIL_ID));
		zrelse(coord->node);
#endif
		assert("edward-709", znode_is_any_locked(coord->node));
		if (coord->between == BEFORE_ITEM) {
			if (equal_to_rdk(coord->node, key)) {
				result = goto_right_neighbor(coord, hint->coord.lh);
				if (result == -E_NO_NEIGHBOR) {
					crc_invalidate_extended_coord(&hint->coord);
					return CBK_COORD_NOTFOUND;
				}
			}
			coord->between = AT_UNIT;
			result = zload(coord->node);
			if (result)
				return result;
			/* check current item */
			
			if(!coord_is_existing_item(coord)) {
				/* FIXME-EDWARD: This was the last item
				   of the object */
				crc_invalidate_extended_coord(&hint->coord);
				zrelse(coord->node);
				longterm_unlock_znode(hint->coord.lh);
				goto traverse_tree;
			}
			item_key_by_coord(coord, &ikey);
			if (!all_but_offset_key_eq(key, &ikey)) {
				unset_hint(hint);
				zrelse(coord->node);
				return CBK_COORD_NOTFOUND;
			}
			if (get_key_offset(key) == get_key_offset(&ikey)) {
				zrelse(coord->node);
				return CBK_COORD_FOUND;
			}
			//assert("edward-765", get_key_offset(key) > get_key_offset(&ikey));
			zrelse(coord->node);
			return CBK_COORD_NOTFOUND;
		}
		else {
			assert("edward-710", coord->between == AT_UNIT);	
			
			/* repeat check with updated @key */
			result = zload(coord->node);
			if (result)
				return result;
			item_key_by_coord(coord, &ikey);
			assert("edward-711", all_but_offset_key_eq(key, &ikey));
			
			if (get_key_offset(key) == get_key_offset(&ikey)) {
				zrelse(coord->node);
				return CBK_COORD_FOUND;
			}
			zrelse(coord->node);
			/* status is not changed, perhaps this is a hole */
			return CBK_COORD_NOTFOUND;
		}
	}
	else {
		/* extended coord is invalid */
		result = crc_hint_validate(hint, key, check_key, lock_mode);
		if (result)
			goto traverse_tree;
#if REISER4_DEBUG
		result = zload(coord->node);
		
		assert("edward-891", !result);
		assert("edward-892", item_plugin_by_coord(coord) == item_plugin_by_id(CTAIL_ID));
		zrelse(coord->node);
#endif		
		assert("edward-712", znode_is_any_locked(coord->node));	
		
		/* hint is valid, extended coord is invalid */
		if (check_key) {
#if REISER4_DEBUG
			result = zload(coord->node);
			assert("edward-893", !result);
			item_key_by_coord(coord, &ikey);
			assert("edward-894", keyeq(key, &ikey));
			zrelse(coord->node);
#endif
			
			coord->between = AT_UNIT;
			return CBK_COORD_FOUND;
		}
#if REISER4_DEBUG
		{
			int cluster_shift;
			result = zload(coord->node);
			assert("edward-897", !result);
			cluster_shift = cluster_shift_by_coord(coord);
			item_key_by_coord(coord, &ikey);
			
			/* at least current item should be of previous cluster */
			assert("edward-898", all_but_offset_key_eq(key, &ikey));
			assert("edward-899", get_key_offset(key) >> PAGE_CACHE_SHIFT >> cluster_shift ==
			       (get_key_offset(&ikey) >> PAGE_CACHE_SHIFT >> cluster_shift) + 1);
			zrelse(coord->node);
		}
#endif		
		return CBK_COORD_NOTFOUND;
	}
	assert("edward-713", hint->coord.lh->owner == NULL);
 traverse_tree:
	
	assert("edward-714", schedulable());
	
	coord_init_zero(coord);
	hint->coord.valid = 0;
	return  coord_by_key(current_tree, key, coord, hint->coord.lh, lock_mode,
			     bias, LEAF_LEVEL, LEAF_LEVEL, CBK_UNIQUE, ra_info);
}

/* FIXME-EDWARD */
#if 0
/* This represent reiser4 crypto alignment policy.
   Returns the size > 0 of aligning overhead, if we should align/cut,
   returns 0, if we shouldn't (alignment assumes appending an overhead of the size > 0) */
static int
crypto_overhead(size_t len /* advised length */,
		reiser4_cluster_t * clust,
		struct inode * inode, rw_op rw)
{
	size_t size = 0;
	int result = 0;
	int oh;

	assert("edward-486", clust != 0);

	if (!inode_get_crypto(inode) || !inode_crypto_plugin(inode)->align_cluster)
		return 0;
	if (!len)
		size = clust->len;

	assert("edward-615", size != 0);
	assert("edward-489", crypto_blocksize(inode) != 0);

	switch (rw) {
	case WRITE_OP: /* align */
		assert("edward-488", size <= inode_cluster_size(inode));

		oh = size % crypto_blocksize(inode);

		if (!oh && size == fsize_to_count(clust, inode))
			/* cluster don't need alignment and didn't get compressed */
			return 0;
		result = (crypto_blocksize(inode) - oh);
		break;
	case READ_OP: /* cut */
		assert("edward-490", size <= inode_scaled_cluster_size(inode));
		if (size >= inode_scaled_offset(inode, fsize_to_count(clust, inode)))
			/* cluster didn't get aligned */
			return 0;
		assert("edward-491", tfm_stream_data(clust, OUTPUT_STREAM) != NULL);
		assert("edward-900", 0);
		/* FIXME-EDWARD: the stuff above */
		result = *(tfm_stream_data(clust, OUTPUT_STREAM) + size - 1);
		break;
	default:
		impossible("edward-493", "bad option for getting alignment");
	}
	return result;
}
#endif

/* maximal aligning overhead which can be appended
   to the flow before encryption if any */
reiser4_internal unsigned
max_crypto_overhead(struct inode * inode)
{
	if (!inode_get_crypto(inode) || !inode_crypto_plugin(inode)->align_cluster)
		return 0;
	return crypto_blocksize(inode);
}

reiser4_internal unsigned
compress_overhead(struct inode * inode, int in_len)
{
	return inode_compression_plugin(inode)->overrun(in_len);
}

/* The following two functions represent reiser4 compression policy */
static int
try_compress(tfm_cluster_t * tc, struct inode * inode)
{
	return (inode_compression_plugin(inode) != compression_plugin_by_id(NONE_COMPRESSION_ID)) &&
		(tc->len >= MIN_SIZE_FOR_COMPRESSION);
}

static int
try_encrypt(struct inode * inode)
{
	return inode_get_crypto(inode) != NULL;
}

/* Decide by the lengths of compressed and decompressed cluster, should we save or should
   we discard the result of compression. The policy is that the length of compressed then
   encrypted cluster including _all_ appended infrasrtucture should be _less_ then its lenght
   before compression. */
static int
save_compressed(int old_size, int new_size, struct inode * inode)
{
	return (new_size + CLUSTER_MAGIC_SIZE + max_crypto_overhead(inode) < old_size);
}

/* guess if the cluster was compressed */
static int
need_decompression(reiser4_cluster_t * clust, struct inode * inode,
		   int encrypted /* is cluster encrypted */)
{
	tfm_cluster_t * tc = &clust->tc;
	
	assert("edward-142", tc != 0);
	assert("edward-143", inode != NULL);

	return (inode_compression_plugin(inode) != compression_plugin_by_id(NONE_COMPRESSION_ID)) &&
		(tc->len < (encrypted ? inode_scaled_offset(inode, fsize_to_count(clust, inode)) : fsize_to_count(clust, inode)));

}

static void set_compression_magic(__u8 * magic)
{
	/* FIXME-EDWARD: Use a checksum here */
	assert("edward-279", magic != NULL); 
	xmemset(magic, 0, CLUSTER_MAGIC_SIZE);
}

reiser4_internal int
grab_tfm_stream(struct inode * inode, tfm_cluster_t * tc, 
		tfm_action act, tfm_stream_id id)
{
	size_t size = inode_scaled_cluster_size(inode);
	
	assert("edward-901", tc != NULL);
	assert("edward-1027", inode_compression_plugin(inode) != NULL);
	
	if (act == TFM_WRITE)
		size += compress_overhead(inode, inode_cluster_size(inode));

	if (!tfm_stream(tc, id) && id == INPUT_STREAM)
		alternate_streams(tc);
	if (!tfm_stream(tc, id))
		return alloc_tfm_stream(tc, size, id);
	
	assert("edward-902", tfm_stream_is_set(tc, id));
	
	if (tfm_stream_size(tc, id) < size)
		return realloc_tfm_stream(tc, size, id);
	return 0;
}

/* Common deflate cluster manager */

reiser4_internal int
deflate_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result = 0;
	int transformed = 0;
	tfm_cluster_t * tc = &clust->tc;
	
	assert("edward-401", inode != NULL);
	assert("edward-903", tfm_stream_is_set(tc, INPUT_STREAM));
	assert("edward-498", !tfm_cluster_is_uptodate(tc));
	
	if (result)
		return result;
	if (try_compress(tc, inode)) {
		/* try to compress, discard bad results */
		__u32 dst_len;
		compression_plugin * cplug = inode_compression_plugin(inode);
		
		assert("edward-602", cplug != NULL);
		
		result = grab_tfm_stream(inode, tc, TFM_WRITE, OUTPUT_STREAM);
		if (result)
			return result;
		dst_len = tfm_stream_size(tc, OUTPUT_STREAM);
		cplug->compress(get_coa(tc, cplug->h.id),
				tfm_stream_data(tc, INPUT_STREAM), tc->len,
				tfm_stream_data(tc, OUTPUT_STREAM), &dst_len);
		
		/* make sure we didn't overwrite extra bytes */
		assert("edward-603", dst_len <= tfm_stream_size(tc, OUTPUT_STREAM));
		
		/* should we accept or discard the result of compression transform */
		if (save_compressed(tc->len, dst_len, inode)) {
			/* accept */
			tc->len = dst_len;

			set_compression_magic(tfm_stream_data(tc, OUTPUT_STREAM) + tc->len);
			tc->len += CLUSTER_MAGIC_SIZE;
			transformed = 1;
		}
	}
	if (try_encrypt(inode)) {
		crypto_plugin * cplug;
		/* FIXME-EDWARD */
		assert("edward-904", 0);
		
		cplug = inode_crypto_plugin(inode);
		if (transformed)
			alternate_streams(tc);
		result = grab_tfm_stream(inode, tc, TFM_WRITE, OUTPUT_STREAM);
		if (result)
			return result;
		/* FIXME: set src_len, dst_len, encrypt */
		transformed = 1;
	}
	if (!transformed)
		alternate_streams(tc);
	return result;
}

/* Common inflate cluster manager. 
   Is used in readpage() or readpages() methods of
   cryptcompress object plugins. */
reiser4_internal int
inflate_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result = 0;
	int transformed = 0;
	
	tfm_cluster_t * tc = &clust->tc;
	
	assert("edward-905", inode != NULL);
	assert("edward-906", tfm_stream_is_set(&clust->tc, INPUT_STREAM));
	assert("edward-907", !tfm_cluster_is_uptodate(tc));
	
	if (inode_get_crypto(inode) != NULL) {
		crypto_plugin * cplug;

		/* FIXME-EDWARD: isn't supported yet */
		assert("edward-908", 0);
		cplug = inode_crypto_plugin(inode);
		assert("edward-617", cplug != NULL);
		
		result = grab_tfm_stream(inode, tc, TFM_READ, OUTPUT_STREAM);
		if (result)
			return result;
		assert("edward-909", tfm_cluster_is_set(tc));
		
		/* set src_len, dst_len and decrypt */
		/* tc->len = dst_len */
		
		transformed = 1;
	}
	if (need_decompression(clust, inode, 0)) {
		__u8 magic[CLUSTER_MAGIC_SIZE];
		unsigned dst_len = inode_cluster_size(inode);
		compression_plugin * cplug = inode_compression_plugin(inode);
		
		if(transformed)
			alternate_streams(tc);
		
		result = grab_tfm_stream(inode, tc, TFM_READ, OUTPUT_STREAM);
		if (result)
			return result;
		assert("edward-910", tfm_cluster_is_set(tc));
		
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
		
		if (memcmp(tfm_stream_data(tc, INPUT_STREAM) + (tc->len - (size_t)CLUSTER_MAGIC_SIZE),
			   magic, (size_t)CLUSTER_MAGIC_SIZE)) {
			printk("edward-156: wrong compression magic %d (should be %d)\n",
			       *((int *)(tfm_stream_data(tc, INPUT_STREAM) + (tc->len - (size_t)CLUSTER_MAGIC_SIZE))), *((int *)magic));
			result = -EIO;
			return result;
		}
		tc->len -= (size_t)CLUSTER_MAGIC_SIZE;

		/* decompress cluster */
		cplug->decompress(get_coa(tc, cplug->h.id), 
				  tfm_stream_data(tc, INPUT_STREAM), tc->len,
				  tfm_stream_data(tc, OUTPUT_STREAM), &dst_len);
		
		/* check length */
		assert("edward-157", dst_len == fsize_to_count(clust, inode));
		tc->len = dst_len;
		transformed = 1;
	}
	if (!transformed)
		alternate_streams(tc);
	return result;
}

/* plugin->read() :
 * generic_file_read()
 * All key offsets don't make sense in traditional unix semantics unless they
 * represent the beginning of clusters, so the only thing we can do is start
 * right from mapping to the address space (this is precisely what filemap
 * generic method does) */

/* plugin->readpage() */
reiser4_internal int
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
	if (!iplug->s.file.readpage) {
		put_cluster_handle(&clust, TFM_READ);
		return -EINVAL;
	}
	result = iplug->s.file.readpage(&clust, page);

	assert("edward-64", ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
	/* if page has jnode - that jnode is mapped
	   assert("edward-65", ergo(result == 0 && PagePrivate(page),
	   jnode_mapped(jprivate(page))));
	*/
	put_cluster_handle(&clust, TFM_READ);
	return result;
}

/* plugin->readpages() */
reiser4_internal void
readpages_cryptcompress(struct file *file, struct address_space *mapping,
			struct list_head *pages)
{
	item_plugin *iplug;

	iplug = item_plugin_by_id(CTAIL_ID);
	iplug->s.file.readpages(file, mapping, pages);
	return;
}

static void
set_cluster_pages_dirty(reiser4_cluster_t * clust)
{
	int i;
	struct page * pg;

	for (i=0; i < clust->nr_pages; i++) {

		pg = clust->pages[i];

		assert("edward-968", pg != NULL);

		lock_page(pg);
		
		set_page_dirty_internal(pg, 0);
		SetPageUptodate(pg);
		if (!PageReferenced(pg))
			SetPageReferenced(pg);
		mark_page_accessed(pg);
		
		unlock_page(pg);
	}
}

/* . reserve space for a disk cluster if its jnode is not dirty;
   . update set of pages referenced by this jnode
   . update jnode's counter of referenced pages (excluding first one)
*/
static void
make_cluster_jnode_dirty_locked(reiser4_cluster_t * clust,
				jnode * node, struct inode * inode)
{
	int i;
	int count;
	
	assert("edward-221", node != NULL);
	assert("edward-971", clust->reserved == 1);
	assert("edward-1028", spin_jnode_is_locked(node));
	assert("edward-972", node->page_count < inode_cluster_pages(inode));
	
	if (jnode_is_dirty(node)) {
		/* there are >= 1 pages already referenced by this jnode */
		assert("edward-973", count_to_nrpages(fsize_to_count(clust, inode)));
		count = count_to_nrpages(fsize_to_count(clust, inode)) - 1;
		/* space for the disk cluster is already reserved */
		free_reserved4cluster(inode, clust);
	}
	else {
		/* there is only one page referenced by this jnode */
		count = 0;
		jnode_make_dirty_locked(node);
	}
	
	assert("edward-974", count == node->page_count);
	
	if (clust_to_off(clust->index, inode) + clust->off + clust->count > inode->i_size)
		INODE_SET_FIELD(inode,
				i_size, 
				clust_to_off(clust->index, inode) + clust->off + clust->count);
	for (i = 0; i <= count; i++) {
		assert("edward-975", clust->pages[i]);
		assert("edward-976", count < inode_cluster_size(inode));
		assert("edward-977", PageDirty(clust->pages[i]));
		
		page_cache_release(clust->pages[i]);
	}

#if REISER4_DEBUG
	node->page_count = max_count(node->page_count, clust->nr_pages - 1);
#endif
	jput(node);
	return;
}

/* This is the interface to capture cluster nodes via their struct page reference.
   Any two blocks of the same cluster contain dependent modification and should
   commit at the same time */
static int
try_capture_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result = 0;
	jnode * node;
	
	assert("edward-1029", clust != NULL);
	assert("edward-1030", clust->reserved == 1);
	assert("edward-1031", clust->nr_pages != 0);
	assert("edward-1032", clust->pages != NULL);
	assert("edward-1033", clust->pages[0] != NULL);
	
	node = jprivate(clust->pages[0]);
	
	assert("edward-1035", node != NULL);

	LOCK_JNODE(node);
	result = try_capture(node, ZNODE_WRITE_LOCK, 0/* not non-blocking */, 0 /* no can_coc */);
	if (result) {
		assert("edward-1034", 0);
		UNLOCK_JNODE(node);
		return result;
	}
	make_cluster_jnode_dirty_locked(clust, node, inode);
	UNLOCK_JNODE(node);
	
	return 0;
}

reiser4_internal jnode *
jnode_of_page_cluster(reiser4_cluster_t * clust)
{
	assert("edward-916", clust != NULL);
	assert("edward-917", clust->pages != NULL);
	assert("edward-918", clust->nr_pages != 0);	

	return jnode_of_page(clust->pages[0]);
}

/* Collect unlocked cluster pages and jnode */
static int
grab_cache_cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	jnode * node = NULL;
	
	assert("edward-182", clust != NULL);
	assert("edward-183", clust->pages != NULL);
	assert("edward-437", clust->nr_pages != 0);
	assert("edward-184", 0 < clust->nr_pages <= inode_cluster_pages(inode));

	for (i = 0; i < clust->nr_pages; i++) {
		
		clust->pages[i] = grab_cache_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		if (!clust->pages[i]) {
			result = RETERR(-ENOMEM);
			break;
		}
		if (i == 0) {
			node = jnode_of_page(clust->pages[i]);
			unlock_page(clust->pages[i]);
			if (IS_ERR(node)) {
				result = PTR_ERR(node);
				break;
			}
			assert("edward-919", node);
			continue;
		}
		unlock_page(clust->pages[i]);
	}
	if (result) {
		while(i)
			page_cache_release(clust->pages[--i]);
		if (node && !IS_ERR(node))
			jput(node);
		return result;
	}
	assert("edward-920", jprivate(clust->pages[0]));
	LOCK_JNODE(node);
	JF_SET(node, JNODE_CLUSTER_PAGE);
	UNLOCK_JNODE(node);
	return 0;
}

/* collect unlocked cluster pages */
reiser4_internal int
grab_cluster_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;

	assert("edward-787", clust != NULL);
	assert("edward-788", clust->pages != NULL);
	assert("edward-789", clust->nr_pages != 0);
	assert("edward-790", 0 < clust->nr_pages <= inode_cluster_pages(inode));

	for (i = 0; i < clust->nr_pages; i++) {
		clust->pages[i] = grab_cache_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		if (!clust->pages[i]) {
			result = RETERR(-ENOMEM);
			break;
		}
		unlock_page(clust->pages[i]);
	}
	if (result)
		while(i) 
			page_cache_release(clust->pages[--i]);
	return result;
}

UNUSED_ARG static void
set_cluster_unlinked(reiser4_cluster_t * clust, struct inode * inode)
{
	jnode * node;

	node = jprivate(clust->pages[0]);

	assert("edward-640", node);

	LOCK_JNODE(node);
	JF_SET(node, JNODE_NEW);
	UNLOCK_JNODE(node);
}

/* put cluster pages */
reiser4_internal void
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
release_cluster_pages_and_jnode(reiser4_cluster_t * clust)
{
	jnode * node;
	
	assert("edward-445", clust != NULL);
	assert("edward-922", clust->pages != NULL);
	assert("edward-446", clust->pages[0] != NULL);

	node = jprivate(clust->pages[0]);
	
	assert("edward-447", node != NULL);

	release_cluster_pages(clust, 0);
	
	jput(node);
}

/* debugging purposes */
#if REISER4_DEBUG
reiser4_internal int
cluster_invariant(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-279", clust != NULL);

	return (clust->pages != NULL &&
		clust->off < inode_cluster_size(inode) &&
		ergo(clust->delta != 0, clust->stat == HOLE_CLUSTER) &&
		clust->off + clust->count + clust->delta <= inode_cluster_size(inode));
}
#endif

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
			break;
		case DATA_CLUSTER:
			/* keep immovability, off+count+delta=inv */
			clust->stat = DATA_CLUSTER;
			clust->off = clust->off + clust->count;
			clust->count = clust->delta;
			clust->delta = 0;
			break;
		default:
			impossible ("edward-282", "wrong next cluster status");
		}
		break;
	default:
		impossible ("edward-283", "wrong current cluster status");
	}
	clust->reserved = 0;
}

static int
update_sd_cryptcompress(struct inode *inode)
{
	int result = 0;
	int old_grabbed;
	reiser4_context *ctx = get_current_context();
	reiser4_super_info_data * sbinfo = get_super_private(ctx->super);

	assert("edward-978", schedulable());

	old_grabbed = ctx->grabbed_blocks;
	
	result = reiser4_grab_space_force(/* one for stat data update */
					  estimate_update_common(inode),
					  BA_CAN_COMMIT);
	assert("edward-979", !result);
	
	if (result)
		return result;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	result = reiser4_update_sd(inode);
	
	grabbed2free(ctx, sbinfo, ctx->grabbed_blocks - old_grabbed);
	return result;
}

reiser4_internal void
uncapture_cluster_jnode(jnode *node)
{
	txn_atom *atom;
	
	assert("edward-1023", spin_jnode_is_locked(node));
	
	/*jnode_make_clean(node);*/
	atom = jnode_get_atom(node);
	if (atom == NULL) {
		assert("jmacd-7111", !jnode_is_dirty(node));
		UNLOCK_JNODE (node);
		return;
	}

	uncapture_block(node);
	UNLOCK_ATOM(atom);
	jput(node);
}

/* . first, make cluster's jnode clean (the order is essential to
     avoid synchronization issues)
   . prepare input tfm-stream that we should transform if at least
     one attached page is dirty.
*/

reiser4_internal int
flush_cluster_pages(reiser4_cluster_t * clust, jnode * node,
struct inode * inode)
{
	int i;
	int result = 0;
	int do_transform = 0;
	tfm_cluster_t * tc;
	
	assert("edward-980", node != NULL);
	assert("edward-236", inode != NULL);
	assert("edward-237", clust != NULL);
	assert("edward-238", clust->off == 0);
	assert("edward-239", clust->count == 0);	
	assert("edward-240", clust->delta == 0);
	assert("edward-241", schedulable());
	assert("edward-718", crc_inode_ok(inode));
	
	tc = &clust->tc;

	LOCK_JNODE(node);
	
	if (!jnode_is_dirty(node)) {

		assert("edward-981", node->page_count == 0);
		warning("edward-982", "flush_cluster_pages: jnode is not dirty "
			"clust %lu, inode %llu\n", 
			clust->index, (unsigned long long)get_inode_oid(inode));

		/* race with another flush */
		UNLOCK_JNODE(node);
		return -E_REPEAT;
	}
	
	tc->len = fsize_to_count(clust, inode);
	clust->nr_pages = count_to_nrpages(tc->len);
	
	assert("edward-983", clust->nr_pages == node->page_count + 1);
#if REISER4_DEBUG
	node->page_count = 0;
#endif	
	cluster_reserved2grabbed(estimate_insert_cluster(inode, 0));
	uncapture_cluster_jnode(node);
	
	UNLOCK_JNODE(node);
	
	result = grab_tfm_stream(inode, tc, TFM_WRITE, INPUT_STREAM);
	if (result)
		return result;
	
	for (i = 0; i < clust->nr_pages; i++){
		struct page * page;
		char * data;
		
		page = find_trylock_page(inode->i_mapping, 
				     clust_to_pg(clust->index, inode) + i);
		
		assert("edward-242", page != NULL);
		assert("edward-984", PageLocked(page));
		
		if (!PageDirty(page))
			warning("edward-985", "Page of index %lu (inode %llu)"
				" is already flushed\n", page->index,
				(unsigned long long)get_inode_oid(inode));
		else {
			reiser4_clear_page_dirty(page);
			do_transform = 1;
		}
		data = kmap(page);
		
		assert("edward-986", off_to_pgcount(tc->len, i) != 0);

		xmemcpy(tfm_stream_data(tc, INPUT_STREAM) + pg_to_off(i), 
			data, off_to_pgcount(tc->len, i));
		kunmap(page);
		unlock_page(page);
		if (i)
			/* do not touch jnode's page */
			page_cache_release(page);
	}
	if (!do_transform) {
		/* rare case: all the attached pages are clean */
		warning("edward-987", "Nothing to update in disk cluster"
			" (index %lu, inode %llu\n)",
			clust->index, (unsigned long long)get_inode_oid(inode));
		result = -E_REPEAT;
	}
	return result;
}

reiser4_internal void
set_hint_cluster(struct inode * inode, hint_t * hint, unsigned long index, znode_lock_mode mode)
{
	reiser4_key key;
	assert("edward-722", crc_inode_ok(inode));
	assert("edward-723", inode_file_plugin(inode) == file_plugin_by_id(CRC_FILE_PLUGIN_ID));

	inode_file_plugin(inode)->key_by_inode(inode, clust_to_off(index, inode), &key);

	seal_init(&hint->seal, &hint->coord.base_coord, &key);
	hint->offset = get_key_offset(&key);
	hint->level = znode_get_level(hint->coord.base_coord.node);
	hint->mode = mode;
}

static int
balance_dirty_page_cluster(reiser4_cluster_t * clust, loff_t off, loff_t to_file)
{
	int result;
	struct inode * inode;
	
	assert("edward-724", clust->file != NULL);
	
	inode = clust->file->f_dentry->d_inode;
	
	assert("edward-725", crc_inode_ok(inode));
	
	/* set next window params */
	update_cluster(inode, clust, off, to_file);
	set_hint_cluster(inode, clust->hint, clust->index, ZNODE_WRITE_LOCK);
	clust->hint->coord.valid = 0;
	longterm_unlock_znode(clust->hint->coord.lh);
	result = update_sd_cryptcompress(inode);
	assert("edward-988", !result);
	if (result)
		return result;
	assert("edward-726", clust->hint->coord.lh->owner == NULL);
	//       atomic_inc(&inode->i_count);
	reiser4_throttle_write(inode);
	
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
	assert ("edward-989", clust->reserved == 1);
	assert ("edward-727", crc_inode_ok(inode));
	assert ("edward-192", cluster_invariant(clust, inode));
	assert ("edward-201", clust->stat == HOLE_CLUSTER);

	if ((clust->off == 0 && clust->count == inode_cluster_size(inode)) /* fake cluster */ ||
	    (clust->count == 0) /* nothing to write */) {
		free_reserved4cluster(inode, clust);
		update_cluster(inode, clust, file_off, to_file);
		return 0;
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

		set_cluster_pages_dirty(clust);
		result = try_capture_cluster(clust, inode);
		if (result)
			return result;
		result = balance_dirty_page_cluster(clust, file_off, to_file);
	}
	else 
		update_cluster(inode, clust, file_off, to_file);
	return result;
}

/*
  The main disk search procedure for cryptcompress plugins, which
  . scans all items of disk cluster
  . maybe reads each one (if @read != 0)
  . maybe makes its znode dirty  (if @write != 0)
*/
reiser4_internal int
find_cluster(reiser4_cluster_t * clust,
	     struct inode * inode,
	     int read,
	     int write)
{
	flow_t f;
	hint_t * hint;
	int result;
	unsigned long cl_idx;
	ra_info_t ra_info;
	file_plugin * fplug;
	item_plugin * iplug;
	tfm_cluster_t * tc;

	assert("edward-138", clust != NULL);
	assert("edward-728", clust->hint != NULL);
	assert("edward-225", read || write);
	assert("edward-226", schedulable());
	assert("edward-137", inode != NULL);
	assert("edward-729", crc_inode_ok(inode));
	assert("edward-474", get_current_context()->grabbed_blocks == 0);

	hint = clust->hint;
	cl_idx = clust->index;
	fplug = inode_file_plugin(inode);

	tc = &clust->tc;

	assert("edward-462", !tfm_cluster_is_uptodate(tc));
	assert("edward-461", ergo(read, tfm_stream_is_set(tc, INPUT_STREAM)));

	/* build flow for the cluster */
	fplug->flow_by_inode(inode, 
			     (tfm_stream_is_set(tc, INPUT_STREAM) ? tfm_stream_data(tc, INPUT_STREAM) : 0),
			     0 /* kernel space */,
			     inode_scaled_cluster_size(inode),
			     clust_to_off(cl_idx, inode), READ_OP, &f);
	if (write) {
		/* reserve for flush to make dirty all the leaf nodes
		   which contain disk cluster */
		result = reiser4_grab_space_force(estimate_disk_cluster(inode), BA_CAN_COMMIT);
		assert("edward-990", !result);
		if (result)
			goto out2;
	}
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));

	while (f.length) {
		result = find_cluster_item(hint, &f.key, 1 /* check key */, (write ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK), NULL, FIND_EXACT);
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
			goto ok;
		case CBK_COORD_FOUND:
			assert("edward-148", hint->coord.base_coord.between == AT_UNIT);
			assert("edward-460", hint->coord.base_coord.unit_pos == 0);

			coord_clear_iplug(&hint->coord.base_coord);
			result = zload_ra(hint->coord.base_coord.node, &ra_info);
			if (unlikely(result))
				goto out2;
			iplug = item_plugin_by_coord(&hint->coord.base_coord);
			assert("edward-147", item_plugin_by_coord(&hint->coord.base_coord) == item_plugin_by_id(CTAIL_ID));
			
			result = iplug->s.file.read(NULL, &f, hint);
			if (result)
				goto out;
			if (write) {
				znode_make_dirty(hint->coord.base_coord.node);
				znode_set_convertible(hint->coord.base_coord.node);
			}
			crc_validate_extended_coord(&hint->coord, get_key_offset(&f.key));
			zrelse(hint->coord.base_coord.node);
			break;
		default:
			goto out2;
		}
	}
 ok:
	/* at least one item was found  */
	/* NOTE-EDWARD: Callers should handle the case when disk cluster is incomplete (-EIO) */
	tc->len = inode_scaled_cluster_size(inode) - f.length;
	/* set hint for next cluster */
	set_hint_cluster(inode, clust->hint, clust->index + 1, write ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK);
	all_grabbed2free();
	return 0;
 out:
	zrelse(hint->coord.base_coord.node);
 out2:
	all_grabbed2free();
	return result;
}

reiser4_internal int
get_disk_cluster_locked(reiser4_cluster_t * clust, znode_lock_mode lock_mode)
{
	reiser4_key key;
	ra_info_t ra_info;
	struct inode * inode;

	assert("edward-730", schedulable());
	assert("edward-731", clust != NULL);
	assert("edward-732", clust->file != NULL);

	inode = clust->file->f_dentry->d_inode;
	key_by_inode_cryptcompress(inode, clust_to_off(clust->index, inode), &key);
	ra_info.key_to_stop = key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));

	return find_cluster_item(clust->hint, &key, 0 /* don't check key */, lock_mode, NULL, FIND_MAX_NOT_MORE_THAN);
}

/* Read before write.
   If success, hint contains locked position before(after) disk cluster.  
   We don't take an interest in how much bytes was written when error occures */
static int
read_some_cluster_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	unsigned to_read;
	item_plugin * iplug;

	iplug = item_plugin_by_id(CTAIL_ID);

	assert("edward-733", get_current_context()->grabbed_blocks == 0);
	assert("edward-924", !tfm_cluster_is_uptodate(&clust->tc));

	if (new_cluster(clust, inode)) {
		/* new cluster, nothing to read, but anyway find a position in the tree */
		assert("edward-734", schedulable());
		assert("edward-735", clust->hint->coord.lh->owner == NULL);

		/* since we read before write, take a write lock */
		result = get_disk_cluster_locked(clust, ZNODE_WRITE_LOCK);
		assert("edward-991", !cbk_errored(result));
		if (cbk_errored(result))
			return result;
		assert("edward-736", clust->hint->coord.base_coord.node == clust->hint->coord.lh->node);
		return 0;
	}
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

		if (!tfm_cluster_is_uptodate(&clust->tc)) {
			/* read cluster and mark its znodes dirty */
			result = ctail_read_cluster(clust, inode, 1 /* write */);
			assert("edward-992", !result);
			if (result)
				goto out;
			assert("edward-925", tfm_cluster_is_uptodate(&clust->tc));	
		}
		lock_page(pg);
		result =  do_readpage_ctail(clust, pg);
		unlock_page(pg);
		assert("edward-993", !result);
		if (result) {
			impossible("edward-219", "do_readpage_ctail returned crap");
			goto out;
		}
	}
	if (!tfm_cluster_is_uptodate(&clust->tc)) {
		/* disk cluster unclaimed, but we need to make its znodes dirty
		   to make flush rewrite its content */
		result = find_cluster(clust, inode, 0 /* do not read */, 1 /*write */);
		assert("edward-994", !cbk_errored(result));
		if (!cbk_errored(result))
			result = 0;
	}
 out:
	tfm_cluster_clr_uptodate(&clust->tc);
	return result;
}

static int
crc_make_unprepped_cluster (reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-737", clust != NULL);
	assert("edward-738", inode != NULL);
	assert("edward-739", crc_inode_ok(inode));

	return ctail_make_unprepped_cluster(clust, inode);
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
	assert("edward-741", crc_inode_ok(inode));
	assert("edward-280", cluster_invariant(clust, inode));

	o_c_d = clust->count + clust->delta;

	if (nr_pages != NULL) {
		assert("edward-422", *nr_pages <= inode_cluster_pages(inode));
		clust->nr_pages = *nr_pages;
	}
	else {  /* wasn't advised, guess by frame */
		assert("edward-740", clust->pages != NULL);
		set_nrpages_by_frame(clust);
	}
	if(!clust->nr_pages)
		/* do nothing */
		return 0;
	/* collect unlocked pages and jnodes */
	result = grab_cache_cluster(inode, clust);
	assert("edward-995", !result);
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
		if (inode_get_crypto(inode) && cplug->align_cluster) {
			assert("edward-926", 0);
			cplug->align_cluster(data + off, off, PAGE_CACHE_SIZE);
		}
		else
			xmemset(data + off, 0, PAGE_CACHE_SIZE - off);
		kunmap_atomic(data, KM_USER0);
		unlock_page(pg);
	}
	result = reserve4cluster(inode, clust, msg);

	assert("edward-996", !result);

	if (result)
		goto exit1;
	result = read_some_cluster_pages(inode, clust);
	assert("edward-997", !result);
	if (result)
		goto exit2;

	assert("edward-742", znode_is_write_locked(clust->hint->coord.base_coord.node));

	switch (clust->stat) {
	case HOLE_CLUSTER:
		result = write_hole(inode, clust, file_off, to_file);
		break;
	case DATA_CLUSTER:
		if (!new_cluster(clust, inode))
			break;
	case FAKE_CLUSTER:
		/* page cluster is unprepped */
#ifdef HANDLE_VIA_FLUSH_SCAN
		set_cluster_unlinked(clust, inode);
#else
		/* handling via flush squalloc */
		result = crc_make_unprepped_cluster(clust, inode);
		assert("edward-743", crc_inode_ok(inode));
		assert("edward-744", znode_is_write_locked(clust->hint->coord.lh->node));
		assert("edward-745", znode_is_dirty(clust->hint->coord.lh->node));
#endif
		break;
	default:
		impossible("edward-746", "bad cluster status");
	}
	if (!result) {
		assert("edward-927", jprivate(clust->pages[0]));
		return 0;
	}
 exit2:
	free_reserved4cluster(inode, clust);
 exit1:
	page_cache_release(clust->pages[0]);
	release_cluster_pages_and_jnode(clust);
	
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

static int
set_cluster_params(struct inode * inode, reiser4_cluster_t * clust, flow_t * f, loff_t file_off)
{
	int result;
	assert("edward-197", clust != NULL);
	assert("edward-198", inode != NULL);
	assert("edward-747", reiser4_inode_data(inode)->cluster_shift <= MAX_CLUSTER_SHIFT);

	result = alloc_page_cluster(clust, inode_cluster_pages(inode));
	if (result)
		return result;	
	
	if (file_off > inode->i_size) {
		/* Uhmm, hole in crypto-file... */
		loff_t hole_size;
		hole_size = file_off - inode->i_size;

		printk("edward-176, Warning: Hole of size %llu in "
		       "cryptocompressed file (inode %llu, offset %llu) \n",
		       hole_size, (unsigned long long)get_inode_oid(inode), file_off);

		clust_by_offs(clust, inode, inode->i_size, file_off);
		clust->stat = HOLE_CLUSTER;
		if (clust->off + hole_size < inode_cluster_size(inode))
			/* besides there is also user's data to write to this cluster */
			clust->delta = min_count(inode_cluster_size(inode) - (clust->off + clust->count), f->length);
		return 0;
	}
	clust_by_offs(clust, inode, file_off, file_off + f->length);
	clust->stat = DATA_CLUSTER;
	return 0;
}

static void edward_break(void){;}

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
	hint_t hint;
	lock_handle lh;
	int result = 0;
	size_t to_write = 0;
	loff_t file_off;
	reiser4_cluster_t clust;
		
	assert("edward-161", schedulable());
	assert("edward-748", crc_inode_ok(inode));
	assert("edward-159", current_blocksize == PAGE_CACHE_SIZE);
	assert("edward-749", reiser4_inode_data(inode)->cluster_shift <= MAX_CLUSTER_SHIFT);
	
	result = load_file_hint(file, &hint);
	if (result)
		return result;
	init_lh(&lh);
	hint.coord.lh = &lh;
	//coord_init_invalid(&hint.coord.base_coord, 0);
	
	result = flow_by_inode_cryptcompress(inode, (char *)buf, 1 /* user space */, count, pos, WRITE_OP, &f);
	if (result)
		goto out;
	to_write = f.length;

        /* current write position in file */
	file_off = pos;
	reiser4_cluster_init(&clust);
	clust.file = file;
	clust.hint = &hint;
			
	result = set_cluster_params(inode, &clust, &f, file_off);
	if (result)
		goto out;
	if (inode->i_ino == 76906)
		edward_break();
	
	if (next_cluster_stat(&clust) == HOLE_CLUSTER) {
		result = prepare_cluster(inode, file_off, f.length, NULL, &clust, "write cryptcompress hole");
		if (result)
			goto out;
	}
	do {
		char *src;
		unsigned page_off, page_count;
		
		assert("edward-750", schedulable());
		
		result = prepare_cluster(inode, file_off, f.length, NULL, &clust, "write cryptcompress flow");
		if (result)
			goto out;
		
		assert("edward-751", crc_inode_ok(inode));
		assert("edward-204", clust.stat == DATA_CLUSTER);
		assert("edward-752", znode_is_write_locked(hint.coord.base_coord.node));
		
		/* set write position in page */
		page_off = off_to_pgoff(clust.off);
		
                /* copy user's data to cluster pages */
		for (i = off_to_pg(clust.off), src = f.data; i < count_to_nrpages(clust.off + clust.count); i++, src += page_count) {
			page_count = min_count(PAGE_CACHE_SIZE - page_off, off_to_pgcount(clust.off + clust.count, i));
			
			assert("edward-287", clust.pages[i] != NULL);
			
			lock_page(clust.pages[i]);
			result = __copy_from_user((char *)kmap(clust.pages[i]) + page_off, src, page_count);
			kunmap(clust.pages[i]);
			if (unlikely(result)) {
				unlock_page(clust.pages[i]);
				result = -EFAULT;
				goto err3;
			}
			unlock_page(clust.pages[i]);
			page_off = 0;
		}
		assert("edward-753", crc_inode_ok(inode));
		
		set_cluster_pages_dirty(&clust);
		
		result = try_capture_cluster(&clust, inode);
		if (result)
			goto err2;
		
		assert("edward-998", f.user == 1);
		
		move_flow_forward(&f, clust.count);
		
		/* disk cluster may be already clean at this point */
		
		/* . update cluster
		   . set hint for new offset
		   . unlock znode
		   . update inode
		   . balance dirty pages
		*/
		result = balance_dirty_page_cluster(&clust, 0, f.length);
		if(result)
			goto err1;
		assert("edward-755", hint.coord.lh->owner == NULL);
		continue;
	err3:
		page_cache_release(clust.pages[0]);
	err2:
		release_cluster_pages_and_jnode(&clust);
	err1:
		if (clust.reserved)
			free_reserved4cluster(inode, &clust);
		break;
	} while (f.length);
 out:	
	done_lh(&lh);	
 	if (result == -EEXIST)
		printk("write returns EEXIST!\n");

	put_cluster_handle(&clust, TFM_READ);
	save_file_hint(file, &hint);
	if (buf) {
		/* if nothing were written - there must be an error */
		assert("edward-195", ergo((to_write == f.length), result < 0));
		return (to_write - f.length) ? (to_write - f.length) : result;
	}
	return result;
}

static ssize_t
write_crc_file(struct file * file, /* file to write to */
	   struct inode *inode, /* inode */
	   const char *buf, /* address of user-space buffer */
	   size_t count, /* number of bytes to write */
	   loff_t * off /* position to write which */)
{

	int result;
	loff_t pos;
	ssize_t written;

	assert("edward-196", crc_inode_ok(inode));

	result = generic_write_checks(file, off, &count, 0);
	if (unlikely(result != 0))
		return result;

	if (unlikely(count == 0))
		return 0;

        /* FIXME-EDWARD: other UNIX features */

	pos = *off;
	written = write_cryptcompress_flow(file, inode, (char *)buf, count, pos);
	if (written < 0) {
		if (written == -EEXIST)
			printk("write_crc_file returns EEXIST!\n");
		return written;
	}

        /* update position in a file */
	*off = pos + written;
	/* return number of written bytes */
	return written;
}

/* plugin->u.file.write */
reiser4_internal ssize_t
write_cryptcompress(struct file * file, /* file to write to */
		    const char *buf, /* address of user-space buffer */
		    size_t count, /* number of bytes to write */
		    loff_t * off /* position to write which */)
{
	ssize_t result;
	struct inode *inode;

	inode = file->f_dentry->d_inode;

	down(&inode->i_sem);

	result = write_crc_file(file, inode, buf, count, off);

	up(&inode->i_sem);
	return result;
}

/* Helper function for cryptcompress_truncate */
static int
find_object_size(struct inode *inode, loff_t * size)
{
	int result;
	reiser4_key key;
	hint_t hint;
	coord_t *coord;
	lock_handle lh;
	item_plugin *iplug;
	file_plugin *fplug = inode_file_plugin(inode);

	assert("edward-95", crc_inode_ok(inode));

	init_lh(&lh);
	hint_init_zero(&hint);
	hint.coord.lh = &lh;
	
	fplug->key_by_inode(inode, get_key_offset(max_key()), &key);

	/* find the last item of this object */
	result = find_cluster_item(&hint, &key, 0, ZNODE_READ_LOCK, 0/* ra_info */, FIND_MAX_NOT_MORE_THAN);
	if (result == CBK_COORD_NOTFOUND) {
		/* object doesn't have any item */
		done_lh(&lh);
		*size = 0;
		return 0;
	}
	if (result != CBK_COORD_FOUND) {
		/* error occurred */
		done_lh(&lh);
		return result;
	}
	coord = &hint.coord.base_coord;

	/* there is at least one item */
	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (unlikely(result)) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(coord);
	assert("edward-277", iplug == item_plugin_by_id(CTAIL_ID));
	assert("edward-659", cluster_shift_by_coord(coord) == inode_cluster_shift(inode));

	iplug->s.file.append_key(coord, &key);

	*size = get_key_offset(&key);

	zrelse(coord->node);
	done_lh(&lh);

	return 0;
}

#if 0
UNUSED_ARG static int
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
		result = reserve_cut_iteration(tree_by_inode(inode));
		if (result)
			break;

		result = cut_tree_object(current_tree, &from_key, &to_key,
					 &smallest_removed, inode, 0);
		if (result == -E_REPEAT) {
			/* -E_REPEAT is a signal to interrupt a long file truncation process */
			/* FIXME(Zam) cut_tree does not support that signaling.*/
			result = update_inode_cryptcompress
				(inode, get_key_offset(&smallest_removed), 1, 1, update_sd);
			if (result)
				break;

			all_grabbed2free();
			reiser4_release_reserved(inode->i_sb);

			txn_restart_current();
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
#endif

/* The following two procedures are called when truncate decided
   to deal with real items */
static int
cryptcompress_append_hole(struct inode * inode, loff_t new_size)
{
	return write_cryptcompress_flow(0, inode, 0, 0, new_size);
}

reiser4_internal void
truncate_cluster(struct inode * inode, pgoff_t start, long count)
{
	loff_t from, to;

	from = ((loff_t)start) << PAGE_CACHE_SHIFT;
	to = from + (((loff_t)count) << PAGE_CACHE_SHIFT) - 1;
	truncate_inode_pages_range(inode->i_mapping, from, to);
	truncate_jnodes_range(inode, start, 1);
}

static int
shorten_cryptcompress(struct inode * inode, loff_t new_size, int update_sd,
		      loff_t asize)
{
	int result;
	pgoff_t start, count;
	loff_t old_size;
	char * kaddr;
	unsigned pgoff;
	reiser4_cluster_t clust;
	crypto_plugin * cplug;

	assert("edward-290", inode->i_size > new_size);
	assert("edward-756", crc_inode_ok(inode));

	pgoff = 0;
	start = count = 0;
	old_size = inode->i_size;
	cplug = inode_crypto_plugin(inode);

	reiser4_cluster_init(&clust);

	result = cut_file_items(inode, new_size, update_sd, asize, 0);

	if(result)
		return result;

	assert("edward-660", ergo(!new_size,
				  (reiser4_inode_data(inode)->anonymous_eflushed == 0 &&
				   reiser4_inode_data(inode)->captured_eflushed == 0)));

	if (!off_to_cloff(new_size, inode))
		/* truncated to cluster boundary (1) */
		return 0;

	result = alloc_page_cluster(&clust, inode_cluster_pages(inode));
	if (result)
		return result;
	/* set frame */
	clust_by_offs(&clust, inode, new_size, old_size);
	
	/* read the whole cluster */
	result = prepare_cluster(inode, 0, 0, NULL, &clust, "shorten cryptcompress");
	if (result) 
		goto exit;
	/* truncate last cluster pages and jnodes */
	assert("edward-294", clust.stat == DATA_CLUSTER);
	assert("edward-661", clust.off > 0);
	assert("edward-999", clust.reserved == 1);
	
	pgoff = off_to_pgoff(clust.off);

        /* reduced idx of the first page to release */
	start = off_to_pg(clust.off - 1) + 1;
	/* number of pages to release and truncate */
	count = clust.nr_pages - start;

	/* release last pages which won't participate in flush */
	release_cluster_pages(&clust, start);
	/* truncate the pages above, also don't forget about jnodes */
	truncate_cluster(inode, clust_to_pg(clust.index, inode) + start, count);
	/* update number of cluster pages */
	clust.nr_pages = start;

	/* align last non-truncated page */
	lock_page(clust.pages[clust.nr_pages - 1]);
	kaddr = kmap_atomic(clust.pages[clust.nr_pages - 1], KM_USER0);

	if (inode_get_crypto(inode) && cplug->align_cluster)
		cplug->align_cluster(kaddr + pgoff, pgoff, PAGE_CACHE_SIZE);
	else
		xmemset(kaddr + pgoff, 0, PAGE_CACHE_SIZE - pgoff);
	unlock_page(clust.pages[clust.nr_pages - 1]);
	
	set_cluster_pages_dirty(&clust);
	result = try_capture_cluster(&clust, inode);
	if (result)
		goto err2;
	
	/* FIXME-EDWARD: Update this using balance dirty cluster pages */
	assert("edward-757", 0 /* don't free reserved4cluster when success */);
	result = update_sd_cryptcompress(inode);
	if(!result)
		goto err1;
	reiser4_throttle_write(inode);
	goto exit;
 err2:
	page_cache_release(clust.pages[0]);
 err1:
	release_cluster_pages_and_jnode(&clust);
	
	if (clust.reserved)
	free_reserved4cluster(inode, &clust);
 exit:
	put_cluster_handle(&clust, TFM_READ);
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
	loff_t asize; /* actual size */

	result = find_object_size(inode, &asize);

	if (result)
		return result;
	if (!asize ||
	    /* no items */
	    off_to_clust(asize, inode) < off_to_clust(new_size, inode)
	    /* truncating up to fake cluster boundary */) {
		/* do not touch items */
		assert("edward-662", !off_to_cloff(new_size, inode));

		INODE_SET_FIELD(inode, i_size, asize);
		truncate_cluster(inode, size_to_next_pg(new_size),
				 size_to_pg(old_size) - size_to_next_pg(new_size) + 1);
		assert("edward-663", ergo(!new_size,
					  reiser4_inode_data(inode)->anonymous_eflushed == 0 &&
					  reiser4_inode_data(inode)->captured_eflushed == 0));

		if (update_sd) {
			result = setattr_reserve_common(tree_by_inode(inode));
			if (!result)
				result = update_sd_cryptcompress(inode);
			all_grabbed2free();
		}
		return result;
	}
	result = (old_size < new_size ? cryptcompress_append_hole(inode, new_size) :
		  shorten_cryptcompress(inode, new_size, update_sd, asize));
	return result;
}

/* plugin->u.file.truncate */
reiser4_internal int
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

	result = prepare_cluster(page->mapping->host, 0, 0, &nrpages, &clust, "cryptcompress_writepage");
	if(result)
		goto exit;

	set_cluster_pages_dirty(&clust); 
	result = try_capture_cluster(&clust, inode);
	if (result) {
		free_reserved4cluster(inode, &clust);
		goto exit;
	}
	lock_page(page);
 exit:
	reiser4_kfree(clust.pages);
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
reiser4_internal int
capture_cryptcompress(struct inode *inode, const struct writeback_control *wbc, long *captured)
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
	return RETERR(-EBUSY);
}

static inline void
validate_crc_extended_coord(uf_coord_t *uf_coord, loff_t offset)
{
	assert("edward-418", uf_coord->valid == 0);
	assert("edward-419", item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension);

	/* FIXME: */
	item_body_by_coord(&uf_coord->base_coord);
	item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension(uf_coord, offset);
}

/* plugin->u.file.mmap */
reiser4_internal int
mmap_cryptcompress(struct file * file, struct vm_area_struct * vma)
{
	return generic_file_mmap(file, vma);
}


/* plugin->u.file.release */
/* plugin->u.file.get_block */
/* This function is used for ->bmap() VFS method in reiser4 address_space_operations */
reiser4_internal int
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
		init_lh(&lh);
		hint_init_zero(&hint);
		hint.coord.lh = &lh;
		result = find_cluster_item(&hint, &key, 0, ZNODE_READ_LOCK, 0, FIND_EXACT);
		if (result != CBK_COORD_FOUND) {
			done_lh(&lh);
			return result;
		}
		result = zload(hint.coord.base_coord.node);
		if (unlikely(result)) {
			done_lh(&lh);
			return result;
		}
		iplug = item_plugin_by_coord(&hint.coord.base_coord);

		assert("edward-421", iplug == item_plugin_by_id(CTAIL_ID));

		if (!hint.coord.valid)
			validate_crc_extended_coord(&hint.coord,
						(loff_t) block << PAGE_CACHE_SHIFT);
		if (iplug->s.file.get_block)
			result = iplug->s.file.get_block(&hint.coord.base_coord, block, bh_result);
		else
			result = RETERR(-EINVAL);

		zrelse(hint.coord.base_coord.node);
		done_lh(&lh);
		return result;
	}
}

/* plugin->u.file.delete */
/* EDWARD-FIXME-HANS: comment is where? 
 VS-FIXME-HANS: how long ago did I ask the above?  Why aren't you supervising this work more closely?  You know he is a junior programmer.... */
reiser4_internal int
delete_cryptcompress(struct inode *inode)
{
	int result;

	assert("edward-429", inode->i_nlink == 0);

	if (inode->i_size) {
		result = cryptcompress_truncate(inode, 0, 0);
		if (result) {
			warning("edward-430", "cannot truncate cryptcompress file  %lli: %i",
				(unsigned long long)get_inode_oid(inode), result);
			return result;
		}
	}
	return delete_object(inode, 0);
}

/* plugin->u.file.init_inode_data */
/* plugin->u.file.owns_item:
   owns_item_common */
/* plugin->u.file.pre_delete */
/* EDWARD-FIXME-HANS: comment is where? */
reiser4_internal int
pre_delete_cryptcompress(struct inode *inode)
{
	return cryptcompress_truncate(inode, 0, 0);
}

/* plugin->u.file.setattr method */
reiser4_internal int
setattr_cryptcompress(struct inode *inode,	/* Object to change attributes */
		      struct iattr *attr /* change description */ )
{
	int result;

	if (attr->ia_valid & ATTR_SIZE) {
		/* EDWARD-FIXME-HANS: VS-FIXME-HANS:
		   Q: this case occurs when? truncate?
		   A: yes

		   Q: If so, why isn't this code in truncate itself instead of here?

		   A: because vfs calls fs's truncate after it has called truncate_inode_pages to get rid of pages
		   corresponding to part of file being truncated. In reiser4 it may cause existence of unallocated
		   extents which do not have jnodes. Flush code does not expect that. Solution of this problem is
		   straightforward. As vfs's truncate is implemented using setattr operation (common implementaion of
		   which calls truncate_inode_pages and fs's truncate in case when size of file changes) - it seems
		   reasonable to have reiser4_setattr which will take care of removing pages, jnodes and extents
		   simultaneously in case of truncate.
		   Q: do you think implementing truncate using setattr is ugly,
		   and vfs needs improving, or is there some sense in which this is a good design? 

		   A: VS-FIXME-HANS: 
		*/

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
		/* FIXME: Edward, please consider calling setattr_common() here */
		result = setattr_reserve_common(tree_by_inode(inode));
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

static int
save_len_cryptcompress_plugin(struct inode * inode, reiser4_plugin * plugin)
{
	assert("edward-457", inode != NULL);
	assert("edward-458", plugin != NULL);
	assert("edward-459", plugin->h.id == CRC_FILE_PLUGIN_ID);
	return 0;
}

reiser4_internal int
load_cryptcompress_plugin(struct inode * inode, reiser4_plugin * plugin, char **area, int *len)
{
	assert("edward-455", inode != NULL);
	assert("edward-456", (reiser4_inode_data(inode)->pset != NULL));

	plugin_set_file(&reiser4_inode_data(inode)->pset, file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	return 0;
}

static int
change_crypto_file(struct inode * inode, reiser4_plugin * plugin)
{
	/* cannot change object plugin of already existing object */
	return RETERR(-EINVAL);
}

struct reiser4_plugin_ops cryptcompress_plugin_ops = {
	.load      = load_cryptcompress_plugin,
	.save_len  = save_len_cryptcompress_plugin,
	.save      = NULL,
	.alignment = 8,
	.change    = change_crypto_file
};

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
