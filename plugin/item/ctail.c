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

#define list_to_page(head) (list_entry((head)->prev, struct page, list))

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
unsigned
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

/* plugin->u.item.b.lookup :
   tail_lookup */

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

/* collect locked pages */
__attribute__((unused)) static int
grab_cache_cluster(struct inode * inode,
		   struct page ** page,
		   unsigned long index, /* index of a first page */
		   int nr_pages         /* number of pages */ )
{
	int i;
	int result = 0;

	assert("edward-158", !(index & ~(~0UL << inode_cluster_shift(inode))));
	
	for (i = 0; i < nr_pages; i++) {
		page[i] = grab_cache_page(inode->i_mapping, index + i);
		if (!(page[i])) {
			result = RETERR(-ENOMEM);
			break;
		}
	}
	if (result) {
		while(i) {
			i--;
			reiser4_unlock_page(page[i]);
			page_cache_release(page[i]);
		}
	}
	return result;
}

/* collect jnodes */
__attribute__((unused)) static int
jnodes_of_cluster(struct page ** page,  /* page of the cluster (should be locked) */ 
		  jnode ** jnode,
		  int nr_pages          /* number of pages */)
{
	int i;
	int result = 0;
	for (i = 0; i < nr_pages; i++) {
		assert("edward-169", page[i] != NULL);
		jnode[i] = jnode_of_page(page[i]);
		if (IS_ERR(jnode[i])) {
			result = PTR_ERR(jnode[i]);
			break;
		}
	}
	if (result) {
		while(i) {
			i--;
			jput(jnode[i]);
		}
	}
	return result;
}

__attribute__((unused)) static void
set_cluster_uptodate(struct inode * inode,
		     struct page ** page, /* first page of the cluster */
		     int nr_pages         /* number of pages */)
{
	int i;
	for (i = 0; i < nr_pages; i++)
		SetPageUptodate(page[i]);
}

