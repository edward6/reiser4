/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */


#include "../../reiser4.h"

/* this file contains:
 * tail2extent and extent2tail
 */

/* exclusive access to a file is acquired for tail conversion */
/* Audited by: green(2002.06.15) */
void get_exclusive_access (struct inode * inode)
{
	down_write (&reiser4_inode_data (inode)->sem);
}

/* Audited by: green(2002.06.15) */
void drop_exclusive_access (struct inode * inode)
{
	up_write (&reiser4_inode_data (inode)->sem);
}

/* nonexclusive access to a file is acquired for read, write, readpage */
/* Audited by: green(2002.06.15) */
void get_nonexclusive_access (struct inode * inode)
{
	down_read (&reiser4_inode_data (inode)->sem);
}

/* Audited by: green(2002.06.15) */
void drop_nonexclusive_access (struct inode * inode)
{
	up_read (&reiser4_inode_data (inode)->sem);
}


/* part of tail2extent. @count bytes were copied into @page starting from the
 * beginning. mark all modifed jnode dirty and loaded, mark others
 * uptodate */
static void mark_jnodes_dirty (struct page * page, unsigned count)
{
	struct jnode * j;

	/* make sure that page has jnodes */
	assert ("vs-525", PagePrivate (page));
	
	j = nth_jnode (page, 0);
	do {
		if (count) {
			jnode_set_dirty (j);
			if (count < current_blocksize)
				count = 0;
			else
				count -= current_blocksize;
		}
		jnode_set_loaded (j);

		/* AUDIT Perhaps it worth to add a check for count !=
		 * 0 here as well to optimise for a case where only
		 * few buffers at the beginning of a page were
		 * touched.  ANSWER: we need to iterate all jnodes
		 * anyway to mark them loaded */
	} while ((j = next_jnode (j)) != 0);
}


/* part of tail2extent. Cut all items covering @count bytes starting from
 * @offset */
/* Audited by: green(2002.06.15) */
static int cut_tail_items (struct inode * inode, loff_t offset, int count)
{
	reiser4_key from, to;


	/* AUDIT: How about putting an assertion here, what would check
	   all provided range is covered by tail items only? */
	/* key of first byte in the range to be cut  */
	unix_file_key_by_inode (inode, offset, &from);

	/* key of last byte in that range */
	to = from;
	set_key_offset (&to, (__u64)(offset + count - 1));

	/* cut everything between those keys */
	return cut_tree (tree_by_inode (inode), &from, &to);
}


/* @page contains file data. tree does not contain items corresponding to those
 * data. Put them in using specified item plugin */
static int write_pages_by_item (struct inode * inode, struct page ** pages,
				unsigned nr_pages, int count, item_plugin * iplug)
{
	flow_t f;
	coord_t coord;
	lock_handle lh;
	int result;
	char * p_data;
	unsigned i;
	int to_page;


	assert ("vs-604", ergo (item_plugin_id(iplug) == TAIL_ID, 
				nr_pages == 1));
	assert ("vs-564", iplug && iplug->s.file.write);


	result = 0;

	coord_init_zero (&coord);
	init_lh (&lh);

	for (i = 0; i < nr_pages; i ++) {
		if (item_plugin_id (iplug) == TAIL_ID)
			p_data = kmap (pages [i]);
		else
			p_data = 0;

		/* build flow */
		if (count > (int)PAGE_CACHE_SIZE)
			to_page = (int)PAGE_CACHE_SIZE;
		else
			to_page = count;
			
		inode_file_plugin (inode)->
			flow_by_inode (inode, p_data/* no data to copy from */,
				       0/* not user space */,
				       (unsigned)to_page,
				       (loff_t)(pages[i]->index << PAGE_CACHE_SHIFT),
				       WRITE_OP, &f);

		do {
			znode * loaded;

			result = find_next_item (0, &f.key, &coord, &lh, 
						 ZNODE_WRITE_LOCK);
			if (result != CBK_COORD_NOTFOUND && 
			    result != CBK_COORD_FOUND) {
				goto done;
			}
			loaded = coord.node;
			result = zload (loaded);
			if (result)
				goto done;
			
			result = iplug->s.file.write (inode, &coord, &lh, &f,
						      pages [i]);
			/* item's write method may return -EAGAIN */
			zrelse (loaded);
		} while (result == -EAGAIN);

		if (p_data != NULL)
			kunmap (pages [i]);
		/* page is written */
		count -= to_page;
	}

 done:
	done_lh (&lh);

	/* result of write is 0 or error */
	assert ("vs-589", result <= 0);
	/* if result is 0 - all @count bytes is written completely */
	assert ("vs-588", ergo (result == 0, count == 0));
	return result;
}


