/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */


#include "../../reiser4.h"

/* this file contains:
 * - file_plugin methods of reiser4 unix file (REGULAR_FILE_PLUGIN_ID)
 */

/* plugin->u.file.write_flow = NULL
 * plugin->u.file.read_flow = NULL
 */


/* plugin->u.file.truncate */
int unix_file_truncate (struct inode * inode, loff_t size UNUSED_ARG)
{
	int result;
	reiser4_key from, to;

	assert ("vs-319", size == inode->i_size);

	build_sd_key (inode, &from);
	set_key_type (&from, KEY_BODY_MINOR);
	set_key_offset (&from, (__u64) inode->i_size);
	to = from;
	set_key_offset (&to, get_key_offset (max_key ()));
	
	/* all items of ordinary reiser4 file are grouped together. That is why
	   we can use cut_tree. Plan B files (for instance) can not be
	   truncated that simply */
	result = cut_tree (tree_by_inode (inode), &from, &to);
	if (result) {
		return result;
	}
	assert ("vs-637",
		inode_file_plugin( inode ) -> write_sd_by_inode ==
		common_file_save);
	if ((result = common_file_save (inode)))
		warning ("vs-638", "updating stat data failed\n");
	return result;
}


/* plugin->u.write_sd_by_inode = common_file_save */


static int find_item (reiser4_key * key, tree_coord * coord,
		      lock_handle * lh, znode_lock_mode lock_mode);

/* plugin->u.file.readpage
 * this finds item of file corresponding to page being read in and calls its
 * readpage method
 */
int unix_file_readpage (struct file * file UNUSED_ARG, struct page * page)
{
	int result;
	tree_coord coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin * iplug;
	struct readpage_arg arg;


	/* get key of first byte of the page */
	unix_file_key_by_inode (page->mapping->host,
				(loff_t)page->index << PAGE_CACHE_SHIFT, &key);
	
	init_lh (&lh);

	/* look for file metadata corresponding to first byte of page */
	result = find_item (&key, &coord, &lh, ZNODE_READ_LOCK);
	if (result != CBK_COORD_FOUND) {
		warning ("vs-280", "No file items found\n");
		done_lh (&lh);
		return result;
	}

	/* get plugin of found item */
	iplug = item_plugin_by_coord (&coord);
	if (!iplug->s.file.readpage) {
		done_lh (&lh);
		return -EINVAL;
	}

	arg.coord = &coord;
	arg.lh = &lh;
	result = iplug->s.file.readpage (&arg, page);

	done_lh (&lh);
	return result;
}


/* plugin->u.file.read */
ssize_t unix_file_read (struct file * file, char * buf, size_t size,
			loff_t * off)
{
	int result;
	struct inode * inode;
	tree_coord coord;
	lock_handle lh;
	size_t to_read;
	item_plugin * iplug;
	flow_t f;


	/* collect statistics on the number of reads */
	reiser4_stat_file_add (reads);

	inode = file->f_dentry->d_inode;
	result = 0;

	/* build flow */
	assert ("vs-528",
		inode_file_plugin (inode)->flow_by_inode == common_build_flow);

	result = common_build_flow (inode, buf, 1/* user space */, size,
				    *off, READ_OP, &f);
	if (result)
		return result;

	to_read = f.length;
	while (f.length) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/* do not read out of file */
			break;
		
		/* look for file metadata corresponding to position we read
		 * from */
		result = find_item (&f.key, &coord, &lh,
				    ZNODE_READ_LOCK);
		switch (result) {
		case CBK_COORD_FOUND:
			/* call read method of found item */
			iplug = item_plugin_by_coord (&coord);
			if (!iplug->s.file.read)
				result = -EINVAL;
			else
				result = iplug->s.file.read (inode, &coord,
							     &lh, &f);
			break;

		case CBK_COORD_NOTFOUND:
			/* item had to be found, as it was not - we have
			 * -EIO */
			result = -EIO;
		default:
			break;
		}
		done_lh (&lh);
		if (!result)
			continue;
		break;
	}

	/* update position in a file */
	*off += (to_read - f.length);
	/* return number of read bytes or error code if nothing is read */
	return (to_read - f.length) ? (to_read - f.length) : result;
}


/* these are write modes. Certain mode is chosen depending on resulting file
 * size and current metadata of file */
