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
	struct sealed_coord hint;


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

		while (f.length) {
			result = find_next_item (0, &f.key, &coord, &lh, 
						 ZNODE_WRITE_LOCK,
						 CBK_UNIQUE | CBK_FOR_INSERT);
			if (result != CBK_COORD_NOTFOUND && 
			    result != CBK_COORD_FOUND) {
				goto done;
			}

			assert ("vs-957", ergo (result == CBK_COORD_NOTFOUND,
						get_key_offset (&f.key) == 0));
			assert ("vs-958", ergo (result == CBK_COORD_FOUND,
						get_key_offset (&f.key) != 0));

			set_hint (&hint, &f.key, &coord);
			done_lh (&lh);

			result = iplug->s.file.write (inode, &hint, &f,
						      pages [i]);
			if (result)
				goto done;
			
		}

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
#if 0
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
#endif

int tail2extent (struct inode * inode)
{
	int result;
	reiser4_key key;     /* key of next byte to be moved to page */
	reiser4_key tmp;
	char * p_data;       /* data of page */
	unsigned page_off,   /* offset within the page where to copy data */
		count,       /* number of bytes of item which can be
			      * copied to page */
		copied;      /* number of bytes of item copied already */
	struct page * pages [TAIL2EXTENT_PAGE_NUM];
	unsigned nr_pages;        /* number of pages in the above array */
	int done;            /* set to 1 when all file is read */
	char * item;
	int i;


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
	page_off = 0;
	item = 0;
	copied = 0;

	done = 0;
	while (!done) {
		memset (pages, 0, sizeof (pages));
		nr_pages = 0;
		for (i = 0; i < sizeof_array (pages) && !done; i ++) {
			assert ("vs-598",
				(get_key_offset (&key) & ~PAGE_CACHE_MASK) == 0);
			pages [i] = grab_cache_page (inode->i_mapping,
						     (unsigned long)(get_key_offset (&key) >>
								     PAGE_CACHE_SHIFT));
			if (!pages [i]) {
				drop_pages (pages, i);
				result = -ENOMEM;
				goto error;
			}

			page_off = 0;

			while (page_off < PAGE_CACHE_SIZE) {
				coord_t coord;
				lock_handle lh;

				/* get next item */
				coord_init_zero (&coord);
				init_lh (&lh);
				result = find_next_item (0, &key, &coord, &lh,
							 ZNODE_READ_LOCK, CBK_UNIQUE);
				if (result != CBK_COORD_FOUND) {
					drop_pages (pages, nr_pages);
					if (result == CBK_COORD_NOTFOUND &&
					    get_key_offset (&key) == 0)
						/* conversion can be called for
						 * empty file */
						result = 0;
					goto error;
				}
				if (coord.between == AFTER_UNIT) {
					char *kaddr;
					/*
					 * FIXME-VS: this is more save way to
					 * detect end of file
					 */
					done_lh (&lh);
					kaddr = kmap_atomic(pages [i], KM_USER0);
					memset (kaddr + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic (kaddr, KM_USER0);
					done = 1;
					break;
				}
				
				result = zload (coord.node);
				if (result) {
					done_lh (&lh);
					drop_pages (pages, nr_pages);
				}
				assert ("vs-562", unix_file_owns_item (inode, &coord));
				assert ("vs-856", coord.between == AT_UNIT);
				assert ("green-11",
					keyeq (&key, unit_key_by_coord (&coord, &tmp)));
				if (item_id_by_coord (&coord) != TAIL_ID) {
					/*
					 * something other than tail found
					 * This is only possible when first item of a file found during call to reiser4_mmap.
					 */
					if (get_key_offset (&key) == (__u64)0) {
						if (item_id_by_coord (&coord) != EXTENT_POINTER_ID)
							result = -EIO;
						else
							result = 0;
					} else
						result = -EIO;
					zrelse (coord.node);
					done_lh (&lh);
					drop_pages (pages, nr_pages);
					goto error;
				}
				item = item_body_by_coord (&coord) + coord.unit_pos;
				
				/* how many bytes to copy */
				count = item_length_by_coord (&coord) - coord.unit_pos;
				/* limit length of copy to end of page */
				if (count > PAGE_CACHE_SIZE - page_off)
				count = PAGE_CACHE_SIZE - page_off;
			
				/* kmap/kunmap are necessary for pages which
				 * are not addressable by direct kernel virtual
				 * addresses */
				p_data = kmap (pages [i]);
				/* copy item (as much as will fit starting from
				 * the beginning of the item) into the page */
				memcpy (p_data + page_off, item, (unsigned)count);
				kunmap (pages [i]);
				
				page_off += count;
				set_key_offset (&key, get_key_offset (&key) + count);
				
				zrelse (coord.node);
				done_lh (&lh);
				
				if (get_key_offset (&key) == inode->i_size) {
					/*
					 * FIXME-VS: this can be used to detect
					 * end of file
					 */
					memset (kmap (pages [i]) + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap (pages [i]);
					done = 1;
					/*break;*/
				}
			} /* while */
		} /* for */

		result = replace (inode, pages, i, 
				  (int)((i - 1) * PAGE_CACHE_SIZE +
					page_off));
		drop_pages (pages, i);
		if (result)
			goto error;
	}

	inode_set_flag (inode, REISER4_TAIL_STATE_KNOWN);
	inode_clr_flag (inode, REISER4_HAS_TAIL);

 ok:
	drop_exclusive_access (inode);
	get_nonexclusive_access (inode);
	
	/* It is advisabel to check here that all grabbed pages were freed */

	/* file can not be converted back to tails, because tail */
	assert ("vs-830", (inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN) &&
			   !inode_get_flag (inode, REISER4_HAS_TAIL)));

	return 0;

 error:
	drop_exclusive_access (inode);
	get_nonexclusive_access (inode);
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
		if (PagePrivate (page)) {
			page -> mapping -> a_ops -> invalidatepage( page, 0 );
		}
		clear_page_dirty(page);
		ClearPageUptodate(page);
		remove_from_page_cache (page);
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