/* part of tail2extent. @page contains data read from tail items. Those tail
 * items are removed from tree already. extent slot pointing to this page will
 * be created by using extent_write */
/* Audited by: green(2002.06.15) */
static int write_pages_by_extent (struct inode * inode, struct page ** pages,
				  unsigned nr_pages, int count)
{
	return write_pages_by_item (inode, pages, nr_pages, count,
				    item_plugin_by_id (EXTENT_POINTER_ID));
}


/* Audited by: green(2002.06.15) */
static void drop_pages (struct page ** pages, unsigned nr_pages)
{
	unsigned i;

	for (i = 0; i < nr_pages; i ++) {
		unlock_page (pages [i]);
		page_cache_release (pages [i]);
	}
}


/* part of tail2extent.  */
/* Audited by: green(2002.06.15) */
static int replace (struct inode * inode, struct page ** pages, unsigned nr_pages,
		    int count)
{
	int result;
	unsigned i;
	unsigned to_page;
	STORE_COUNTERS;

	assert ("vs-596", nr_pages > 0 && pages [0]);

	/* cut copied items */
	result = cut_tail_items (inode, (loff_t)pages [0]->index << PAGE_CACHE_SHIFT,
				 count);
	if (result) {
		return result;
	}

	CHECK_COUNTERS;

	/* put into tree replacement for just removed items: extent item,
	 * namely */
	result = write_pages_by_extent (inode, pages, nr_pages, count);
	if (result) {
		return result;
	}
	
	/* mark buffers of pages (those only into which removed tail items were
	 * copied) dirty and all pages - uptodate */
	to_page = PAGE_CACHE_SIZE;
	for (i = 0; i < nr_pages; i ++) {
		if (i == nr_pages - 1 && (count & ~PAGE_CACHE_MASK))
			to_page = (count & ~PAGE_CACHE_MASK);
		mark_jnodes_dirty (pages [i], to_page);
		SetPageUptodate (pages [i]);
	}
	return 0;
}


#define TAIL2EXTENT_PAGE_NUM 3  /* number of pages to fill before cutting tail
				 * items */

/* Audited by: green(2002.06.15) */
static int all_pages_are_full (unsigned nr_pages, unsigned page_off)
{
	/* max number of pages is used and last one is full */
	return (nr_pages == TAIL2EXTENT_PAGE_NUM &&
		page_off == PAGE_CACHE_SIZE);
}


/* part of tail2extent. */
/* Audited by: green(2002.06.15) */
static int file_is_over (struct inode * inode, reiser4_key * key,
			 coord_t * coord)
{
	reiser4_key coord_key;

	assert ("vs-567", item_id_by_coord (coord) == TAIL_ID);
	assert ("vs-566", inode->i_size >= (loff_t)get_key_offset (key));
	item_key_by_coord (coord, &coord_key);
	assert ("vs-601", keygt (key, &coord_key));
	set_key_offset (&coord_key,
			get_key_offset (&coord_key) +
			item_length_by_coord (coord));
	assert ("vs-568", keyle (key, &coord_key));

	/*
	 * FIXME-VS: do we need to try harder?
	 */
	return (inode->i_size == (loff_t)get_key_offset (key));
}