typedef enum {
	WRITE_EXTENT,
	WRITE_TAIL,
	CONVERT
} write_todo;

static write_todo unix_file_how_to_write (struct inode *, flow_t *, tree_coord *);
static int tail2extent (struct inode *);

/* plugin->u.file.write */
ssize_t unix_file_write (struct file * file,
			 char * buf, /* comments are needed */
			 size_t size,
			 loff_t * off)
{
	int result;
	struct inode * inode;
	tree_coord coord;
	lock_handle lh;	
	size_t to_write;
	item_plugin * iplug;
	flow_t f;
	

	/* collect statistics on the number of writes */
	reiser4_stat_file_add (writes);

	inode = file->f_dentry->d_inode;

	/* build flow */
	assert ("vs-481",
		inode_file_plugin (inode)->flow_by_inode == common_build_flow);

	result = common_build_flow (inode, buf, 1/* user space */, size, *off,
				    WRITE_OP, &f);
	if (result)
		return result;

	down_read (&reiser4_inode_data (inode)->sem);

	to_write = f.length;
	while (f.length) {
		/* look for file metadata corresponding to position we write
		 * to */
		result = find_item (&f.key, &coord, &lh, ZNODE_WRITE_LOCK);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/* error occured */
			done_lh (&lh);
			break;
		}

		switch (unix_file_how_to_write (inode, &f, &coord)) {
		case WRITE_EXTENT:
			iplug = item_plugin_by_id (EXTENT_POINTER_ID);
			/* resolves to extent_write function */

			result = iplug->s.file.write (inode, &coord, &lh, &f, 0);
			break;

		case WRITE_TAIL:
			iplug = item_plugin_by_id (TAIL_ID);
			/* resolves to tail_write function */

			result = iplug->s.file.write (inode, &coord, &lh, &f, 0);
			break;
			
		case CONVERT:
			done_lh (&lh);
			result = tail2extent (inode);
			if (result) {
				up_read (&reiser4_inode_data (inode)->sem);
				return result;
			}
			continue;

		default:
			impossible ("vs-293", "unknown write mode");
		}

		done_lh (&lh);
		if (!result || result == -EAGAIN)
			continue;
		break;
	}
	if( to_write - f.length ) {
		/* something was written. Update stat data */
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		assert ("vs-639",
			inode_file_plugin (inode)->write_sd_by_inode ==
			common_file_save);
		if (common_file_save (inode))
			warning ("vs-636", "updating stat data failed\n");
	}

	up_read (&reiser4_inode_data (inode)->sem);

	/* update position in a file */
	*off += (to_write - f.length);
	/* return number of written bytes or error code if nothing is written */
	return (to_write - f.length) ? (to_write - f.length) : result;
}


/*
 * look for item of file @inode corresponding to @key
 */
static int find_item (reiser4_key * key, tree_coord * coord,
		      lock_handle * lh, znode_lock_mode lock_mode)
{
	init_lh (lh);
	return coord_by_key (current_tree, key, coord, lh,
			     lock_mode, FIND_MAX_NOT_MORE_THAN,
			     TWIG_LEVEL, LEAF_LEVEL, CBK_UNIQUE | CBK_FOR_INSERT);
}


/*
 * unix files of reiser4 are built either of extents only or of tail items
 * only. If looking for file item coord_by_key stopped at twig level - file
 * consists of extents only, if at leaf level - file is built of extents only
 * FIXME-VS: it is possible to imagine different ways for finding that
 */
static int built_of_extents (struct inode * inode UNUSED_ARG,
			     tree_coord * coord)
{
	return znode_get_level (coord->node) == TWIG_LEVEL;
}


/* returns 1 if file of that size (@new_size) has to be stored in unformatted
 * nodes */
static int should_have_notail (struct inode * inode, loff_t new_size)
{
	if (!inode_tail_plugin (inode))
		return 1;
	return !inode_tail_plugin (inode)->have_tail (inode, new_size);

}


