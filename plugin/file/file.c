/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */


#include "../../reiser4.h"

/* this file contains:
 * - file_plugin methods of reiser4 ordinary file (REGULAR_FILE_PLUGIN_ID)
 */

/* plugin->u.file.write_flow = NULL
 * plugin->u.file.read_flow = NULL
 */


/* plugin->u.file.truncate */
int ordinary_file_truncate (struct inode * inode, loff_t size UNUSED_ARG)
{
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
	return cut_tree (tree_by_inode (inode), &from, &to);
}


/* plugin->u.write_sd_by_inode = common_file_save */


/* plugin->u.file.readpage
 * this finds item of file corresponding to page being read in and calls its
 * readpage method
 */
static int find_item (reiser4_key * key, tree_coord * coord,
		      lock_handle * lh, znode_lock_mode lock_mode);

int ordinary_readpage (struct file * file UNUSED_ARG, struct page * page)
{
	int result;
	tree_coord coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin * iplug;
	struct readpage_arg arg;


	build_sd_key (page->mapping->host, &key);
	set_key_type (&key, KEY_BODY_MINOR);
	set_key_offset (&key, (unsigned long long)page->index << PAGE_SHIFT);

	init_coord (&coord);
	init_lh (&lh);

	result = find_item (&key, &coord, &lh, ZNODE_READ_LOCK);
	if (result != CBK_COORD_FOUND) {
		warning ("vs-280", "No file items found\n");
		done_lh (&lh);
		done_coord (&coord);
		return result;
	}

	/*
	 * get plugin of found item
	 */
	iplug = item_plugin_by_coord (&coord);
	if (!iplug->s.file.readpage) {
		done_lh (&lh);
		done_coord (&coord);
		return -EINVAL;
	}

	arg.coord = &coord;
	arg.lh = &lh;
	result = iplug->s.file.readpage (&arg, page);

	done_lh (&lh);
	done_coord (&coord);
	return result;
}


/* plugin->u.file.read */
/*
 * FIXME: This comment is out of date, there is no call to read_cache_page:
 
   this calls mm/filemap.c:read_cache_page() for every page spanned by the
   flow @f. This read_cache_page allocates a page if it does not exist and
   calls a function specified by caller (we specify reiser4_ordinary_readpage
   here) to try to fill that page if it is not
   uptodate. reiser4_ordinary_readpage looks through the tree to find metadata
   corresponding to the beginning of a page being read. If that metadata is
   found - fill_page method of item plugin is called to do mapping of not
   mapped yet buffers the page is built off and issue read of not uptodate
   buffers. When read is issued - and we are forced to wait - extent's
   fill_page fills few more pages in advance using the rest of extent it
   stopped at and starts reading for them. This will cause only reading ahead
   of blocks adjacent to block which must be read anyway. So, except for
   allocation of extra pages there will be no delay in delivering of the page
   we asked about.
 */
ssize_t ordinary_file_read (struct file * file, char * buf, size_t size,
			    loff_t * off)
{
	int result;
	struct inode * inode;
	tree_coord coord;
	lock_handle lh;
	size_t to_read;
	file_plugin * fplug;
	item_plugin * iplug;
	flow_t f;


	/*
	 * collect statistics on the number of reads
	 */
	reiser4_stat_file_add (reads);

	inode = file->f_dentry->d_inode;
	result = 0;

	/*
	 * build flow
	 */
	assert ("vs-528", inode_file_plugin (inode));
	fplug = inode_file_plugin (inode);
	if (!fplug->flow_by_inode)
		return -EINVAL;

	result = fplug->flow_by_inode (inode, buf, 1/* user space */, size, *off,
				       READ_OP, &f);
	if (result)
		return result;

	to_read = f.length;
	while (f.length) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/*
			 * do not read out of file
			 */
			break;
		
		result = find_item (&f.key, &coord, &lh,
				    ZNODE_READ_LOCK);
		switch (result) {
		case CBK_COORD_FOUND:
			iplug = item_plugin_by_coord (&coord);
			if (!iplug->s.file.read)
				result = -EINVAL;
			else
				result = iplug->s.file.read (inode, &coord,
							     &lh, &f);
			break;

		case CBK_COORD_NOTFOUND:
			/* 
			 * item had to be found, as it was not - we have -EIO
			 */
			result = -EIO;
		default:
			break;
		}
		done_lh (&lh);
		done_coord (&coord);
		if (!result)
			continue;
		break;
	}

	*off += (to_read - f.length);
	return (to_read - f.length) ? (to_read - f.length) : result;
}