int tail2extent (struct inode * inode)
{
	int result;
	coord_t coord;
	lock_handle lh;	
	reiser4_key key;     /* key of next byte to be moved to page */
	reiser4_key tmp;     /* used for sanity check */
	struct page * page;
	char * p_data;       /* data of page */
	unsigned page_off,   /* offset within the page where to copy data */
		count,       /* number of bytes of item which can be
			      * copied to page */
		copied;      /* number of bytes of item copied already */
	struct page * pages [TAIL2EXTENT_PAGE_NUM];
	unsigned nr_pages;        /* number of pages in the above array */
	int done;            /* set to 1 when all file is read */
	char * item;


	/* switch inode's rw_semaphore from read_down (set by unix_file_write)
	 * to write_down */
	drop_nonexclusive_access (inode);
	get_exclusive_access (inode);

	if (inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN) &&
	    !inode_get_flag (inode, REISER4_HAS_TAIL)) {
		/* tail was converted by someone else */
		result = 0;
		goto ok;
	}

	/* collect statistics on the number of tail2extent conversions */
	reiser4_stat_file_add (tail2extent);

	/* get key of first byte of a file */
	unix_file_key_by_inode (inode, 0ull, &key);

	memset (pages, 0, sizeof (pages));
	nr_pages = 0;
	page = 0;
	page_off = 0;
	item = 0;
	copied = 0;

	coord_init_zero (&coord);
	init_lh (&lh);
	while (1) {
		if (!item) {
			/* get next item */
			result = find_next_item (0, &key, &coord, &lh, ZNODE_READ_LOCK);
			if (result != CBK_COORD_FOUND) {
				drop_pages (pages, nr_pages);
				goto error1;
			}
			result = zload (coord.node);
			if (result) {
				drop_pages (pages, nr_pages);
				goto error1;
			}
			if (item_id_by_coord (&coord) != TAIL_ID) {
				/*
				 * something other than tail found
				 */
				if (get_key_offset (&key) == (__u64)0) {
					if (item_id_by_coord (&coord) != EXTENT_POINTER_ID)
						result = -EIO;
					else
						result = 0;
				} else
					result = -EIO;
				drop_pages (pages, nr_pages);
				zrelse (coord.node);
				goto error1;
			}
			item = item_body_by_coord (&coord);
			assert ("green-11",
				keyeq (&key, item_key_by_coord (&coord, &tmp)));
			copied = 0;
		}
		assert ("vs-562", unix_file_owns_item (inode, &coord));
		
		if (!page) {
			assert ("vs-598",
				(get_key_offset (&key) & ~PAGE_CACHE_MASK) == 0);
			page = grab_cache_page (inode->i_mapping,
						(unsigned long)(get_key_offset (&key) >>
								PAGE_CACHE_SHIFT));
			if (!page) {
				drop_pages (pages, nr_pages);
				zrelse (coord.node);
				result = -ENOMEM;
				goto error1;
			}
			
			page_off = 0;
			pages [nr_pages] = page;
			nr_pages ++;
		}
		
		/* how many bytes to copy */
		count = item_length_by_coord (&coord) - copied;
		/* limit length of copy to end of page */
		if (count > PAGE_CACHE_SIZE - page_off) {
			count = PAGE_CACHE_SIZE - page_off;
		}
		
		/* kmap/kunmap are necessary for pages which are not
		 * addressable by direct kernel virtual addresses */
		p_data = kmap (page);
		/* copy item (as much as will fit starting from the beginning
		 * of the item) into the page */
		memcpy (p_data + page_off, item, (unsigned)count);
		kunmap (page);
		page_off += count;
		copied += count;
		item += count;
		set_key_offset (&key, get_key_offset (&key) + count);

		if ((done = file_is_over (inode, &key, &coord)) ||
		    all_pages_are_full (nr_pages, page_off)) {
			zrelse (coord.node);
			done_lh (&lh);
			/* replace tail items with extent */
			result = replace (inode, pages, nr_pages, 
					  (int)((nr_pages - 1) * PAGE_CACHE_SIZE +
						page_off));
			drop_pages (pages, nr_pages);
			if (result) {
				goto error2;
			}
			if (done) {
				/* conversion completed */
				inode_set_flag (inode, REISER4_TAIL_STATE_KNOWN);
				inode_clr_flag (inode, REISER4_HAS_TAIL);
				goto ok;
			}
			
			/* there are still tail items of a file */
			memset (pages, 0, sizeof (pages));
			nr_pages = 0;
			item = 0;
			page = 0;
			coord_init_zero (&coord);
			init_lh (&lh);
			continue;
		}
		
		if (copied == (unsigned)item_length_by_coord (&coord)) {
			/* item is over, find next one */
			item = 0;
			zrelse (coord.node);
		}
		if (page_off == PAGE_CACHE_SIZE) {
			/* page is over */
			page = 0;
		}
	}
 error1:
	done_lh (&lh);
 error2:
	drop_exclusive_access (inode);
	get_nonexclusive_access (inode);
	return result;

 ok:
	drop_exclusive_access (inode);
	get_nonexclusive_access (inode);
	
	/* It is advisabel to check here that all grabbed pages were freed */

	/* file should not be converted back to tails */
	assert ("vs-830", (inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN) &&
			   !inode_get_flag (inode, REISER4_HAS_TAIL)));

	return result;
}