/* decide how to write flow @f into file @inode */
static write_todo unix_file_how_to_write (struct inode * inode, flow_t * f,
					  tree_coord * coord)
{
	loff_t new_size;


	/* size file will have after write */
	new_size = get_key_offset (&f->key) + f->length;

	if (new_size <= inode->i_size) {
		/* if file does not get longer - no conversion will be
		 * performed */
		if (built_of_extents (inode, coord))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}

	assert ("vs-377", inode_tail_plugin (inode)->have_tail);

	if (inode->i_size == 0) {
		/* no items of this file are in tree yet */
		assert ("vs-378", znode_get_level (coord->node) == LEAF_LEVEL);
		if (should_have_notail (inode, new_size))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}

	/* file is not empty and will get longer */
	if (should_have_notail (inode, new_size)) {
		/* that long file (@new_size bytes) is supposed to be built of
		 * extents */
		if (built_of_extents (inode, coord)) {
			/* it is built that way already */
			return WRITE_EXTENT;
		} else {
			/* file is built of tail items, conversion is
			 * required */
			return CONVERT;
		}
	} else {
		/* "notail" is not required, so keep file in its current form */
		if (built_of_extents (inode, coord))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}
}


/* part of tail2extent. @count bytes were copied into @page starting from the
 * beginning. mark all modifed buffers dirty and uptodate, mark others
 * uptodate */
static void mark_buffers_dirty (struct page * page, unsigned count)
{
	struct buffer_head * bh;

	assert ("vs-525", page->buffers);
	
	bh = page->buffers;
	do {
		if (count) {
			mark_buffer_dirty (bh);
			if (count < bh->b_size)
				count = 0;
			else
				count -= bh->b_size;
		}
		make_buffer_uptodate (bh, 1);
		bh = bh->b_this_page;
	} while (bh != page->buffers);
}


/* part of tail2extent. Cut all items covering @count bytes starting from
 * @offset */
static int cut_tail_items (struct inode * inode, loff_t offset, int count)
{
	reiser4_key from, to;


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
				int nr_pages, int count, item_plugin * iplug)
{
	flow_t f;
	tree_coord coord;
	lock_handle lh;
	int result;
	char * p_data;
	int i;
	int to_page;


	assert ("vs-604", ergo (iplug->h.id == TAIL_ID, nr_pages == 1));
	assert ("vs-564", iplug && iplug->s.file.write);

	to_page = PAGE_SIZE;
	result = 0;
	for (i = 0; i < nr_pages; i ++) {
		p_data = kmap (pages [i]);

		/* build flow */
		if (count > (int)PAGE_SIZE)
			to_page = (int)PAGE_SIZE;
		else
			to_page = count;
			
		inode_file_plugin (inode)->
			flow_by_inode (inode, p_data, 0/* not user space */,
				       (unsigned)to_page,
				       (loff_t)(pages[i]->index << PAGE_CACHE_SHIFT),
				       WRITE_OP, &f);

		do {
			result = find_item (&f.key, &coord, &lh, ZNODE_WRITE_LOCK);
			if (result != CBK_COORD_NOTFOUND && result != CBK_COORD_FOUND) {
				goto done;
			}

			result = iplug->s.file.write (inode, &coord, &lh, &f,
						      pages [i]);
			done_lh (&lh);
			/* item's write method may return -EAGAIN */
		} while (result == -EAGAIN);
		
		kunmap (pages [i]);
		/* page is written */
		count -= to_page;
	}

 done:
	/* result of write is 0 or error */
	assert ("vs-589", result <= 0);
	/* if result is 0 - all @count bytes is written completely */
	assert ("vs-588", ergo (result == 0, count == 0));
	return result;
}


/* part of tail2extent. @page contains data read from tail items. Those tail
 * items are removed from tree already. extent slot pointing to this page will
 * be created by using extent_write */
static int write_pages_by_extent (struct inode * inode, struct page ** pages,
				  int nr_pages, int count)
{
	return write_pages_by_item (inode, pages, nr_pages, count,
				    item_plugin_by_id (EXTENT_POINTER_ID));
}


static void drop_pages (struct page ** pages, int nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; i ++) {
		unlock_page (pages [i]);
		page_cache_release (pages [i]);
	}
}


