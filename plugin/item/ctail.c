/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* ctails (aka "crypto tails") are items for cryptcompress objects */

/* DESCRIPTION:

Each cryptcompress object is stored on disk as a set of clusters sliced
into ctails.

Internal on-disk structure:

        HEADER   (4)  Here stored disk cluster size in bytes (this is original cluster size
                      translated by ->scale() method of crypto plugin)
	BODY
*/

#include "../../forward.h"
#include "../../debug.h"
#include "../../dformat.h"
#include "../../kassign.h"
#include "../../key.h"
#include "../../coord.h"
#include "item.h"
#include "../node/node.h"
#include "../plugin.h"
#include "../../znode.h"
#include "../../carry.h"
#include "../../tree.h"
#include "../../inode.h"
#include "../../super.h"
#include "../../context.h"
#include "ctail.h"
#include "../../page_cache.h"

#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/pagevec.h>

/* return body of ctail item at @coord */
static ctail_item_format *
formatted_at(const coord_t * coord)
{
	assert("edward-60", coord != NULL);
	return item_body_by_coord(coord);
}

unsigned
cluster_size_by_coord(const coord_t * coord)
{
	return d32tocpu(&formatted_at(coord)->disk_cluster_size);
}

static char *
first_unit(coord_t * coord)
{
	/* FIXME: warning: pointer of type `void *' used in arithmetic */
	return (char *)item_body_by_coord(coord) + sizeof (ctail_item_format);
}

/* plugin->u.item.b.max_key_inside :
   tail_max_key_inside */

/* plugin->u.item.b.can_contain_key :
   tail_can_contain_key */

/* plugin->u.item.b.mergeable
   c-tails of different clusters are not mergeable */
int
mergeable_ctail(const coord_t * p1, const coord_t * p2)
{
	reiser4_key key1, key2;

	assert("edward-61", item_type_by_coord(p1) == ORDINARY_FILE_METADATA_TYPE);
	assert("edward-62", item_id_by_coord(p1) == CTAIL_ID);

	if (item_id_by_coord(p2) != CTAIL_ID) {
		/* second item is of another type */
		return 0;
	}

	item_key_by_coord(p1, &key1);
	item_key_by_coord(p2, &key2);
	if (get_key_locality(&key1) != get_key_locality(&key2) ||
	    get_key_objectid(&key1) != get_key_objectid(&key2) ||
	    get_key_type(&key1) != get_key_type(&key2)) {
		/* items of different objects */
		return 0;
	}
	if ((get_key_offset(&key1) + nr_units_ctail(p1) != get_key_offset(&key2)) ||
	    nr_units_ctail(p1) == cluster_size_by_coord(p1)) {
		/* items of different clusters */
		return 0;
	}
	return 1;
}

/* plugin->u.item.b.nr_units */
pos_in_item_t
nr_units_ctail(const coord_t * coord)
{
	return (item_length_by_coord(coord) - sizeof(formatted_at(coord)->disk_cluster_size));
}

/* plugin->u.item.b.unit_key: 
   tail_unit_key */

/* estimate how much space is needed to insert/paste @data->length bytes
   into ctail at @coord */
int
estimate_ctail(const coord_t * coord /* coord of item */,
	     const reiser4_item_data * data /* parameters for new item */)
{
	if (coord == NULL)
		/* insert */
		return (sizeof(ctail_item_format) + data->length);
	else
		/* paste */
		return data->length;	
}

#if REISER4_DEBUG_OUTPUT
/* ->print() method for this item plugin. */
void
print_ctail(const char *prefix /* prefix to print */ ,
	  coord_t * coord /* coord of item to print */ )
{
	assert("edward-63", prefix != NULL);
	assert("edward-64", coord != NULL);

	if (item_length_by_coord(coord) < (int) sizeof (ctail_item_format)) 
		printk("%s: wrong size: %i < %i\n", prefix, item_length_by_coord(coord), sizeof (ctail_item_format));
	else
		printk("%s: disk cluster size: %i\n", prefix, cluster_size_by_coord(coord));
}
#endif

/* plugin->u.item.b.lookup:
   NULL. (we are looking only for exact keys from item headers) */


/* plugin->u.item.b.check */