/* plugin->u.file.write */
typedef enum {
	WRITE_EXTENT,
	WRITE_TAIL,
	CONVERT
} write_todo;

static write_todo what_todo (struct inode *, flow_t *, tree_coord *);
static int tail2extent (struct inode *, tree_coord *, lock_handle *);

ssize_t ordinary_file_write (struct file * file, char * buf, size_t size,
			     loff_t * off)
{
	int result;
	struct inode * inode;
	tree_coord coord;
	lock_handle lh;	
	size_t to_write;
	file_plugin * fplug;
	item_plugin * iplug;
	flow_t f;
	

	/*
	 * collect statistics on the number of writes
	 */
	reiser4_stat_file_add (writes);

	inode = file->f_dentry->d_inode;
	result = 0;

	/*
	 * build flow
	 */
	assert ("vs-481", inode_file_plugin (inode));
	fplug = inode_file_plugin (inode);
	if (!fplug->flow_by_inode)
		return -EINVAL;

	result = fplug->flow_by_inode (inode, buf, 1/* user space */, size, *off,
				       WRITE_OP, &f);
	if (result)
		return result;

	to_write = f.length;
	while (f.length) {
		result = find_item (&f.key, &coord, &lh, ZNODE_WRITE_LOCK);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/*
			 * error occured
			 */
			done_lh (&lh);
			done_coord (&coord);
			break;
		}
		switch (what_todo (inode, &f, &coord)) {
		case WRITE_EXTENT:
			iplug = item_plugin_by_id (EXTENT_POINTER_ID);
			/* resolves to extent_write function */

			result = iplug->s.file.write (inode, &coord, &lh, &f);
			break;

		case WRITE_TAIL:
			iplug = item_plugin_by_id (TAIL_ID);
			/* resolves to tail_write function */

			result = iplug->s.file.write (inode, &coord, &lh, &f);
			break;
			
		case CONVERT:
			result = tail2extent (inode, &coord, &lh);
			break;

		default:
			impossible ("vs-293", "unknown write mode");
		}

		done_lh (&lh);
		done_coord (&coord);
		if (!result || result == -EAGAIN)
			continue;
		break;
	}

	/*
	 * FIXME-VS: remove after debugging insert_extent is done
	 */
	print_tree_rec ("WRITE", tree_by_inode (inode), REISER4_NODE_CHECK);

	
	*off += (to_write - f.length);
	return (to_write - f.length) ? (to_write - f.length) : result;
}


/*
 * look for item of file @inode corresponding to @key
 */
static int find_item (reiser4_key * key, tree_coord * coord,
		      lock_handle * lh, znode_lock_mode lock_mode)
{
	init_coord (coord);
	init_lh (lh);
	return coord_by_key (current_tree, key, coord, lh,
			     lock_mode, FIND_MAX_NOT_MORE_THAN,
			     LEAF_LEVEL, LEAF_LEVEL, CBK_UNIQUE | CBK_FOR_INSERT);
}


/*
 * ordinary files of reiser4 are built either of extents only or of tail items
 * only. If looking for file item coord_by_key stopped at twig level - file
 * consists of extents only, if at leaf level - file is built of extents only
 * FIXME-VS: it is possible to imagine different ways for finding that
 */
static int built_of_extents (struct inode * inode UNUSED_ARG,
			     tree_coord * coord)
{
	return znode_get_level (coord->node) == TWIG_LEVEL;
}


/* all file data have to be stored in unformatted nodes */
static int should_have_notail (struct inode * inode, loff_t new_size)
{
	assert ("vs-549", inode_tail_plugin (inode));
	return !inode_tail_plugin (inode)->have_tail (inode, new_size);

}