/* part of tail2extent.  */
static int replace (struct inode * inode, struct page ** pages, int nr_pages,
		    int count)
{
	int result;
	int i;
	unsigned to_page;

	assert ("vs-596", nr_pages > 0 && pages [0]);

	/* cut copied items */
	result = cut_tail_items (inode, (loff_t)pages [0]->index << PAGE_CACHE_SHIFT,
				 count);
	if (result) {
		return result;
	}

	/* put into tree replacement for just removed items: extent item,
	 * namely */
	result = write_pages_by_extent (inode, pages, nr_pages, count);
	if (result) {
		return result;
	}
	
	/* mark buffers of pages (those only into which removed tail items were
	 * copied) dirty and all pages - uptodate */
	to_page = PAGE_SIZE;
	for (i = 0; i < nr_pages; i ++) {
		if (i == nr_pages - 1 && (count & ~PAGE_MASK))
			to_page = (count & ~PAGE_MASK);
		mark_buffers_dirty (pages [i], to_page);
		SetPageUptodate (pages [i]);
	}
	return 0;
}


#define TAIL2EXTENT_PAGE_NUM 3  /* number of pages to fill before cutting tail
				 * items */

static int all_pages_are_full (int nr_pages, int page_off)
{
	/* max number of pages is used and last one is full */
	return nr_pages == TAIL2EXTENT_PAGE_NUM && page_off == PAGE_SIZE;
}


/* part of tail2extent. */
static int file_is_over (struct inode * inode, reiser4_key * key,
			    tree_coord * coord)
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


static int tail2extent (struct inode * inode)
{
	int result;
	tree_coord coord;
	lock_handle lh;	
	reiser4_key key;     /* key of next byte to be moved to page */
	struct page * page;
	char * p_data;       /* data of page */
	int page_off,        /* offset within the page where to copy data */
		count,       /* number of bytes of item which can be copied to
			      * page */
		copied;      /* number of bytes of item copied already */
	struct page * pages [TAIL2EXTENT_PAGE_NUM];
	int nr_pages;        /* number of pages in the above array */
	int done;            /* set to 1 when all file is read */
	char * item;


	/* switch inode's rw_semaphore from read_down (set by unix_file_write)
	 * to write_down */
	up_read (&reiser4_inode_data (inode)->sem);
	down_write (&reiser4_inode_data (inode)->sem);

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
	while (1) {
		if (!item) {
			/* get next one */
			result = find_item (&key, &coord, &lh, ZNODE_READ_LOCK);
			if (result != CBK_COORD_FOUND) {
				drop_pages (pages, nr_pages);
				break;
			}
			if (item_id_by_coord (&coord) != TAIL_ID) {
				/* tail was converted by someone else */
				assert ("vs-597", get_key_offset (&key) == 0);
				assert ("vs-599", nr_pages == 0);
				result = 0;
				break;
			}
			item = item_body_by_coord (&coord);
			copied = 0;
		}
		assert ("vs-562", unix_file_owns_item (inode, &coord));

		if (!page) {
			assert ("vs-598",
				(get_key_offset (&key) & ~PAGE_MASK) == 0);
			page = grab_cache_page (inode->i_mapping,
						(unsigned long)(get_key_offset (&key) >>
								PAGE_CACHE_SHIFT));
			if (!page) {
				drop_pages (pages, nr_pages);
				result = -ENOMEM;
				break;
			}
			assert ("vs-603", !page->buffers);
			create_empty_buffers (page, inode->i_sb->s_blocksize);
			page_off = 0;
			pages [nr_pages] = page;
			nr_pages ++;
		}

		/* how many bytes to copy */
		count = item_length_by_coord (&coord) - copied;
		/* limit length of copy to end of page */
		if ((unsigned)count > PAGE_SIZE - page_off) {
			count = PAGE_SIZE - page_off;
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
			done_lh (&lh);
			/* replace tail items with extent */
			result = replace (inode, pages, nr_pages, 
					  (int)((nr_pages - 1) * PAGE_SIZE +
						page_off));
			drop_pages (pages, nr_pages);
			if (done || result) {
				/* conversion completed or error occured */
				goto out;
			}

			/* go for next set of pages */
			memset (pages, 0, sizeof (pages));
			nr_pages = 0;
			item = 0;
			page = 0;
			continue;
		}

		if (copied == item_length_by_coord (&coord)) {
			/* item is over, find next one */
			item = 0;
			done_lh (&lh);
		}
		if (page_off == PAGE_SIZE) {
			/* page is over */
			page = 0;
		}
	}

	done_lh (&lh);

 out:
	/* switch inode's rw_semaphore from write_down to read_down */
	up_write (&reiser4_inode_data (inode)->sem);
	down_read (&reiser4_inode_data (inode)->sem);

	return result;
}


/* plugin->u.file.release
 * convert all extent items into tail items */