/* plugin->u.item.b.paste */
int
paste_ctail(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG)
{
	unsigned old_nr_units;
	
	/* number of units the item had before resizing has been performed */
	old_nr_units = nr_units_ctail(coord) - data->length;

	/* tail items never get pasted in the middle */
	assert("edward-65",
	       (coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
	       (coord->unit_pos == old_nr_units - 1 &&
		coord->between == AFTER_UNIT) ||
	       (coord->unit_pos == 0 && old_nr_units == 0 && coord->between == AT_UNIT));
	
	if (coord->unit_pos == 0)
		/* make space for pasted data when pasting at the beginning of
		   the item */
		xmemmove(first_unit(coord) + data->length, first_unit(coord), old_nr_units);

	if (coord->between == AFTER_UNIT)
		coord->unit_pos++;

	if (data->data) {
		assert("edward-66", data->user == 0 || data->user == 1);
		if (data->user) {
			assert("edward-67", schedulable());
			/* copy from user space */
			__copy_from_user(first_unit(coord) + coord->unit_pos, data->data, (unsigned) data->length);
		} else
			/* copy from kernel space */
			xmemcpy(first_unit(coord) + coord->unit_pos, data->data, (unsigned) data->length);
	} else {
		xmemset(first_unit(coord) + coord->unit_pos, 0, (unsigned) data->length);
	}
	return 0;
}

/* plugin->u.item.b.fast_paste */

/* plugin->u.item.b.can_shift
   number of units is returned via return value, number of bytes via @size. For
   ctail items they coincide */
int
can_shift_ctail(unsigned free_space, coord_t * source,
	       znode * target UNUSED_ARG, shift_direction direction UNUSED_ARG, unsigned *size, unsigned want)
{
	/* make sure that that we do not want to shift more than we have */
	assert("edward-68", want > 0 && want <= nr_units_ctail(source));

	*size = min(want, free_space);
	return *size;
}

/* plugin->u.item.b.copy_units */
void
copy_units_ctail(coord_t * target, coord_t * source,
		unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space UNUSED_ARG)
{
	/* make sure that item @target is expanded already */
	assert("edward-69", nr_units_ctail(target) >= count);
	assert("edward-70", free_space >= count);
	
	if (where_is_free_space == SHIFT_LEFT) {
		/* append item @target with @count first bytes of @source:
		   this restriction came from ordinary tails */
		assert("edward-71", from == 0);

		xmemcpy(first_unit(target) + nr_units_ctail(target) - count, first_unit(source), count);
	} else {
		/* target item is moved to right already */
		reiser4_key key;

		assert("edward-72", nr_units_ctail(source) == from + count);

		xmemcpy(first_unit(target), first_unit(source) + from, count);

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		item_key_by_coord(source, &key);
		set_key_offset(&key, get_key_offset(&key) + from);

		node_plugin_by_node(target->node)->update_item_key(target, &key, 0 /*info */);
	}
}

/* plugin->u.item.b.create_hook
   plugin->u.item.b.kill_hook
   plugin->u.item.b.shift_hook */

/* plugin->u.item.b.cut_units */
int 
cut_units_ctail(coord_t * coord, unsigned *from, unsigned *to,
	       const reiser4_key * from_key UNUSED_ARG,
	       const reiser4_key * to_key UNUSED_ARG, reiser4_key * smallest_removed,
	       void *p UNUSED_ARG)
{ 
	reiser4_key key;
	unsigned count;
	
 	count = *to - *from + 1;
	/* regarless to whether we cut from the beginning or from the end of
	   item - we have nothing to do */
	assert("edward-73", count > 0 && count <= nr_units_ctail(coord));
	assert("edward-74", ergo(*from != 0, *to == coord_last_unit_pos(coord)));

	if (smallest_removed) {
		/* store smallest key removed */
		item_key_by_coord(coord, smallest_removed);
		set_key_offset(smallest_removed, get_key_offset(smallest_removed) + *from);
	}
	if (*from == 0) {
		/* head of item is removed, update item key therefore */
		item_key_by_coord(coord, &key);
		set_key_offset(&key, get_key_offset(&key) + count);
		node_plugin_by_node(coord->node)->update_item_key(coord, &key, 0 /*info */ );
	}

	if (REISER4_DEBUG)
		xmemset(first_unit(coord) + *from, 0, count);
	return count;
}

/* plugin->u.item.b.kill_units :
   ctail_cut_units */

/* plugin->u.item.b.unit_key :
   tail_unit_key */

/* plugin->u.item.s.file.read */
int
read_ctail(struct file *file UNUSED_ARG, flow_t *f, uf_coord_t *uf_coord)
{
	coord_t *coord;

	coord = &uf_coord->base_coord;
	assert("edward-127", f->user == 1);
	assert("edward-128", f->data);
	assert("edward-129", coord && coord->node);
	assert("edward-130", coord_is_existing_unit(coord));
	assert("edward-131", znode_is_rlocked(coord->node));
	assert("edward-132", znode_is_loaded(coord->node));

	/* start read only from the beginning of ctail */
	assert("edward-133", coord->unit_pos == 0);
	/* read only whole ctails */
	assert("edward-134", nr_units_ctail(coord) >= CTAIL_MIN_BODY_SIZE);
	assert("edward-135", item_length_by_coord(coord) <= f->length);
	
	assert("edward-136", schedulable());
	
	memcpy(f->data, (char *)first_unit(coord), (size_t)nr_units_ctail(coord));

	mark_page_accessed(znode_page(coord->node));
	move_flow_forward(f, nr_units_ctail(coord));
	coord->unit_pos --;
	coord->between = AFTER_UNIT;
	return 0;
}

/* read one cluster form disk, decrypt and decompress its data */
int
ctail_read_cluster (reiser4_cluster_t * clust, struct inode * inode)
{
	flow_t f; /* for items collection */
	uf_coord_t uf_coord;
	lock_handle lh;
	int res;
	unsigned long index;
	ra_info_t ra_info;
	file_plugin * fplug;
	item_plugin * iplug;
	crypto_plugin * cr_plug;
	cryptcompress_info_t * info;
	
	assert("edward-137", inode != NULL);
	assert("edward-138", clust != NULL);
	assert("edward-139", clust->buf == NULL);
	assert("edward-140", clust->stat != FAKE_CLUSTER);
	
	fplug = inode_file_plugin(inode);
	iplug = item_plugin_by_id(CTAIL_ID);
	cr_plug = inode_crypto_plugin(inode);
	info = cryptcompress_inode_data(inode);

	assert("edward-141", cr_plug != NULL);
		
	assert("edward-143", info != NULL);
	assert("edward-144", info->expkey != NULL);
	assert("edward-145", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	/* allocate temporary buffer of disk cluster size */
	/* FIXME-EDWARD optimize it for the clusters which represent end of file */
	clust->buf = reiser4_kmalloc(inode_scaled_cluster_size(inode), GFP_KERNEL);
	if (!clust->buf) 
		return -ENOMEM;
	
	index = clust->index;
	
	/* build flow for the cluster */
	fplug->flow_by_inode(inode, clust->buf, 0 /* kernel space */,
			     inode_scaled_cluster_size(inode), index << PAGE_CACHE_SHIFT, READ_OP, &f);
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));
	
	while (f.length) {
		init_lh(&lh);
		res = find_cluster_item(&f.key, &uf_coord.base_coord, &lh, &ra_info);
		switch (res) {
		case CBK_COORD_NOTFOUND:
			if (inode_scaled_offset(inode, index << PAGE_CACHE_SHIFT) == get_key_offset(&f.key)) {
				/* first item not found: hole cluster */
				clust->stat = FAKE_CLUSTER;
				res = 0;
				goto out;
			}
			/* we are outside the cluster, stop search here */
			/* FIXME-EDWARD. Handle the case when cluster is not complete (-EIO) */
			assert("edward-146", f.length != inode_scaled_cluster_size(inode));
			done_lh(&lh);
			goto ok;
		case CBK_COORD_FOUND:
			assert("edward-147", item_plugin_by_coord(&uf_coord.base_coord) == iplug);
			assert("edward-148", uf_coord.base_coord.between != AT_UNIT);
			coord_clear_iplug(&uf_coord.base_coord);
			res = zload_ra(uf_coord.base_coord.node, &ra_info);
			if (unlikely(res))
				goto out;
			res = iplug->s.file.read(NULL, &f, &uf_coord);
			zrelse(uf_coord.base_coord.node);
			if (res) 
				goto out;
			done_lh(&lh);
			break;
		default:
			goto out;
		}
	}
 ok:
	clust->len = inode_scaled_cluster_size(inode) - f.length;
	if (clust->len % cr_plug->blocksize(inode_crypto_stat(inode)->keysize)) {
		res = -EIO;
		goto out2;
	}
	res = process_cluster(clust, inode, READ_OP);
	if (res)
		goto out2;
	return 0;
 out:
	done_lh(&lh);
 out2:
	put_cluster_data(clust, inode);
	return res;
}