/* part of extent2tail. Page contains data which are to be put into tree by
 * tail items. Use tail_write for this. flow is composed like in
 * unix_file_write. The only difference is that data for writing are in
 * kernel space */
/* Audited by: green(2002.06.15) */
static int write_page_by_tail (struct inode * inode, struct page * page,
			       unsigned count)
{
	return write_pages_by_item (inode, &page, 1, (int)count,
				    item_plugin_by_id (TAIL_ID));
}


/* for every page of file: read page, cut part of extent pointing to this page,
 * put data of page tree by tail item */
int extent2tail (struct file * file)
{
	int result;
	struct inode * inode;
	struct page * page;
	int num_pages;
	reiser4_key from;
	reiser4_key to;
	int i;
	unsigned count;
	int (*filler)(void *,struct page*) = 
		(int (*)(void *,struct page*))unix_file_readpage_nolock;

	/* collect statistics on the number of extent2tail conversions */
	reiser4_stat_file_add (extent2tail);
	
	inode = file->f_dentry->d_inode;

	get_exclusive_access (inode);

	if (inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN) &&
	    inode_get_flag (inode, REISER4_HAS_TAIL)) {
		drop_exclusive_access (inode);
		return 0;		
	}

	/* number of pages in the file */
	num_pages = (inode->i_size + PAGE_CACHE_SIZE - 1) / PAGE_CACHE_SIZE;

	unix_file_key_by_inode (inode, 0ull, &from);
	to = from;

	result = 0;

	for (i = 0; i < num_pages; i ++) {
		page = read_cache_page (inode->i_mapping, (unsigned)i, filler, file);
		if (IS_ERR (page)) {
			result = PTR_ERR (page);
			break;
		}

		wait_on_page_locked (page);

		if (!PageUptodate (page)) {
			page_cache_release (page);
			result = -EIO;
			break;
		}
		
		lock_page (page);

		if (page->mapping != inode->i_mapping) {
			/*
			 * page was asynchronously removed from the page cache
			 * while we were waiting for the lock. Re-try.
			 */
			unlock_page (page);
			page_cache_release (page);
			i --;
			continue;
		}

		/* cut part of file we have read */
		set_key_offset (&from, (__u64)(i << PAGE_CACHE_SHIFT));
		set_key_offset (&to, (__u64)((i << PAGE_CACHE_SHIFT) + PAGE_CACHE_SIZE - 1));
		result = cut_tree (tree_by_inode (inode), &from, &to);
		if (result) {
			unlock_page (page);
			page_cache_release (page);
			break;
		}
		/* put page data into tree via tail_write */
		count = PAGE_CACHE_SIZE;
		if (i == num_pages - 1)
			count = (inode->i_size & ~PAGE_CACHE_MASK) ? : PAGE_CACHE_SIZE;
		result = write_page_by_tail (inode, page, count);
		if (result) {
			unlock_page (page);
			page_cache_release (page);
			break;
		}
		/* release page, detach jnode if any */
		page -> mapping -> a_ops -> invalidatepage( page, 0 );
		remove_inode_page (page);
		unlock_page (page);
		page_cache_release (page);		
	}

	if (i == num_pages)
		/*
		 * FIXME-VS: not sure what to do when conversion did
		 * not complete
		 */
		inode_set_flag (inode, REISER4_HAS_TAIL);
	else
		warning ("nikita-2282", "Partial conversion of %li: %i of %i",
			 (long) inode->i_ino, i, num_pages);
	drop_exclusive_access (inode);
	return result;
}