static int extent2tail (struct file *);
int unix_file_release (struct file * file)
{
	struct inode * inode;

	inode = file ->f_dentry->d_inode;

	if (should_have_notail (inode, inode->i_size))
		return 0;

	return extent2tail (file);
}


/* part of extent2tail. Page contains data which are to be put into tree by
 * tail items. Use tail_write for this. flow is composed like in
 * unix_file_write. The only difference is that data for writing are in
 * kernel space */
static int write_page_by_tail (struct inode * inode, struct page * page,
			       unsigned count)
{
	return write_pages_by_item (inode, &page, 1, (int)count,
				    item_plugin_by_id (TAIL_ID));
}


/* for every page of file: read page, cut part of extent pointing to this page,
 * put data of page tree by tail item */
static int extent2tail (struct file * file)
{
	int result;
	struct inode * inode;
	struct page * page;
	int num_pages;
	reiser4_key from;
	reiser4_key to;
	int i;
	unsigned count;



	/* collect statistics on the number of extent2tail conversions */
	reiser4_stat_file_add (extent2tail);
	
	inode = file->f_dentry->d_inode;

	/* get an exclusive access to inode */
	down_write (&reiser4_inode_data (inode)->sem);

	/* number of pages in the file */
	num_pages = (inode->i_size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (!num_pages) {
		up_write (&reiser4_inode_data (inode)->sem);
		return 0;
	}

	unix_file_key_by_inode (inode, 0ull, &from);
	to = from;

	{
		/* find which items file is built of */
		tree_coord coord;
		lock_handle lh;
		int do_conversion;
		
		do_conversion = 0;
		result = find_item (&from, &coord, &lh, ZNODE_READ_LOCK);
		if (result == CBK_COORD_FOUND) {
			if (item_id_by_coord (&coord) == EXTENT_POINTER_ID) {
				do_conversion = 1;
			}
		}
		done_lh (&lh);
		if (!do_conversion) {
			assert ("vs-590", CBK_COORD_FOUND == 0);
			/* error occured or file is built of tail items
			 * already */
			up_write (&reiser4_inode_data (inode)->sem);
			return result;
		}
	}

	result = 0;

	for (i = 0; i < num_pages; i ++) {
		page = grab_cache_page (inode->i_mapping, (unsigned long)i);
		if (!page) {
			if (i)
				warning ("vs-550", "file left extent2tail-ed converted partially");
			result = -ENOMEM;
			break;
		}
		result = unix_file_readpage (file, page);
		if (result) {
			unlock_page (page);
			page_cache_release (page);
			break;
		}
		wait_on_page (page);

		if (!Page_Uptodate (page)) {
			page_cache_release (page);
			result = -EIO;
			break;
		}
		
		lock_page (page);

		/* cut part of file we have read */
		set_key_offset (&from, (__u64)(i << PAGE_CACHE_SHIFT));
		set_key_offset (&to, (__u64)((i << PAGE_CACHE_SHIFT) + PAGE_SIZE - 1));
		result = cut_tree (tree_by_inode (inode), &from, &to);
		if (result) {
			unlock_page (page);
			page_cache_release (page);
			break;
		}
		/* put page data into tree via tail_write */
		count = PAGE_SIZE;
		if (i == num_pages - 1)
			count = (inode->i_size & ~PAGE_MASK) ? : PAGE_SIZE;
		result = write_page_by_tail (inode, page, count);
		if (result) {
			unlock_page (page);
			page_cache_release (page);
			break;
		}
		/* remove page from page cache */
		lru_cache_del (page);
		remove_inode_page (page);
		unlock_page (page);
		page_cache_release (page);		
	}

	up_write (&reiser4_inode_data (inode)->sem);
	return result;
}


/* plugin->u.file.flow_by_inode  = common_build_flow */


/* plugin->u.file.key_by_inode */
int unix_file_key_by_inode ( struct inode *inode, loff_t off, reiser4_key *key )
{
	build_sd_key (inode, key);
	set_key_type (key, KEY_BODY_MINOR );
	set_key_offset (key, ( __u64 ) off);
	return 0;
}


/*
 * plugin->u.file.set_plug_in_sd = NULL
 * plugin->u.file.set_plug_in_inode = NULL
 * plugin->u.file.create_blank_sd = NULL
 */


/* plugin->u.file.create
 * create sd for unix file. Just pass control to
 * fs/reiser4/plugin/object.c:common_file_save()
 */
int unix_file_create( struct inode *object, struct inode *parent UNUSED_ARG,
		      reiser4_object_create_data *data UNUSED_ARG )
{
	assert( "nikita-744", object != NULL );
	assert( "nikita-745", parent != NULL );
	assert( "nikita-747", data != NULL );
	assert( "nikita-748", 
		*reiser4_inode_flags( object ) & REISER4_NO_STAT_DATA );
	assert( "nikita-749", data -> id == REGULAR_FILE_PLUGIN_ID );
	
	return common_file_save( object );
}


/* plugin->u.file.destroy_stat_data = NULL
 * plugin->u.file.add_link = NULL
 * plugin->u.file.rem_link = NULL
 */


/* plugin->u.file.owns_item 
 * this is common_file_owns_item with assertion */
int unix_file_owns_item( const struct inode *inode /* object to check
						    * against */, 
			 const tree_coord *coord /* coord to check */ )
{
	int result;

	result = common_file_owns_item (inode, coord);
	if (!result)
		return 0;
	if (item_type_by_coord (coord) != ORDINARY_FILE_METADATA_TYPE)
		return 0;
	assert ("vs-547", (item_id_by_coord (coord) == EXTENT_POINTER_ID ||
			   item_id_by_coord (coord) == TAIL_ID));
	return 1;
}


/* plugin->u.file.can_add_link = common_file_can_add_link
 */


#if 0

/* 

Plugin coding style note:


It is possible to write a function that looks like:

generic_write()
{
  state = pick_plugin_specific_state_initialization();
  while (write_not_done(state))
    {
      action = do_plugin_specific_action_selection(state);
      do_it(action, state);
      do_plugin_specific_state_update(state);
    }

}

I think this is actually bad style.  

The more indirections you have, the more cognitive cost to understanding the code there is.
Just try to imagine that for a specific plugin you are trying to understand what it does,
tracing each step being performed.

I mean, what code is being shared here, the while loop?  How much is that worth?  Ok, I
made it worse than it would have been in reality for the sake of rhetoric, but there is
a point here I am trying to make.  It even applies to some code I saw not too far back
in the mongo benchmark.

Much better to have different write functions for each plugin, encourage their use as
templates, and encourage them to use many of the same subfunctions.  The above is an
exaggeration of a style problem sometimes seen, one that I think we should avoid for
plugins.  I don't think we should have generic_* plugin operations.

On the other hand, I think that unitype_insert_flow was rightly coded
here as one function that handles both insertions and appends, because
the logic for both was the same except for the choice of which item
operation they call. 

In other words, employ a good sense of balance when coding.

*/

ordinary_file_tail_pack()
{
if (file is less than 16k long && stored in extents)
convert exents to body items using insert_unitype_flow;
}


/* DELETE THIS DUPLICATE FUNCTION HANS AFTER READING (already coded pseudo-code) */
/* takes a flow, and containerizes it into items that are inserted into the
 tree.  The items all have item_handler as their item_handler.  */
insert_error insert_unitype_flow(flow * flow, insert_coord insert_coord, dancing_tree * tree, item_handler item_handler,  insertion_policy_mode policy, insert_unitype_flow_mode insert_or_append) 
{
	char * cursor, end;
	cursor = flow->data;
	end = flow->data + length;
	plugin->initialize_flow(flow);
  
	/* this could be further optimized, but let's keep complexity minimal at first -Hans */
				/* This code assumes that insert_flow
                                   updates the insert_coord on
                                   completion if the flow is not fully
                                   inserted, and that if the node has
                                   been filled, the insert_coord is
                                   set to be within the right neighbor
                                   of the just filled node.  */
	while (cursor<end)
	{
				/* If optimizing for repeated insertions
                                   (which implies not appending) then if the
                                   insert_coord is between nodes and the left
                                   node of the nodes it is between is full,
                                   then insert a new node even if the right
                                   node of the nodes it is between is not
                                   full.  This avoids endless copying of items
                                   after the insertion coord in the node as
                                   repeated insertions are made. */
		if (policy == REPEAT_INSERTIONS && insert_coord_before_first_item(insert_coord) && full_node(left_neighbor(insert_coord)))
			insert_new_node(insert_coord);
		space_needed = space_needed(flow, node_layout, node, item_handler);
		if (insert_coord->insertion_node->free_space > space_needed)
			insert(insert_or_append, flow, insert_coord, node);
		else
		{
			/* shift everything possible to the left of and including insertion coord into the left neighbor */
			shift_left_of_and_including_insert_coord(insert_coord);
			space_needed = space_needed(flow, node_layout, node, item_handler);
			if (insert_coord->insertion_node->free_space > space_needed)
				insert(insert_or_append, flow, insert_coord, node);
			else
			{
				/* shift everything that you can to the right of the insertion coord into the right neighbor */
				/* notice, the insert_coord itself may
                                   have been shifted into what was the
                                   left neighbor in the previous step,
                                   but this algorithm will (should)
                                   still work as shift_right will be a
                                   no_op.*/
				right_shift_result = shift_right_of_but_excluding_insert_coord(insert_coord);
				if (right_shift_result == SHIFTED_SOMETHING)
					space_needed = space_needed(flow, node_layout, node, item_handler);
				if (insert_coord->insertion_node->free_space > space_needed)
					insert(insert_or_append, flow, insert_coord, node);
				else
					if (right_shift_result equals items still to the right of the insert coord in this node)
					{
						insert_new_node();
						right_shift_result = shift_right_of_but_excluding_insertion_coord(insert_coord);
						space_needed = space_needed(flow, node_layout, node, item_handler);
				/* this is nonintuitive and
                                   controversial, we are inserting two
                                   nodes when one might be enough,
                                   because we are optimizing for
                                   further insertions at what will
                                   next become the insert coord by not
                                   having it contain any items to the
                                   right of the insert_coord.  this
                                   depends on balance on commit to be
                                   efficient.  */
						if (insert_coord->insertion_node->free_space < space_needed)
						{
							policy = REPEAT_INSERTIONS;
						}
					}
				insert(insert_or_append, flow, insert_coord, node);	
		
			}
		}
	}
}

write
{
plugin->write
	{
		loop over pages {
			page cache write {}
			or overwrite
			or tree insertion {
				examining whole of what is to be inserted into tree up to next page cache write 
					and layout of nodes near insert coord
					choose optimal method of inserting items
					and invoke methods for inserting them
			}
		}
	}
}



ordinary_file_close()
{
  do usual VFS layer file close stuff;
  ordinary_file_tail_pack();
}

/* nikita-742's range read code should go here, nikita-743 please insert and
   remove corresponding pseudo-code. Nikita-BIGNUM will do this after bio will
   stabilize in 2.5 */


generic_readrange()
{
	if (readpagerange supported)
    {
      while (not all pages to be read have been read or found in page cache)
	{
	  while (page in range to be read is in page cache)
	    do nothing;
	  if (more needs to be read) 
	    while (page in range is not in page cache)
	      increment range to be read from filesystem;
	  reiserfs_read_range (index, nr_blocks_needed_to_be_read, file_readahead, packing_locality_readahead);
	}
    }

}
				/* read_error is non_null if partial
                                   read occurs */
read_error ordinary_file_read_flow(		    )
{
				/* in 4.1, when reading metadata, lock
				   the disk while waiting for metadata
				   read to complete, and then resume
				   rest of read */


/* page cache interaction:

Compute what one must read, and then as something recorded separate compute how much to readahead.
Check one page at a time that pages to be read are not in the page cache, then
send to reiserfs_read (a request for a range that is not in the page cache and needs to be read 
plus a request for how much to read_ahead iff the read_ahead involves reading blocks of the file that are in logical block number sequence
plus a request for how much to read_ahead iff the read_ahead involves reading blocks of the file that are NOT in logical block number sequence
plus a request for how much to read_ahead past the end of the file into the rest of the packing locality iff the read_ahead involves reading blocks of the packing locality that are in sequence)

 */

}

read_range()
{
synchronous read the flow range;
  async read_ahead within file;
/* the looking to see if it is near any blocknumber still in the disk
   I/O queue is an optimization that can be deferred by the coder
   until 4.1 */
  async longer read_ahead within file of block sequences near (either the last blocknumber read, or near any blocknumber still in the disk I/O queue);
  async read_ahead within packing locality of block sequences if near (either the last blocknumber read, or near any blocknumber still in the disk I/O queue);
}


#endif /* 0 */

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