/* read one locked page */
int do_readpage_ctail(reiser4_cluster_t * clust, struct page *page)
{
	int ret;
	unsigned char page_idx;
	unsigned to_page;
	struct inode * inode;
	char * data;
	int release = 0;

	assert("edward-212", PageLocked(page));
	
	inode = page->mapping->host;

	if (clust->stat == FAKE_CLUSTER) {
		/* fill page by zeroes */
		char *kaddr = kmap_atomic(page, KM_USER0);
		
		assert("edward-119", clust->buf == NULL);
		
		memset(kaddr, 0, PAGE_CACHE_SIZE);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		SetPageUptodate(page);
		
		ON_TRACE(TRACE_CTAIL, " - hole, OK\n");
		return 0;
	}	
	if (!cluster_is_uptodate(clust)) {
		clust->index = cluster_index_by_page(page, inode);
		reiser4_unlock_page(page);
		ret = ctail_read_cluster(clust, inode);
		reiser4_lock_page(page);
		if (ret)
			return ret;
		/* cluster was uptodated here, release it before exit */
		release = 1;	
	}
	if(PageUptodate(page))
		/* races with other read/write */
		goto exit;	
	/* fill page by plain text */
	assert("edward-120", clust->len <= inode_cluster_size(inode));
	/* calculate page index in the cluster */
	page_idx = page->index & ((1 << inode_cluster_shift(inode)) - 1);
	to_page = PAGE_CACHE_SIZE;
	
	if(page_idx == (clust->len >> PAGE_CACHE_SHIFT))
		to_page = clust->len & (PAGE_CACHE_SHIFT - 1);
	else if (page_idx > (clust->len >> PAGE_CACHE_SHIFT))
		to_page = 0;
	data = kmap(page);
	memcpy(data, clust->buf + (page_idx << PAGE_CACHE_SHIFT), to_page);
	memset(data + clust->len, 0, PAGE_CACHE_SIZE - to_page);
	kunmap(page);
	SetPageUptodate(page);
 exit:
	if (release) 
		put_cluster_data(clust, inode);
	return 0;	
}

