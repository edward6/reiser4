/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../../forward.h"
#include "../../debug.h"
#include "../../key.h"
#include "../../coord.h"
#include "file.h"
#include "../item/item.h"
#include "../plugin.h"
#include "../../txnmgr.h"
#include "../../jnode.h"
#include "../../znode.h"
#include "../../tree.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../super.h"
#include "../../page_cache.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct file  */
#include <linux/pagemap.h>

/* this file contains:
   tail2extent and extent2tail */

/* exclusive access to a file is acquired for tail conversion */
/* Audited by: green(2002.06.15) */
void
get_exclusive_access(struct inode *inode)
{
	down_write(&reiser4_inode_data(inode)->sem);
}

/* Audited by: green(2002.06.15) */
void
drop_exclusive_access(struct inode *inode)
{
	up_write(&reiser4_inode_data(inode)->sem);
}

/* nonexclusive access to a file is acquired for read, write, readpage */
/* Audited by: green(2002.06.15) */
void
get_nonexclusive_access(struct inode *inode)
{
	down_read(&reiser4_inode_data(inode)->sem);
}

/* Audited by: green(2002.06.15) */
void
drop_nonexclusive_access(struct inode *inode)
{
	up_read(&reiser4_inode_data(inode)->sem);
}

void
nea2ea(struct inode *inode)
{
	drop_nonexclusive_access(inode);
	get_exclusive_access(inode);
}

void
ea2nea(struct inode *inode)
{
	drop_exclusive_access(inode);
	get_nonexclusive_access(inode);
}

/* part of tail2extent. Cut all items covering @count bytes starting from
   @offset */
/* Audited by: green(2002.06.15) */
static int
cut_tail_items(struct inode *inode, loff_t offset, int count)
{
	reiser4_key from, to;

	/* AUDIT: How about putting an assertion here, what would check
	   all provided range is covered by tail items only? */
	/* key of first byte in the range to be cut  */
	unix_file_key_by_inode(inode, offset, &from);

	/* key of last byte in that range */
	to = from;
	set_key_offset(&to, (__u64) (offset + count - 1));

	/* cut everything between those keys */
	return cut_tree(tree_by_inode(inode), &from, &to);
}

typedef enum {
	UNLOCK = 0,
	RELEASE = 1,
	DROP = 2
} page_action;

static void
for_all_pages(struct page **pages, unsigned nr_pages, page_action action)
{
	unsigned i;

	for (i = 0; i < nr_pages; i++) {
		if (!pages[i])
			continue;
		switch(action) {
		case UNLOCK:
			reiser4_unlock_page(pages[i]);
			break;
		case DROP:
			reiser4_unlock_page(pages[i]);
		case RELEASE:
			assert("vs-1082", !PageLocked(pages[i]));
			page_cache_release(pages[i]);
			pages[i] = NULL;
			break;
		}
	}
}

/* part of tail2extent. replace tail items with extent one. Content of tail
   items (@count bytes) being cut are copied already into
   pages. extent_writepage method is called to create extents corresponding to
   those pages */
static int
replace(struct inode *inode, struct page **pages, unsigned nr_pages, int count)
{
	int result;
	unsigned i;
	item_plugin *iplug;
	STORE_COUNTERS;

	assert("vs-596", nr_pages > 0 && pages[0]);

	/* cut copied items */
	result = cut_tail_items(inode, (loff_t) pages[0]->index << PAGE_CACHE_SHIFT, count);
	if (result)
		return result;

	CHECK_COUNTERS;

	/* put into tree replacement for just removed items: extent item, namely */
	iplug = item_plugin_by_id(EXTENT_POINTER_ID);

	for (i = 0; i < nr_pages; i++) {
		result = unix_file_writepage_nolock(pages[i]);
		reiser4_unlock_page(pages[i]);
		if (result)
			break;
		SetPageUptodate(pages[i]);
	}
	return result;
}

#define TAIL2EXTENT_PAGE_NUM 3	/* number of pages to fill before cutting tail
				 * items */

