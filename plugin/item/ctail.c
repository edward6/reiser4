/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* ctails (aka "crypto tails") are items for cryptcompress objects */

/* DESCRIPTION:

Each cryptcompress object is stored on disk as a set of clusters sliced
into ctails.

Internal on-disk structure:

        HEADER   (1)  Here stored disk cluster shift
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
#include "../object.h"
#include "../../znode.h"
#include "../../carry.h"
#include "../../tree.h"
#include "../../inode.h"
#include "../../super.h"
#include "../../context.h"
#include "../../page_cache.h"
#include "../../flush.h"

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

static __u8
cluster_shift_by_coord(const coord_t * coord)
{
	return d8tocpu(&formatted_at(coord)->cluster_shift);
}

static unsigned
cluster_size_by_coord(const coord_t * coord)
{
	return (PAGE_CACHE_SIZE << cluster_shift_by_coord(coord));
}

static unsigned long
cluster_index_by_coord(const coord_t * coord)
{
	reiser4_key  key;
	
	return get_key_offset(item_key_by_coord(coord, &key)) >>  cluster_shift_by_coord(coord) >> PAGE_CACHE_SHIFT;
}

#define cluster_key(key, coord) !(get_key_offset(key) & ~(~0ULL << cluster_shift_by_coord(coord) << PAGE_CACHE_SHIFT))

static char *
first_unit(coord_t * coord)
{
	/* FIXME: warning: pointer of type `void *' used in arithmetic */
	return (char *)item_body_by_coord(coord) + sizeof (ctail_item_format);
}

/* plugin->u.item.b.max_key_inside :
   tail_max_key_inside */

/* plugin->u.item.b.can_contain_key */
int
can_contain_key_ctail(const coord_t *coord, const reiser4_key *key, const reiser4_item_data *data)
{
	reiser4_key item_key;

	if (item_plugin_by_coord(coord) != data->iplug)
		return 0;

	item_key_by_coord(coord, &item_key);
	if (get_key_locality(key) != get_key_locality(&item_key) ||
	    get_key_objectid(key) != get_key_objectid(&item_key))
		return 0;
	if (get_key_offset(&item_key) + nr_units_ctail(coord) != get_key_offset(key))
		return 0;
	if (cluster_key(key, coord))
		return 0;
	return 1;
}

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
	if (get_key_offset(&key1) + nr_units_ctail(p1) != get_key_offset(&key2))
		/*  not adjacent items */
		return 0;
	if (cluster_key(&key2, p2))
		return 0;
	return 1;
}

/* plugin->u.item.b.nr_units */
pos_in_item_t
nr_units_ctail(const coord_t * coord)
{
	return (item_length_by_coord(coord) - sizeof(formatted_at(coord)->cluster_shift));
}

/* plugin->u.item.b.unit_key: 
   tail_unit_key */

/* plugin->u.item.b.estimate:
   estimate how much space is needed to insert/paste @data->length bytes
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

/* ->init() method for this item plugin. */
int
init_ctail(coord_t * coord /* coord of item */,
	   reiser4_item_data * data /* structure used for insertion */)
{
	cputod8(*((char *)(data->arg)), &formatted_at(coord)->cluster_shift);
	return 0;
}

/* plugin->u.item.b.lookup:
   NULL. (we are looking only for exact keys from item headers) */


/* plugin->u.item.b.check */