/* decide how to write @count bytes to position @offset of file @inode */
static write_todo what_todo (struct inode * inode, flow_t * f, tree_coord * coord)
{
	loff_t new_size;


	/*
	 * size file will have after write
	 */
	new_size = get_key_offset (&f->key) + f->length;

	if (new_size <= inode->i_size) {
		/*
		 * if file does not get longer - no conversion will be
		 * performed
		 */
		if (built_of_extents (inode, coord))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}

	assert ("vs-377", inode_tail_plugin (inode)->have_tail);

	if (inode->i_size == 0) {
		/*
		 * no items of this file are in tree yet
		 */
		assert ("vs-378", znode_get_level (coord->node) == LEAF_LEVEL);
		if (should_have_notail (inode, new_size))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}

	/*
	 * file is not empty and will get longer
	 */
	if (should_have_notail (inode, new_size)) {
		/*
		 * that long file (@new_size bytes) is supposed to be built of
		 * extents
		 */
		if (built_of_extents (inode, coord)) {
			/*
			 * it is built that way already
			 */
			return WRITE_EXTENT;
		} else {
			/*
			 * file is built of tail items, conversion is required
			 */
			return CONVERT;
		}
	} else {
		/*
		 * "notail" is not required, so keep file in its current form
		 */
		if (built_of_extents (inode, coord))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}
}


/* @count bytes were copied into page starting from the beginning. mark all
 * modifed buffers dirty and uptodate, mark others uptodate */
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


#ifdef TAIL2EXTENT_ALL_AT_ONCE

NOT THIS

typedef struct {
	struct inode * inode; /* inode of file being tail2extent converted */
	struct page * page;   /* page being filled currently */
	/* FIXME-VS: tail2extent creates pages which do not have extent item
	 * pointing to them. Do those page need to be collected somehow while
	 * coping data into them? */
	unsigned offset;           /* offset within that page */
	reiser4_key next;     /* key of next byte to be copied to page cache */
} copy2page_arg;


/* iterate_tree actor */
static int copy2page (reiser4_tree * tree UNUSED_ARG /* tree scanned */,
		      tree_coord * coord /* current coord */,
		      lock_handle * lh UNUSED_ARG /* current lock handle */,
		      void * p /* copy2page arguments */ )
{
	reiser4_key key;
	copy2page_arg * arg;
	int left;
	char * item;

	arg = (copy2page_arg *)p;
	item_key_by_coord (coord, &key);

	assert ("vs-522", inode_file_plugin (arg->inode));
	assert ("vs-523", inode_file_plugin (arg->inode)->owns_item);

	if (inode_file_plugin (arg->inode)->owns_item (arg->inode, coord)) {
		assert ("vs-548", item_id_by_coord (coord) != TAIL_ID);

		left = item_length_by_coord (coord);
		item = item_body_by_coord (coord);

		while (left) {
			int count;
			char * kaddr;
			
			if (arg->offset == PAGE_SIZE) {
				UnlockPage (arg->page);
				page_cache_release (arg->page);			
				arg->page = 0;
				arg->offset = 0;
			}
			if (!arg->page) {
				arg->page = grab_cache_page (arg->inode->i_mapping,
							     (unsigned long)(get_key_offset (&arg->next) >> PAGE_SHIFT));
				if (!arg->page) {
					return -ENOMEM;
				}
				assert ("vs-524", !arg->page->buffers);
				create_empty_buffers (arg->page,
						      reiser4_get_current_sb ()->s_blocksize);
			}
			count = left;
			if (arg->offset + count > (int)PAGE_SIZE)
				count = PAGE_SIZE - arg->offset;
			
			kaddr = kmap (arg->page);
			memcpy (kaddr + arg->offset, item, (unsigned)count);
			kunmap (arg->page);
			
			arg->offset += count;
			item += count;
			left -= count;
			set_key_offset (&arg->next,
					get_key_offset (&arg->next) + count);
		}

		if (arg->inode->i_size != (loff_t)get_key_offset (&arg->next)) {
			/* there can be at least one more item to be read */
			return 1;
		}
	}

	/* item of another file encountered or last of items to be read was
	 * read, ask iterate_tree to break */

	/* there should have been be at least one item read */
	assert ("vs-518", arg->page);
	/* padd the last page with 0s */
	memset (kmap (arg->page) + arg->offset, 0, PAGE_SIZE - arg->offset);
	mark_buffers_dirty (arg->page, arg->offset);
	SetPageUptodate (arg->page);
	kunmap (arg->page);
	UnlockPage (arg->page);
	page_cache_release (arg->page);	

	return 0;
}


/* read all direct items of file into newly created pages
 * remove all those item from the tree
 * insert extent item */