int
tail2extent(struct inode *inode)
{
	int result;
	reiser4_key key;	/* key of next byte to be moved to page */
	ON_DEBUG(reiser4_key tmp;)
	char *p_data;		/* data of page */
	unsigned page_off = 0,	/* offset within the page where to copy data */
	 count;			/* number of bytes of item which can be
				 * copied to page */
	struct page *pages[TAIL2EXTENT_PAGE_NUM];
	int done;		/* set to 1 when all file is read */
	char *item;
	int i;

	/* switch inode's rw_semaphore from read_down (set by unix_file_write)
	 * to write_down */
	nea2ea(inode);

	if (inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN) && !inode_get_flag(inode, REISER4_HAS_TAIL)) {
		/* tail was converted by someone else */
		ea2nea(inode);
		return 0;
	}

	xmemset(pages, 0, sizeof (pages));

	/* collect statistics on the number of tail2extent conversions */
	reiser4_stat_file_add(tail2extent);

	/* get key of first byte of a file */
	unix_file_key_by_inode(inode, 0ull, &key);

	done = 0;
	result = 0;
	while (!done) {
		/*for_all_pages(pages, sizeof_array(pages), DROP);*/
		xmemset(pages, 0, sizeof (pages));
		for (i = 0; i < sizeof_array(pages) && !done; i++) {
			assert("vs-598", (get_key_offset(&key) & ~PAGE_CACHE_MASK) == 0);
			pages[i] = grab_cache_page(inode->i_mapping, (unsigned long) (get_key_offset(&key)
										      >> PAGE_CACHE_SHIFT));
			if (!pages[i]) {
				result = -ENOMEM;
				goto error;
			}

			/*
			 * usually when one is going to longterm lock znode (as
			 * find_next_item does, for instance) he must not hold
			 * locked pages. However, there is an exception for
			 * case tail2extent. Pages appearing here are not
			 * reachable to everyone else, they are clean, they do
			 * not have jnodes attached so keeping them locked do
			 * not risk deadlock appearance
			 */
			assert("vs-983", !PagePrivate(pages[i]));

			for (page_off = 0; page_off < PAGE_CACHE_SIZE;) {
				coord_t coord;
				lock_handle lh;

				/* get next item */
				coord_init_zero(&coord);
				init_lh(&lh);
				result = find_next_item(0, &key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
				if (result != CBK_COORD_FOUND) {
					if (result == CBK_COORD_NOTFOUND && get_key_offset(&key) == 0)
						/* conversion can be called for
						 * empty file */
						result = 0;
					done_lh(&lh);
					goto error;
				}
				if (coord.between == AFTER_UNIT) {
					/*
					 * this is used to detect end of file
					 * when inode->i_size can not be used
					 */
					done_lh(&lh);
					done = 1;
					p_data = kmap_atomic(pages[i], KM_USER0);
					xmemset(p_data + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic(p_data, KM_USER0);
					break;
				}
				result = zload(coord.node);
				if (result) {
					done_lh(&lh);
					goto error;
				}
				assert("vs-562", unix_file_owns_item(inode, &coord));
				assert("vs-856", coord.between == AT_UNIT);
				assert("green-11", keyeq(&key, unit_key_by_coord(&coord, &tmp)));
				if (item_id_by_coord(&coord) != TAIL_ID) {
					/*
					 * something other than tail
					 * found. This is only possible when
					 * first item of a file found during
					 * call to reiser4_mmap.
					 */
					result = -EIO;
					if (get_key_offset(&key) == 0 && item_id_by_coord(&coord) == EXTENT_POINTER_ID)
						result = 0;

					zrelse(coord.node);
					done_lh(&lh);
					goto error;
				}
				item = item_body_by_coord(&coord) + coord.unit_pos;

				/* how many bytes to copy */
				count = item_length_by_coord(&coord) - coord.unit_pos;
				/* limit length of copy to end of page */
				if (count > PAGE_CACHE_SIZE - page_off)
					count = PAGE_CACHE_SIZE - page_off;

				/* kmap/kunmap are necessary for pages which
				 * are not addressable by direct kernel virtual
				 * addresses */
				p_data = kmap_atomic(pages[i], KM_USER0);
				/* copy item (as much as will fit starting from
				 * the beginning of the item) into the page */
				memcpy(p_data + page_off, item, (unsigned) count);
				kunmap_atomic(p_data, KM_USER0);

				page_off += count;
				set_key_offset(&key, get_key_offset(&key) + count);

				zrelse(coord.node);
				done_lh(&lh);

				if (get_key_offset(&key) == inode->i_size) {
					/*
					 * end of file is detected here
					 */
					p_data = kmap_atomic(pages[i], KM_USER0);
					memset(p_data + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic(p_data, KM_USER0);
					done = 1;
					break;
				}
			}	/* for */
		}		/* for */

		/* to keep right lock order unlock pages before calling replace which will have to obtain longterm
		 * znode lock */
		for_all_pages(pages, sizeof_array(pages), UNLOCK);

		result = replace(inode, pages, i, (int) ((i - 1) * PAGE_CACHE_SIZE + page_off));
		for_all_pages(pages, sizeof_array(pages), RELEASE);
		if (result)
			goto exit;
	}
	/* tail coverted */
	inode_set_flag(inode, REISER4_TAIL_STATE_KNOWN);
	inode_clr_flag(inode, REISER4_HAS_TAIL);

	for_all_pages(pages, sizeof_array(pages), RELEASE);
	ea2nea(inode);

	/* It is advisabel to check here that all grabbed pages were freed */

	/* file could not be converted back to tails while we did not
	 * have neither NEA nor EA to the file */
	assert("vs-830", (inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN) && !inode_get_flag(inode, REISER4_HAS_TAIL)));
	assert("vs-1083", result == 0);
	return 0;

error:
	for_all_pages(pages, sizeof_array(pages), DROP);
exit:
	ea2nea(inode);
	return result;
}

/* part of extent2tail. Page contains data which are to be put into tree by
   tail items. Use tail_write for this. flow is composed like in
   unix_file_write. The only difference is that data for writing are in
   kernel space */
/* Audited by: green(2002.06.15) */
static int
write_page_by_tail(struct inode *inode, struct page *page, unsigned count)
{
	flow_t f;
	coord_t coord;
	lock_handle lh;
	znode *loaded;
	item_plugin *iplug;
	int result;

	result = 0;

	assert("vs-1089", count);

	coord_init_zero(&coord);
	init_lh(&lh);

	/* build flow */
	inode_file_plugin(inode)->flow_by_inode(inode, kmap(page), 0 /* not user space */ ,
						count, (loff_t) (page->index << PAGE_CACHE_SHIFT), WRITE_OP, &f);
	iplug = item_plugin_by_id(TAIL_ID);
	while (f.length) {
		result = find_next_item(0, &f.key, &coord, &lh, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT);
		if (result != CBK_COORD_NOTFOUND && result != CBK_COORD_FOUND) {
			break;
		}
		assert("vs-957", ergo(result == CBK_COORD_NOTFOUND, get_key_offset(&f.key) == 0));
		assert("vs-958", ergo(result == CBK_COORD_FOUND, get_key_offset(&f.key) != 0));

		result = zload(coord.node);
		if (result)
			break;
		loaded = coord.node;
		result = item_plugin_by_id(TAIL_ID)->s.file.write(inode, &coord, &lh, &f);
		zrelse(loaded);
		if (result)
			break;
		done_lh(&lh);
	}

	kunmap(page);

	/* result of write is 0 or error */
	assert("vs-589", result <= 0);
	/* if result is 0 - all @count bytes is written completely */
	assert("vs-588", ergo(result == 0, f.length == 0));
	return result;
}

static int
filler(void *vp, struct page *page)
{
	return unix_file_readpage(vp, page);
}

/* for every page of file: read page, cut part of extent pointing to this page,
   put data of page tree by tail item */
int
extent2tail(struct file *file)
{
	int result;
	struct inode *inode;
	struct page *page;
	unsigned long num_pages, i;
	reiser4_key from;
	reiser4_key to;
	unsigned count;

	/* collect statistics on the number of extent2tail conversions */
	reiser4_stat_file_add(extent2tail);

	inode = file->f_dentry->d_inode;

	get_exclusive_access(inode);

	if (inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN) && inode_get_flag(inode, REISER4_HAS_TAIL)) {
		drop_exclusive_access(inode);
		return 0;
	}

	/* number of pages in the file */
	num_pages = (inode->i_size + PAGE_CACHE_SIZE - 1) / PAGE_CACHE_SIZE;

	unix_file_key_by_inode(inode, 0ull, &from);
	to = from;

	result = 0;

	for (i = 0; i < num_pages; i++) {
		page = read_cache_page(inode->i_mapping, (unsigned) i, filler, file);
		if (IS_ERR(page)) {
			result = PTR_ERR(page);
			break;
		}

		wait_on_page_locked(page);

		if (!PageUptodate(page)) {
			page_cache_release(page);
			result = -EIO;
			break;
		}

		assert("nikita-2689", page->mapping == inode->i_mapping);

		/* cut part of file we have read */
		set_key_offset(&from, (__u64) (i << PAGE_CACHE_SHIFT));
		set_key_offset(&to, (__u64) ((i << PAGE_CACHE_SHIFT) + PAGE_CACHE_SIZE - 1));
		result = cut_tree(tree_by_inode(inode), &from, &to);
		if (result) {
			page_cache_release(page);
			break;
		}
		/* put page data into tree via tail_write */
		count = PAGE_CACHE_SIZE;
		if (i == num_pages - 1)
			count = (inode->i_size & ~PAGE_CACHE_MASK) ? : PAGE_CACHE_SIZE;
		result = write_page_by_tail(inode, page, count);
		if (result) {
			page_cache_release(page);
			break;
		}
		/* release page, detach jnode if any */
		reiser4_lock_page(page);
		assert("vs-1086", page->mapping == inode->i_mapping);
		if (PagePrivate(page)) {
			result = page->mapping->a_ops->invalidatepage(page, 0);
			if (result) {
				reiser4_unlock_page(page);
				page_cache_release(page);
				break;
			}
		}
		assert("nikita-2690", (!PagePrivate(page) && page->private == 0));
		drop_page(page, NULL);
		/*
		 * release reference taken by read_cache_page() above
		 */
		page_cache_release(page);
	}

	if (i == num_pages)
		/*
		 * FIXME-VS: not sure what to do when conversion did
		 * not complete
		 */
		inode_set_flag(inode, REISER4_HAS_TAIL);
	else
		warning("nikita-2282", "Partial conversion of %lu: %lu of %lu", inode->i_ino, i, num_pages);
	drop_exclusive_access(inode);
	return result;
}

/* Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
 */