/* plugin->u.item.b.paste */
int
paste_ctail(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG)
{
	unsigned old_nr_units;

	assert("edward-268", data->data != NULL);
	/* copy only from kernel space */
	assert("edward-66", data->user == 0);
	
	/* number of units the item had before resizing has been performed */
	old_nr_units = nr_units_ctail(coord) - data->length;

	/* tail items never get pasted in the middle */
	assert("edward-65",
	       (coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||  /* after can_paste - wanna glue at right - impossible */     
	       (coord->unit_pos == old_nr_units - 1 && coord->between == AFTER_UNIT) ||  
	       (coord->unit_pos == 0 && old_nr_units == 0 && coord->between == AT_UNIT)); /* after create item */
	
	if (coord->between == AFTER_UNIT)
		coord->unit_pos++;
	
	xmemcpy(first_unit(coord) + coord->unit_pos, data->data, (unsigned) data->length);
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

/* plugin->u.item.b.create_hook */
/* plugin->u.item.b.kill_hook */
int
kill_hook_ctail(const coord_t *coord, unsigned from, unsigned count, struct cut_list *p)
{
	struct inode *inode;

	assert("edward-291", znode_is_write_locked(coord->node));
	
	inode = p->inode;
	if (inode) {
		reiser4_key key;
		
		assert("edward-292", count == nr_units_ctail(coord));

		item_key_by_coord(coord, &key);
		if (from == 0 &&
		    ((get_key_offset(&key) & ((loff_t) (cluster_size_by_coord(coord)) - 1)) == 0))
			truncate_pages_cryptcompress(inode->i_mapping, off_to_pg(get_key_offset(&key)));
	}
	return 0;
}

/* plugin->u.item.b.shift_hook */
/* plugin->u.item.b.cut_units */
int 
cut_units_ctail(coord_t * coord, unsigned *from, unsigned *to,
	       const reiser4_key * from_key UNUSED_ARG,
	       const reiser4_key * to_key UNUSED_ARG, reiser4_key * smallest_removed,
	       struct cut_list *p UNUSED_ARG)
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
	coord->unit_pos --; /* ?? */
	coord->between = AFTER_UNIT;
	return 0;
}

/* this reads one cluster form disk,
   attaches buffer with decrypted and decompressed data */
int
ctail_read_cluster (reiser4_cluster_t * clust, struct inode * inode, int write)
{
	int result;

	assert("edward-139", clust->buf == NULL);
	assert("edward-140", clust->stat != FAKE_CLUSTER);
	assert("edward-145", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	/* allocate temporary buffer of disk cluster size */
	/* FIXME-EDWARD:
	   - kmalloc?
	   - optimize it for the clusters which represent end of file */
	clust->bufsize = inode_scaled_cluster_size(inode);
	clust->buf = reiser4_kmalloc(clust->bufsize, GFP_KERNEL);
	if (!clust->buf) 
		return -ENOMEM;
	result = find_cluster(clust, inode, 1 /* read */, write);
	if (result)
		goto out;
	result = inflate_cluster(clust, inode);
	if(result)
		goto out;
	return 0;
 out:
	put_cluster_data(clust, inode);
	return result;
}

/* read one locked page */
int do_readpage_ctail(reiser4_cluster_t * clust, struct page *page)
{
	int ret;
	unsigned cloff;
	struct inode * inode;
	char * data;
	int release = 0;

	assert("edward-212", PageLocked(page));
	inode = page->mapping->host;

	if (!cluster_is_uptodate(clust)) {
		clust->index = pg_to_clust(page->index, inode);
		reiser4_unlock_page(page);
		ret = ctail_read_cluster(clust, inode, 0 /* do not write */);
		reiser4_lock_page(page);
		if (ret)
			return ret;
		/* cluster was uptodated here, release it before exit */
		release = 1;	
	}
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
	if(PageUptodate(page))
		/* races with other read/write */
		goto exit;	
	/* fill page by plain text */
	assert("edward-120", clust->len <= inode_cluster_size(inode));
	assert("edward-299", off_to_pgoff(clust->len) == 0);
	
	cloff = pg_to_off_to_cloff(page->index, inode);
	data = kmap(page);
	memcpy(data, clust->buf + cloff, PAGE_CACHE_SIZE);
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
	/* */
	reiser4_unlock_page(page);

	return result;
}

/* plugin->s.file.writepage */
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
			clust.index = pg_to_clust(page->index, inode);
			reiser4_unlock_page(page);
			ret = ctail_read_cluster(&clust, inode, 0 /* do not write */);
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

/* 
   plugin->u.item.s.file.append_key
*/
reiser4_key *
append_key_ctail(const coord_t *coord, reiser4_key *key)
{
	return NULL;
}

/* key of the first item of the next cluster */
reiser4_key *
append_cluster_key_ctail(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, ((__u64)(cluster_index_by_coord(coord)) + 1) << cluster_shift_by_coord(coord) << PAGE_CACHE_SHIFT);
	return key;
}

static int
insert_crc_flow(coord_t * coord, lock_handle * lh, flow_t * f, struct inode * inode)
{
	int result;
	carry_pool pool;
	carry_level lowest_level;
	carry_op *op;
	reiser4_item_data data;
	__u8 cluster_shift = inode_cluster_shift(inode);

	init_carry_pool(&pool);
	init_carry_level(&lowest_level, &pool);
	
	op = post_carry(&lowest_level, COP_INSERT_FLOW, coord->node, 0 /* operate directly on coord -> node */ );
	if (IS_ERR(op) || (op == NULL))
		return RETERR(op ? PTR_ERR(op) : -EIO);
	data.user = 0;
	data.iplug = item_plugin_by_id(CTAIL_ID);
	data.arg = &cluster_shift;

	data.length = 0;
	data.data = 0;

	op->u.insert_flow.insert_point = coord;
	op->u.insert_flow.flow = f;
	op->u.insert_flow.data = &data;
	op->u.insert_flow.new_nodes = 0;

	lowest_level.track_type = CARRY_TRACK_CHANGE;
	lowest_level.tracked = lh;
	
	ON_STATS(lowest_level.level_no = znode_get_level(coord->node));
	result = carry(&lowest_level, 0);
	done_carry_pool(&pool);
	
	return result;
}

static int
insert_crc_flow_in_place(coord_t * coord, lock_handle * lh, flow_t * f, struct inode * inode)
{
	int ret;
	coord_t point;
	lock_handle lock;

	coord_dup (&point, coord);
	init_lh (&lock);
	copy_lh(&lock, lh);
	
	ret = insert_crc_flow(&point, &lock, f, inode);
	done_lh(&lock);
	return ret;
}


/* overwrite first @f->length bytes of the item and cut remainder without carry */
static int
overwrite_ctail(coord_t * coord, flow_t * f)
{
	unsigned count;
	
	assert("edward-269", f->user == 0);
	assert("edward-270", f->data != NULL);
	assert("edward-271", f->length > 0);
	assert("edward-272", coord_is_existing_unit(coord));
	assert("edward-273", coord->unit_pos == 0);
	assert("edward-274", znode_is_write_locked(coord->node));
	assert("edward-275", schedulable());

	count = item_length_by_coord(coord);
	
	if (count > f->length) {
		node_plugin * nplug = node_plugin_by_coord(coord);
		nplug->change_item_size(coord, count - f->length);
		count = f->length;
	}
	xmemcpy(item_body_by_coord(coord), f->data, count);
	move_flow_forward(f, count);
	return 0;
}

static int
cut_ctail(coord_t * coord)
{
	return 0;
}

/* plugin->u.item.s.file.write ? */
int
write_ctail(flow_t *f, coord_t *coord, lock_handle *lh, int grabbed, crc_write_mode_t mode, struct inode * inode)
{
	int result;
	
	switch (mode) {
	case CRC_FIRST_ITEM:
	case CRC_APPEND_ITEM:
		result = insert_crc_flow_in_place(coord, lh, f, inode);
		break;
	case CRC_OVERWRITE_ITEM:
		result = overwrite_ctail(coord, f);
		break;
	case CRC_CUT_ITEM:
		result = cut_ctail(coord);
		break;
	default:
		impossible("edward-244", "wrong ctail write mode");
	}
	return result;
}

/* plugin->u.item.f.scan */
/* Check if the cluster node we started from doesn't have any items
   in the tree. If so, insert prosessed cluster into the tree.
   Don't care about scan counter since leftward scanning will be continued
   from rightmost dirty node.
*/  
int scan_ctail(flush_scan * scan, const coord_t * in_coord)
{
	int result;
	struct page * page;
	struct inode * inode;
	reiser4_cluster_t clust;
	flow_t f;
	file_plugin * fplug;
	
	assert("edward-227", scan->node != NULL);
	assert("edward-228", jnode_is_cluster_page(scan->node));
	
	page = jnode_page(scan->node);
	
	assert("edward-229", page->mapping != NULL);
	assert("edward-230", page->mapping != NULL);
	assert("edward-231", page->mapping->host != NULL);

	inode = page->mapping->host;
	fplug = inode_file_plugin(inode);

	assert("edward-244", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	assert("edward-232", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	reiser4_cluster_init(&clust);

	if (!coord_is_invalid(&scan->parent_coord)) {
		/* do nothing */
		return 0;
	}
	assert("edward-233", scan->direction == LEFT_SIDE);
	/* we start at an unformatted node, which doesn't have any items
	   in the tree (new file or converted hole), so insert flow and
	   continue scan from rightmost dirty node */
	
	clust.index = pg_to_clust(page->index, inode);
	
	/* remove appropriate jnodes from dirty list */
	result = flush_cluster_pages(&clust, inode);
	if (result)
		return result;
	result = deflate_cluster(&clust, inode);
	if (result)
		goto exit;
	
	fplug->flow_by_inode(inode, clust.buf, 0, clust.len, clust_to_off(clust.index, inode), WRITE, &f);
	/* insert processed data */
	result = insert_crc_flow(&scan->parent_coord, /* insert point */
				 &scan->parent_lock, &f, inode);
	if (result)
		goto exit;
	
	assert("edward-234", f.length == 0);
	assert("edward-235", !coord_is_invalid(&scan->parent_coord));

 exit:	
	put_cluster_data(&clust, inode);
	return result;
}

/* how to write */
static crc_write_mode_t
guess_write_mode(flush_pos_t * pos, int child)
{
	ctail_squeeze_info_t * info;
	
	assert("edward-245", pos != NULL);
	assert("edward-246", znode_is_wlocked(pos->coord.node));
	assert("edward-247", znode_is_loaded(pos->coord.node));
	
	info = &pos->idata->ctail_info;
	
	if (!info->flow.length)
		return CRC_CUT_ITEM;
	
	if (!child)
		return CRC_APPEND_ITEM;
	
	return CRC_OVERWRITE_ITEM;
}

/* attach valid squeeze item data to the flush position */
static int
attach_squeeze_ctail_data(flush_pos_t * pos, struct inode * inode)
{
	int ret = 0;
	file_plugin * fplug;
	ctail_squeeze_info_t * info;
	
	assert("edward-248", pos != NULL);
	assert("edward-249", pos->child != NULL);
	assert("edward-250", pos->idata == NULL);
	assert("edward-251", inode != NULL);

	fplug = inode_file_plugin(inode);
	
	assert("edward-252", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));

	pos->idata = reiser4_kmalloc(sizeof(flush_squeeze_item_data_t), GFP_KERNEL);
	if (pos->idata == NULL)
		return -ENOMEM;
	
	info = &pos->idata->ctail_info;
	info->clust = reiser4_kmalloc(sizeof(reiser4_cluster_t), GFP_KERNEL);
	if (info->clust == NULL) {
		ret = -ENOMEM;
		goto exit2;
	}
	reiser4_cluster_init(info->clust);
	info->inode = inode;
	info->clust->index = pg_to_clust(jnode_page(pos->child)->index, inode);
	
	ret = flush_cluster_pages(info->clust, inode);
	if (ret)
		goto exit1;

	ret = deflate_cluster(info->clust, inode);
	if (ret)
		goto exit1;
	/* attach flow by cluster buffer */
	fplug->flow_by_inode(info->inode, info->clust->buf, 0/* kernel space */, info->clust->len, clust_to_off(info->clust->index, inode), WRITE_OP, &info->flow);
	jput(pos->child);
 	return 0;

 exit1:
	reiser4_kfree(info->clust, sizeof(reiser4_cluster_t));
 exit2:
	reiser4_kfree(pos->idata, sizeof(flush_squeeze_item_data_t));
	jput(pos->child);
        /* invalidate squeeze item info */
	pos->idata = NULL;
	return ret;
}

static void
invalidate_squeeze_ctail_data(flush_squeeze_item_data_t * idata)
{

	ctail_squeeze_info_t * info;
	
	assert("edward-253", idata != NULL);
	info = &idata->ctail_info;

	assert("edward-254", info->clust != NULL);
	assert("edward-255", info->inode != NULL);
	assert("edward-256", info->clust->buf != NULL);

	reiser4_kfree(info->clust->buf, inode_scaled_cluster_size(info->inode));
	reiser4_kfree(info->clust, sizeof(reiser4_cluster_t));
	reiser4_kfree(info, sizeof(ctail_squeeze_info_t));
}

/* plugin->u.item.f.utmost_child */

/* This function sets leftmost child for a first cluster item,
   if the child exists, and NULL in other cases.
   NOTE-EDWARD: Do not call this for RIGHT_SIDE */

int
utmost_child_ctail(const coord_t * coord, sideof side, jnode ** child)
{	
	reiser4_key key;

	assert("edward-257", coord != NULL);
	assert("edward-258", child != NULL);
	assert("edward-259", side == LEFT_SIDE);
	assert("edward-260", item_plugin_by_coord(coord) == item_plugin_by_id(CTAIL_ID));

	if (get_key_offset(&key) & ~(~0ULL << cluster_shift_by_coord(coord) << PAGE_CACHE_SHIFT))
		*child = NULL;
	else
		*child = jlook_lock(current_tree, get_key_objectid(item_key_by_coord(coord, &key)), cluster_index_by_coord(coord));
	return 0;
}

/* plugin->u.item.f.squeeze */
/* @child == 1: attach squeeze item info to the @pos if it is not uptodate,
   @child == 0: detach and invalidate it.
*/
int squeeze_ctail(flush_pos_t * pos, int child)
{
	int result;
	ctail_squeeze_info_t * info;
	
	assert("edward-261", pos != NULL);
	assert("edward-262", item_plugin_by_coord(&pos->coord) == item_plugin_by_id(CTAIL_ID));
	
	if (pos->idata == 0) {
		
		struct inode * inode;
		
		assert("edward-263", child != 0);
		assert("edward-264", pos->child != NULL);
		assert("edward-265", jnode_page(pos->child) != NULL);
		assert("edward-266", jnode_page(pos->child)->mapping != NULL);
		
		inode = jnode_page(pos->child)->mapping->host;
		
		assert("edward-267", inode != NULL);
		
		/* attach item squeeze info by child and put the last one */
		result = attach_squeeze_ctail_data(pos, inode);
		pos->child = NULL;
		if (result != 0)
			return result;
	}
	
	info = &pos->idata->ctail_info;
	result = write_ctail(&info->flow, &pos->coord, &pos->lock, 0, guess_write_mode(pos, child), info->inode);
	if (result != 0 || child == 0)
		invalidate_squeeze_ctail_data(pos->idata); 
	return result;
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