static int tail2extent (struct inode * inode, tree_coord * coord, 
			lock_handle * lh)
{
	int result;
	file_plugin * fplug;
	reiser4_key key;
	copy2page_arg arg;
	reiser4_item_data extent_data;


	/* collect statistics on the number of tail2extent conversions */
	reiser4_stat_file_add (tail2extent);


	fplug = inode_file_plugin (inode);
	if (!fplug || !fplug->key_by_inode) {
		return -EINVAL;
	}
	/* get key of first byte of a file */
	result = fplug->key_by_inode (inode, 0ull, &key);
	if (result) {
		return result;
	}

	done_lh (lh);
	done_coord (coord);
	

	result = find_item (&key, coord, lh, ZNODE_READ_LOCK);
	if (result != CBK_COORD_FOUND) {
		return result;
	}

	/* for all items of the file starting from the found one run copy2page */
	arg.inode = inode;
	arg.page = 0;
	arg.offset = 0;
	arg.next = key;
	result = iterate_tree (tree_by_inode (inode), coord, lh,
			       copy2page, &arg, ZNODE_READ_LOCK, 0/*through items*/);
	done_lh (lh);
	done_coord (coord);
	if (result && result != -ENAVAIL) {
		/* error occured. -ENAVAIL means "no right neighbor or it is an
		 * unformatted node" which is not an error here */
		return result;
	}

	/* cut items which were read and copied into page cache */	
	result = cut_tree (tree_by_inode (inode), &key, &arg.next);
	if (result)
		return result;

	/* insert an unallocated extent. We do not make body of extent item
	 * here. Width of extent is passed via extent_data.arg. extent_init
	 * will set extent state (unallocated) and its width directly */
	extent_data.data = 0;
	extent_data.user = 0;
	extent_data.length = sizeof (reiser4_extent);
	/* width of extent to be inserted */
	assert ("vs-533", get_key_offset (&arg.next) > 0);
	extent_data.arg = (void *)(unsigned long)((get_key_offset (&arg.next) - 1) / inode->i_sb->s_blocksize + 1);
	extent_data.iplug = item_plugin_by_id (EXTENT_POINTER_ID);

	result = find_item (&key, coord, lh, ZNODE_WRITE_LOCK);
	if (result != CBK_COORD_NOTFOUND) {
		return result;
	}
	return insert_extent_by_coord (coord, &extent_data, &key, lh);
}

#else /* TAIL2EXTENT_ITEM_BY_ITEM */

/* part of tail2extent. It returns true if coord is set to not last item of
 * file and tail2extent must continue */
static int must_continue (struct inode * inode, reiser4_key * key,
			  const tree_coord * coord UNUSED_ARG)
{
	reiser4_key coord_key;

	assert ("vs-567", item_id_by_coord (coord) == TAIL_ID);
	assert ("vs-566", inode->i_size >= (loff_t)get_key_offset (key));
	item_key_by_coord (coord, &coord_key);
	set_key_offset (&coord_key,
			get_key_offset (&coord_key) +
			item_length_by_coord (&coord_key));
	assert ("vs-568", keycmp (key, &coord_key) == EQUAL_TO);
	/*
	 * FIXME-VS: do we need to try harder?
	 */
	return inode->i_size == (loff_t)get_key_offset (key);
		
}


/* part of tail2extent. Cut all items of which given page consists */
static int cut_tail_page (struct page * page)
{
	struct inode * inode;
	reiser4_key from, to;

	inode = page->mapping->host;

	inode_file_plugin (inode)->key_by_inode (inode,
						 (loff_t)page->index << PAGE_SHIFT,
						 &from);
	to = from;
	set_key_offset (&to, get_key_offset (&to) + PAGE_SIZE - 1);
	return cut_tree (tree_by_inode (inode), &from, &to);
}


/* part of tail2extent. Page contains data read from tail items. Those tail
 * items are removed from tree already. extent slot pointing to this page will
 * be created by using extent_write. The difference between usual usage of
 * extent_write and how it is used here is that extent_write does not have to
 * deal with page at all. This is done by setting flow->data to 0 */