/* return true if the cluster contains specified page */
static int
page_of_cluster(struct page * page, reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-162", page != NULL);
	assert("edward-163", clust != NULL);
	assert("edward-164", inode != NULL);
	assert("edward-165", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	return (clust->buf != NULL &&
		clust->index <= page->index &&
		page->index < clust->index + (1 << inode_cluster_shift(inode))); 
}

/* plugin->u.item.s.file.read */
int
read_ctail(struct file *file UNUSED_ARG, coord_t *coord, flow_t * f)
{
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
static int
ctail_cluster_by_page (reiser4_cluster_t * clust, struct page * page, struct inode * inode)
{
	flow_t f; /* for items collection */
	coord_t coord;
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
	assert("edward-140", clust->stat != HOLE_CLUSTER);
	
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
	
	/* calculate cluster index */
	index = cluster_index_by_page(page, inode);
	
	/* build flow for the cluster */
	fplug->flow_by_inode(inode, clust->buf, 0 /* kernel space */,
			     inode_scaled_cluster_size(inode), index << PAGE_CACHE_SHIFT, READ_OP, &f);
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));
	
	while (f.length) {
		init_lh(&lh);
		res = find_cluster_item(&f.key, &coord, &lh, &ra_info);
		switch (res) {
		case CBK_COORD_NOTFOUND:
			if (inode_scaled_offset(inode, index << PAGE_CACHE_SHIFT) == get_key_offset(&f.key)) {
				/* first item not found: hole cluster */
				clust->stat = HOLE_CLUSTER;
				res = 0;
				goto out;
			}
			/* we are outside the cluster, stop search here */
			/* FIXME-EDWARD. Handle the case when cluster is not complete (-EIO) */
			assert("edward-146", f.length != inode_scaled_cluster_size(inode));
			done_lh(&lh);
			goto ok;
		case CBK_COORD_FOUND:
			assert("edward-147", item_plugin_by_coord(&coord) == iplug);
			assert("edward-148", coord.between != AT_UNIT);
			res = zload_ra(coord.node, &ra_info);
			if (unlikely(res))
				goto out;
			res = iplug->s.file.read(NULL, &coord, &f);
			zrelse(coord.node);
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
	clust->index = index;
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

/* plugin->u.item.s.file.readpage */
int readpage_ctail(void * vp, struct page *page)
{
	reiser4_cluster_t * clust = vp;
	int index; /* page index in the cluster */
	int length;
	struct inode * inode;
	char * data;
	int release = 0;

	assert("edward-114", clust != NULL);
	assert("edward-115", PageLocked(page));
	assert("edward-116", !PageUptodate(page));
	assert("edward-117", !jprivate(page) && !PagePrivate(page));
	assert("edward-118", page->mapping && page->mapping->host);

	inode = page->mapping->host;

	if (cluster_is_required(clust)) {
		int ret;
		ret = ctail_cluster_by_page(clust, page, inode);
		if (ret)
			return ret;
		/* we just has attached data that must be released
		   before exit */
		release = 1;
	}
	if (clust->stat == HOLE_CLUSTER) {
		char *kaddr = kmap_atomic(page, KM_USER0);
		
		assert("edward-119", clust->buf == NULL);
		
		memset(kaddr, 0, PAGE_CACHE_SIZE);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		SetPageUptodate(page);
		reiser4_unlock_page(page);
		
		ON_TRACE(TRACE_CTAIL, " - hole, OK\n");
		return 0;
	}	
	
	/* fill one required page */
	assert("edward-120", clust->len <= inode_cluster_size(inode));
	/* length of data to copy to the page */

	index = page->index >> inode_cluster_shift(inode);
	length = (index == clust->len >> PAGE_CACHE_SHIFT ?
		  /* is this a last page of the cluster? */
		  inode_cluster_size(inode) - clust->len : PAGE_CACHE_SIZE);
	data = kmap(page);
	memcpy(data, clust->buf + (index << PAGE_CACHE_SHIFT), length);
	memset(data + clust->len, 0, PAGE_CACHE_SIZE - length);
	kunmap(page);
	SetPageUptodate(page);
	reiser4_unlock_page(page);
	if (release) 
		put_cluster_data(clust, inode);
	return 0;	
}

/* plugin->s.file.writepage */
int
writepage_ctail(coord_t * coord, lock_handle * lh, struct page *page)
{
	return 0;
}

/* plugin->u.item.s.file.readpages
   populate an address space with some pages, and start reads against them. */
void
readpages_ctail(coord_t *coord UNUSED_ARG, struct address_space *mapping, struct list_head *pages)
{
#if 0 
	reiser4_cluster_t clust;
	struct page *page;
	struct pagevec lru_pvec;
	int ret = 0;
	struct inode * inode;
	
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
		if (!page_of_cluster(page, &clust, inode)) {
			put_cluster_data(&clust, inode);
			ret = ctail_cluster_by_page(&clust, page, inode);
			if (ret)
				goto exit;
		}
		ret = ctail_readpage(&clust, page);
		if (!pagevec_add(&lru_pvec, page))
			__pagevec_lru_add(&lru_pvec);
		if (ret) {
			SetPageError(page);
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
	return ret;
#endif
} 

/* Read pages which are not completely overwritten.
   All cluster pages must be locked */
__attribute__((unused)) static int
prepare_cluster(struct inode *inode, struct page **pages,
		int nr_pages,    /* number of pages */
		unsigned from,   /* write position in cluster */
		unsigned count   /* bytes to write */)
{
	char *data;
	int result;
	
	assert ("edward-170", from < inode_cluster_size(inode));
	assert ("edward-171", 1 <= count && count <= inode_cluster_size(inode));
	
	result = 0;
	
	if (from == 0 && from + count >= inode->i_size) {
		/* Current end of file is in this cluster. Write areas covers it
		   all. No need to read cluster */

		/* FIXME-EDWARD: Hold the case of new pages (fill areas around write
		   ones by crypto-plugin special data */
		
		/* offset in last page after write */
		unsigned off = (unsigned)((from + count) & (PAGE_CACHE_SIZE - 1));
		if (off) {
			crypto_plugin * cplug = inode_crypto_plugin(inode);
			
			data = kmap_atomic(pages[nr_pages - 1], KM_USER0);
			cplug->align_cluster(data + off,  off, PAGE_CACHE_SIZE);
			kunmap_atomic(data, KM_USER0);
		}
	}
	else {
		/* read pages if it is necessary */
		int i;
		reiser4_cluster_t clust;
		reiser4_cluster_init(&clust);
		
		for (i = 0; i < nr_pages; i++) {
			assert("edward-172", PageLocked(pages[i]));

			if (from >> PAGE_CACHE_SHIFT < i &&
			    i < (from + count - 1) >> PAGE_CACHE_SHIFT) /* ? */
				/* page will be completely overwritten,
				   skip this */
				continue;
			if (PageUptodate(pages[i]))
				continue;
			if (!page_of_cluster(pages[i], &clust, inode)) {
				assert("edward-173", clust.buf == NULL);
				result = ctail_cluster_by_page(&clust, pages[i], inode);
				if (result)
					break;
			}
			result =  readpage_ctail(&clust, pages[i]);
			if (result) {
				put_cluster_data(&clust, inode);
				break;
			}
		}
	}	
	return result;
}


/* This is the interface to capture cluster nodes via their struct page reference.
   Any two blocks of the same cluster contain dependent modification and should
   commit at the same time */
int
try_capture_cluster(struct page **pg, znode_lock_mode lock_mode, int non_blocking)
{
	return 0;
}

/* plugin->u.item.s.file.write */
int
write_ctail(struct inode *inode, coord_t *coord UNUSED_ARG,
	    lock_handle *lh UNUSED_ARG, flow_t * f,
	    hint_t *hint UNUSED_ARG, int grabbed)
{
	int result = 0;
#if 0
	loff_t file_off;
	unsigned clust_off, to_clust;
	struct page * page;
	int i;

	assert("edward-159", current_blocksize == PAGE_CACHE_SIZE);
	assert("edward-160", f->user == 1);
	
	/* write position in file */
	file_off = get_key_offset(&f->key);
	/* write position in cluster */
	clust_off = (unsigned) (file_off & (inode_cluster_size(inode) - 1));
	
	do {
		int nr_pages;
		unsigned page_off, to_page;
		/* index of first cluster page in file */
		unsigned long index =
			(unsigned long) (file_off >>
					 PAGE_CACHE_SHIFT >>
					 inode_cluster_shift(inode) <<
					 inode_cluster_shift(inode));
		/* bytes to write */
		to_clust = inode_cluster_size(inode) - clust_off;
		if (to_clust > f->length)
			to_clust = f->length;
		/* number of cluster pages to allocate */
		nr_pages = (clust_off + to_clust - 1 >> PAGE_CACHE_SHIFT) + 1;

		{
			struct page * pages[nr_pages];
			jnode * jnodes[nr_pages];
			
			/* write position in page */
			page_off = clust_off & (PAGE_CACHE_SIZE - 1);
			/* allocate cluster pages */
			result = grab_cache_cluster(inode, pages, index, nr_pages);
			if (result)
				goto exit1;
			result = jnodes_of_cluster(pages, jnodes, nr_pages);
			if (result)
				goto exit2;
			result = prepare_cluster(inode, pages, nr_pages, file_off, clust_off, to_clust);
			if (result)
				goto exit3;
			assert("edward-161", schedulable());
			
                /* copy user data into cluster */
 		for (i = clust_off >> PAGE_CACHE_SHIFT;
		     i <= clust_off + to_clust >> PAGE_CACHE_SHIFT; i++) {
			to_page = PAGE_CACHE_SIZE - page_off;
			if (to_page > to_clust)
				to_page = to_clust;
			result = __copy_from_user((char *)kmap(pages[i]) + page_off, f->data, to_page);
			kunmap(pages[i]);
			if (unlikely(result)) {
				result = -EFAULT;
				goto exit3;
			}
			page_off = 0;
		}
		set_cluster_uptodate(inode, pages, nr_pages);
		result = try_capture_cluster(pages, ZNODE_WRITE_LOCK, 0);
		if (result)
			goto exit3;
		cluster_jnodes_make_dirty(jnodes);
		put_cluster_jnodes(jnodes);
		unlock_cluster_pages(pages);
		cluster_pages_release(pages);

		clust_off = 0;
		file_off += to_clust;
		move_flow_forward(f, to_clust);
		
	exit3:
		put_cluster_jnodes(jnodes);
	exit2:
		unlock_cluster_pages(pages);
		cluster_pages_release(pages);
	exit1:
		break;
	} while (f->length);
#endif
	return result;
}

/* 
   plugin->u.item.s.file.append_key
   key of first byte which is the next to last byte by addressed by this item
*/
reiser4_key *
append_key_ctail(const coord_t * coord, reiser4_key * key, void *p)
{
	return NULL;
}

/*
  plugin->u.item.s.file.key_in_item
  return true @coord is set inside of item to key @key
*/
int
key_in_item_ctail(coord_t * coord, const reiser4_key * key, void *p)
{
	return 0;
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