/* plugin->u.item.s.file.readpage */
int readpage_ctail(void * vp, struct page * page)
{
	int result;
	reiser4_cluster_t * clust = vp;

	assert("edward-114", clust != NULL);
	assert("edward-115", PageLocked(page));
	assert("edward-116", !PageUptodate(page));
	assert("edward-117", !jprivate(page) && !PagePrivate(page));
	assert("edward-118", page->mapping && page->mapping->host);
	
	result = do_readpage_ctail(clust, page);
	assert("edward-213", PageLocked(page));
	if (!result)
		reiser4_unlock_page(page);
	return result;
}

/* plugin->s.file.writepage */
int
writepage_ctail(uf_coord_t *uf_coord, struct page *page, write_mode_t mode)
{
	return 0;
}

/* plugin->u.item.s.file.readpages
   populate an address space with some pages, and start reads against them.
   FIXME_EDWARD: this function should return errors
*/
void
readpages_ctail(void *coord UNUSED_ARG, struct address_space *mapping, struct list_head *pages)
{
	reiser4_cluster_t clust;
	struct page *page;
	struct pagevec lru_pvec;
	int ret = 0;
	struct inode * inode;

	if (!list_empty(pages) && pages->next != pages->prev)
		/* more then one pages in the list - make sure its order is right */
		assert("edward-214", list_to_page(pages)->index < list_to_page(pages)->index);
	
	pagevec_init(&lru_pvec, 0);
	reiser4_cluster_init(&clust);
	inode = mapping->host;
	
	while (!list_empty(pages)) {
		page = list_to_page(pages);
		list_del(&page->list);
		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			continue;
		}
		/* update cluster if it is necessary */
		if (!cluster_is_uptodate(&clust) || !page_of_cluster(page, &clust, inode)) {
			put_cluster_data(&clust, inode);
			clust.index = cluster_index_by_page(page, inode);
			reiser4_unlock_page(page);
			ret = ctail_read_cluster(&clust, inode);
			if (ret)
				goto exit;
			reiser4_lock_page(page);
		}
		ret = do_readpage_ctail(&clust, page);
		if (!pagevec_add(&lru_pvec, page))
			__pagevec_lru_add(&lru_pvec);
		if (ret) {
			impossible("edward-215", "do_readpage_ctail returned crap");
			reiser4_unlock_page(page);
		exit:
			while (!list_empty(pages)) {
				struct page *victim;
				
				victim = list_to_page(pages);
				list_del(&victim->list);
				page_cache_release(victim);
			}
			break;
		}
	}
	put_cluster_data(&clust, inode);
	pagevec_lru_add(&lru_pvec);
	return;
}

/* plugin->u.item.s.file.write */
int
write_ctail(struct inode *inode, flow_t * f, hint_t *hint, int grabbed, write_mode_t mode)
{
	return 0;
}

/* 
   plugin->u.item.s.file.append_key
   key of first byte which is the next to last byte by addressed by this item
*/
reiser4_key *
append_key_ctail(const coord_t *coord, reiser4_key *key)
{
	return NULL;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