static int write_page_by_extent (struct inode * inode, struct page * page,
				 unsigned count)
{
	flow_t f;
	tree_coord coord;
	lock_handle lh;
	item_plugin * iplug;
	int result;


	inode_file_plugin (inode)->
		flow_by_inode (inode, 0/* data */, 0/* kernel space */,
			       count, (loff_t)(page->index << PAGE_SHIFT),
			       WRITE_OP, &f);

	result = find_item (&f.key, &coord, &lh, ZNODE_WRITE_LOCK);
	if (result != CBK_COORD_NOTFOUND) {
		assert ("vs-563", result != CBK_COORD_FOUND);
		page_cache_release (page);
		return result;
	}

	iplug = item_plugin_by_id (EXTENT_POINTER_ID);
	assert ("vs-564", iplug && iplug->s.file.write);
	result = iplug->s.file.write (inode, &coord, &lh, &f);
	done_lh (&lh);
	done_coord (&coord);
	if (result != (int)count) {
		/*
		 * FIXME-VS: how do we handle this case? Abort
		 * transaction?
		 */
		warning ("vs-565", "tail2extent conversion has eaten data");
		return (result < 0) ? result : -ENOSPC;
	}
	return 0;
}


/* this does conversion page by page. It starts from very first item of file,
 * when page is filled with data from tail items those items are cut and
 * unallocated extent corresponding to that page gets created. This loops until
 * all the conversion is done */
static int tail2extent (struct inode * inode, tree_coord * coord, 
			lock_handle * lh)
{
	int result;
	file_plugin * fplug;
	reiser4_key key;
	struct page * page;
	char * p_data;
	int i;
	unsigned page_off, count, copied;
	char * item;

	/* collect statistics on the number of tail2extent conversions */
	reiser4_stat_file_add (tail2extent);

	fplug = inode_file_plugin (inode);
	if (!fplug || !fplug->key_by_inode) {
		return -EINVAL;
	}
	/* get key of first byte of a file */
	result = fplug->key_by_inode (inode, 0ull, &key);
	if (result) {
		return result;
	}

	done_lh (lh);
	done_coord (coord);

	page = 0;
	item = 0;
	while (1) {
		if (!item) {
			result = find_item (&key, coord, lh, ZNODE_READ_LOCK);
			if (result != CBK_COORD_FOUND) {
				return result;
			}
			assert ("vs-562", fplug->owns_item (inode, coord));
			item = item_body_by_coord (coord);
			copied = 0;
		}

		page_off = get_key_offset (&key) & PAGE_MASK;
		if (page_off == 0) {
			/* we start new page */
			assert ("vs-561", page == 0);
			page = grab_cache_page (inode->i_mapping,
						(unsigned long)(get_key_offset (&key) >> PAGE_SHIFT));
			if (!page)
				return -ENOMEM;
		}
		count = item_length_by_coord (coord) - copied;
		if (count > PAGE_SIZE - page_off) {
			/* page boundary is inside of tail item */
			count = PAGE_SIZE - page_off;
		}
		p_data = kmap (page);
		memcpy (p_data + page_off, item, count);
		kunmap (page);
		item += count;
		copied += count;
		page_off += count;
		set_key_offset (&key, get_key_offset (&key) + count);

		if (page_off == PAGE_SIZE ||
		    !must_continue (inode, &key, coord)) {
			/* page is filled completely or last item of item is
			 * read, cut corresponding tail items and call
			 * extent_write to write a page */
			done_lh (lh);
			done_coord (coord);

			count = page_off;
			if ((result = cut_tail_page (page)) ||
			    (result = write_page_by_extent (inode, page, count))) {
				/*
				 * FIXME-VS: how do we handle this case? Abort
				 * transaction?
				 */
				warning ("vs-565", "tail2extent conversion has eaten data");
				UnlockPage (page);
				page_cache_release (page);
				break;
			}
			/* extent corresponding to page is in the tree, tail
			 * items are not */
			mark_buffers_dirty (page, count);
			SetPageUptodate (page);
			UnlockPage (page);
			page_cache_release (page);

			if (!must_continue (inode, &key, coord))
				/* conversion is done */
				break;

			/* new page will be started */
			page = 0;
			/* we have to re-search item*/
			item = 0;
		}
		if (copied == item_length_by_coord (coord)) {
			/* all content of item is copied into page, therefore
			 * we have to find new one */
			item = 0;
		}
	}
	return result;
}

#endif /* TAIL2EXTENT_ITEM_BY_ITEM */


/* plugin->u.file.release
 * convert all extent items into tail items */
static int extent2tail (struct file *);
int ordinary_file_release (struct file * file)
{
	struct inode * inode;

	inode = file ->f_dentry->d_inode;

	if (should_have_notail (inode, inode->i_size))
		return 0;

	return extent2tail (file);
}


/* part of extent2tail. Page contains data which are to be put into tree by
 * tail items. Use tail_write for this. flow is composed like in
 * ordinary_file_write. The only difference is that data for writing are in
 * kernel space */
