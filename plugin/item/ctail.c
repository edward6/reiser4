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
#include "ctail.h"
#include "../../page_cache.h"

#include <linux/fs.h>	

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
ctail_mergeable(const coord_t * p1, const coord_t * p2)
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
	if ((get_key_offset(&key1) + ctail_nr_units(p1) != get_key_offset(&key2)) ||
	    ctail_nr_units(p1) == cluster_size_by_coord(p1)) {
		/* items of different clusters */
		return 0;
	}
	return 1;
}

/* plugin->u.item.b.nr_units */
unsigned
ctail_nr_units(const coord_t * coord)
{
	return (item_length_by_coord(coord) - sizeof(formatted_at(coord)->disk_cluster_size));
}

/* plugin->u.item.b.unit_key: 
   tail_unit_key */

/* estimate how much space is needed to insert/paste @data->length bytes
   into ctail at @coord */
int
ctail_estimate(const coord_t * coord /* coord of item */,
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
ctail_print(const char *prefix /* prefix to print */ ,
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
ctail_paste(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG)
{
	unsigned old_nr_units;
	
	/* number of units the item had before resizing has been performed */
	old_nr_units = ctail_nr_units(coord) - data->length;

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
ctail_can_shift(unsigned free_space, coord_t * source,
	       znode * target UNUSED_ARG, shift_direction direction UNUSED_ARG, unsigned *size, unsigned want)
{
	/* make sure that that we do not want to shift more than we have */
	assert("edward-68", want > 0 && want <= ctail_nr_units(source));

	*size = min(want, free_space);
	return *size;
}

/* plugin->u.item.b.copy_units */
void
ctail_copy_units(coord_t * target, coord_t * source,
		unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space UNUSED_ARG)
{
	/* make sure that item @target is expanded already */
	assert("edward-69", ctail_nr_units(target) >= count);
	assert("edward-70", free_space >= count);
	
	if (where_is_free_space == SHIFT_LEFT) {
		/* append item @target with @count first bytes of @source:
		   this restriction came from ordinary tails */
		assert("edward-71", from == 0);

		xmemcpy(first_unit(target) + ctail_nr_units(target) - count, first_unit(source), count);
	} else {
		/* target item is moved to right already */
		reiser4_key key;

		assert("edward-72", ctail_nr_units(source) == from + count);

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
ctail_cut_units(coord_t * coord, unsigned *from, unsigned *to,
	       const reiser4_key * from_key UNUSED_ARG,
	       const reiser4_key * to_key UNUSED_ARG, reiser4_key * smallest_removed,
	       void *p UNUSED_ARG)
{ 
	reiser4_key key;
	unsigned count;
	
 	count = *to - *from + 1;
	/* regarless to whether we cut from the beginning or from the end of
	   item - we have nothing to do */
	assert("edward-73", count > 0 && count <= ctail_nr_units(coord));
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

/* plugin->u.item.s.file.write */
int
ctail_write(struct inode *inode, coord_t *coord, lock_handle *lh, flow_t * f, struct sealed_coord *hint, int grabbed)
{
	return 0;
}

/* plugin->u.item.s.file.read */
int
ctail_read(struct file *file, coord_t *coord, flow_t * f)
{
	return 0;
}

static int ctail_cluster_by_page (reiser4_cluster_t * clust, struct page * page, struct inode * inode)
{
	return 0;
}

/* plugin->u.item.s.file.readpage */
int ctail_readpage(void * vp, struct page *page)
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
		
		trace_on(TRACE_CTAIL, " - hole, OK\n");
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
	if (release) 
		put_cluster_data(clust, inode);
	return 0;	
}

/* plugin->u.item.s.file.writepage */
int
ctail_writepage(coord_t * coord, lock_handle * lh, struct page *page)
{
	return 0;
}

/* plugin->u.item.s.file.readpages */
void
ctail_readpages(coord_t *coord, struct address_space *mapping, struct list_head *pages)
{
}
/* 
   plugin->u.item.s.file.append_key
   key of first byte which is the next to last byte by addressed by this item
*/
reiser4_key *
ctail_append_key(const coord_t * coord, reiser4_key * key, void *p)
{
	return NULL;
}

/*
  plugin->u.item.s.file.key_in_item
  return true @coord is set inside of item to key @key
*/
int
ctail_key_in_item(coord_t * coord, const reiser4_key * key, void *p)
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