static int write_page_by_tail (struct inode * inode, struct page * page,
			       unsigned count)
{
	flow_t f;
	tree_coord coord;
	lock_handle lh;
	item_plugin * iplug;
	char * p_data;
	int result;


	p_data = kmap (page);
	inode_file_plugin (inode)->
		flow_by_inode (inode, p_data, 0/* kernel space */,
			       count, (loff_t)(page->index << PAGE_SHIFT),
			       WRITE_OP, &f);

	result = find_item (&f.key, &coord, &lh, ZNODE_WRITE_LOCK);
	if (result != CBK_COORD_NOTFOUND) {
		assert ("vs-559", result != CBK_COORD_FOUND);
		page_cache_release (page);
		return result;
	}

	iplug = item_plugin_by_id (TAIL_ID);
	assert ("vs-558", iplug && iplug->s.file.write);
	result = iplug->s.file.write (inode, &coord, &lh, &f);
	kunmap (page);
	done_lh (&lh);
	done_coord (&coord);
	if (result != (int)count) {
		/*
		 * FIXME-VS: how do we handle this case? Abort
		 * transaction?
		 */
		warning ("vs-560", "extent2tail conversion has eaten data");
		return (result < 0) ? result : -ENOSPC;
	}
	return 0;
}


/* for every page of file: read page, cut part of extent pointing to this page,
 * put data of page tree by tail item */
static int extent2tail (struct file * file)
{
	int result;
	struct inode * inode;
	file_plugin * fplug;
	struct page * page;
	int num_pages;
	reiser4_key from;
	reiser4_key to;
	int i;
	unsigned count;


	/* collect statistics on the number of extent2tail conversions */
	reiser4_stat_file_add (extent2tail);
	
	inode = file->f_dentry->d_inode;

	/* number of pages in the file */
	num_pages = (inode->i_size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (!num_pages)
		return 0;

	fplug = inode_file_plugin (inode);
	assert ("vs-551", fplug);
	assert ("vs-552", fplug->key_by_inode);

	fplug->key_by_inode (inode, 0ull, &from);
	to = from;

	for (i = 0; i < num_pages; i ++) {
		page = grab_cache_page (inode->i_mapping, (unsigned long)i);
		if (!page) {
			if (i)
				warning ("vs-550", "file left extent2tail-ed converted partially");
			return -ENOMEM;
		}
		result = ordinary_readpage (file, page);
		if (result) {
			UnlockPage (page);
			page_cache_release (page);
			return result;
		}
		wait_on_page (page);
		if (!Page_Uptodate (page)) {
			page_cache_release (page);
			return -EIO;
		}
		/* cut part of file we have read */
		set_key_offset (&from, (__u64)(i << PAGE_SHIFT));
		set_key_offset (&to, (__u64)((i << PAGE_SHIFT) + PAGE_SIZE - 1));
		result = cut_tree (tree_by_inode (inode), &from, &to);
		if (result) {
			page_cache_release (page);
			return -EIO;
		}
		/* put page data into tree via tail_write */
		count = PAGE_SIZE;
		if (i == num_pages - 1)
			count = (inode->i_size & ~PAGE_MASK) ? : PAGE_SIZE;
		result = write_page_by_tail (inode, page, count);
		page_cache_release (page);
		if (result)
			return result;
	}

	return 0;
}


/* plugin->u.file.flow_by_inode  = common_build_flow
 * plugin->u.file.flow_by_key    = NULL
 * plugin->u.file.key_by_inode   = ordinary_key_by_inode
 * plugin->u.file.set_plug_in_sd = NULL
 * plugin->u.file.set_plug_in_inode = NULL
 * plugin->u.file.create_blank_sd = NULL
 */


/* plugin->u.file.create
 * create sd for ordinary file. Just pass control to
 * fs/reiser4/plugin/object.c:common_file_save()
 */
int ordinary_file_create( struct inode *object, struct inode *parent UNUSED_ARG,
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
int ordinary_file_owns_item( const struct inode *inode /* object to check
							* against */, 
			     const tree_coord *coord /* coord to check */ )
{
	int result;

	result = common_file_owns_item (inode, coord);
	if (!result)
		return 0;
	assert ("vs-546",
		item_type_by_coord (coord) == ORDINARY_FILE_METADATA_TYPE);
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
