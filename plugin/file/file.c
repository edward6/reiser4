/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */


#include "../../reiser4.h"

/* this file contains:
 * - file_plugin methods of reiser4 unix file (REGULAR_FILE_PLUGIN_ID)
 */

/*
 * look for item of file @inode corresponding to @key
 */

#ifdef PSEUDO_CODE_CAN_COMPILE
find_item ()
{
	if (!coord && seal)
		set coord based on seal;
	if (coord) {
		if (key_in_coord(coord))
			return coord;
		coord = get_next_item(coord);
		if (key_in_coord(coord))
			return coord;
	}
	coord_by_key();
}
#endif

#define SEARCH_BIAS FIND_MAX_NOT_MORE_THAN
/*#define SEARCH_BIAS FIND_EXACT*/


/* get key of item next to one @coord is set to */
static reiser4_key * get_next_item_key (const coord_t * coord,
					reiser4_key * next_key)
{
	if (coord->item_pos == node_num_items (coord->node) - 1) {
		/* get key of next item if it is in right neighbor */
		UNDER_SPIN_VOID (dk, current_tree,
				 *next_key = *znode_get_rd_key (coord->node));
	} else {
		/* get key of next item if it is in the same node */
		coord_t next;
		
		coord_dup (&next, coord);
		check_me ("vs-730", coord_next_item (&next) == 0);
		item_key_by_coord (&next, next_key);
	}
	return next_key;
}


/* check whether coord is set as if it was just set by node's lookup with
 * @key. If yes - 1 is returned. If it is not - check whether @key is inside of
 * this node, if yes - call node lookup. Otherwise - return 0 */
int coord_set_properly (const reiser4_key * key, coord_t * coord)
{
	int result;
	coord_t item;         /* item @coord is set to or after */
	reiser4_key item_key; /* key of that item */
	item_plugin * iplug;  /* plugin of item coord is set to or after */
	reiser4_key max_key,  /* max key currently contained in that item */
		max_possible_key, /* max possible key which can be stored in
				   * that item */
		next_item_key; /* key of item next to that item or right
				* delimiting key */


	/*
	 * FIXME-VS: znode_contains_key is not appropriate here because it
	 * does: left delimiting key <= key <= right delimiting key. We need
	 * here: left_delimiting key <= key < right delimiting key
	 */
	result = UNDER_SPIN (dk, current_tree,
			     keyle (znode_get_ld_key (coord->node), key) &&
			     keylt (key, znode_get_rd_key (coord->node)));
	
	if (!result) {
		/* node does not contain @key */
		if (coord_is_before_leftmost (coord)) {
			/* this is only possible when left neighbor is
			 * unformatted node */
			assert ("vs-910", znode_get_level (coord->node) == LEAF_LEVEL);
			assert ("vs-684", UNDER_SPIN 
				(tree, current_tree,
				 znode_is_left_connected (coord->node) && coord->node->left == 0));
			if (UNDER_SPIN (dk, current_tree, keylt (key, znode_get_ld_key (coord->node))))
				return 1;
		}
		return 0;
	}

	result = zload (coord->node);
	if (result)
		return 0;

	if (node_is_empty (coord->node)) {
		/*
		 * FIXME:NIKITA->VS can reproducible fail this
		 *
		 * Probably coord->node was created by coord_by_key drill? It
		 * is empty, but HEARD_BANSHEE is not set (CREATED is).
		 */
		//assert ("vs-751", keyeq (key, znode_get_ld_key (coord->node)));

		zrelse (coord->node);
		return 1;
	}

	/* FIXME-VS: fow now */
	assert ("vs-736", coord->between != BEFORE_ITEM);
	assert ("vs-737", coord->between != BEFORE_UNIT);


	if (coord_is_existing_item (coord)) {
		/* check whether @key is inside of this unit */
		item_plugin * iplug;

		iplug = item_plugin_by_coord (coord);
		assert ("vs-716", iplug && iplug->common.key_in_item);

		if (iplug->common.key_in_item (coord, key)) {
			/*
			 * FIXME-VS: should coord be updated?
			 */
			zrelse (coord->node);
			return 1;
		}
	}
	assert ("vs-769", ergo (coord_is_existing_item (coord), 
				keylt (item_plugin_by_coord (coord)->common.real_max_key_inside (coord, &max_key), key)));

	/* get key of item after which coord is set */
	coord_dup (&item, coord);
	item.unit_pos = 0;
	item.between = AT_UNIT;
	item_key_by_coord (&item, &item_key);
	iplug = item_plugin_by_coord (&item);


	/* max key stored in item */
	if (iplug->common.real_max_key_inside)
		iplug->common.real_max_key_inside (&item, &max_key);
	else
		max_key = item_key;

	/* max possible key which can be in item */
	if (iplug->common.max_key_inside)
		iplug->common.max_key_inside (&item, &max_possible_key);
	else
		max_possible_key = item_key;

	/* key of item next to coord or right delimiting key */
	get_next_item_key (coord, &next_item_key);

	if (keyge (key, &item_key)) {
		/* item_key <= key */
		if (keygt (key, &max_possible_key)) {
			/* key > max_possible_key */
			assert ("vs-739", keylt (key, &next_item_key));
			coord->unit_pos = 0;
			coord->between = AFTER_ITEM;
			zrelse (coord->node);
			return 1;		
		}
		if (keylt (key, &max_possible_key)) {
			/* key < max_possible_key */
			coord->unit_pos = coord_last_unit_pos (coord);
			coord->between = AFTER_UNIT;
			zrelse (coord->node);
			return 1;		
		}
	}

	/*
	 * We assume that this function is being used for the unix file plugin,
	 * that no two items of this file exist in the same node, and that we
	 * always go to the next node to find the item.  If this is not true,
	 * then the optimization needs changing here, and we should try
	 * incrementing the item_pos and seeing if it contains what we are
	 * looking for instead of searching within the node.
	 */
	impossible ("vs-725", "do we ever get here?");
	node_plugin_by_node (coord->node)->lookup (coord->node, key,
						   SEARCH_BIAS,
						   coord);
	zrelse (coord->node);
	return 1;
}


/* get right neighbor and set coord to first unit in it */
static int get_next_item (coord_t * coord, lock_handle * lh,
			  znode_lock_mode lock_mode)
{
	int result;
	lock_handle lh_right_neighbor;

	init_lh (&lh_right_neighbor);
	result = reiser4_get_right_neighbor (&lh_right_neighbor,
					     coord->node, (int)lock_mode,
					     GN_DO_READ );
	if (result) {
		done_lh (&lh_right_neighbor);
		return result;
	}

	/*
	 * FIXME-VS: zload only to use coord_init_first_unit
	 */
	result = zload (lh_right_neighbor.node);
	if (result != 0) {
		done_lh (&lh_right_neighbor);
		return result;
	}
	coord_init_first_unit (coord, lh_right_neighbor.node);
	zrelse (lh_right_neighbor.node);

	done_lh (lh);
	move_lh (lh, &lh_right_neighbor);

	return result;	
}


int find_next_item (struct file * file,
		    const reiser4_key * key, /* key of position in a file of
					      * next read/write */
		    coord_t * coord, /* on entry - initilized by 0s or
					coordinate (locked node and position in
					it) on which previous read/write
					operated, on exit - coordinate of
					position specified by @key */
		    lock_handle * lh, /* initialized by 0s if @coord is zeroed
				       * or othewise lock handle of locked node
				       * in @coord, on exit - lock handle of
				       * locked node in @coord */
		    znode_lock_mode lock_mode /* which lock (read/write) to put
					       * on returned node */)
{
	int result;
	__u32 flags;

	/* collect statistics on the number of calls to this function */
	reiser4_stat_file_add (find_items);

#if 0
/* VS said so */
	if (!coord->node && file) {
		reiser4_file_fsdata * fdata;
		seal_t seal;
		coord_t sealed_coord;

		/* try to use seal which might be set in previous access to the
		 * file */
		fdata = reiser4_get_file_fsdata (file);
		if (!IS_ERR (fdata)) {
			seal = fdata->reg.last_access;
			if (seal_is_set (&seal)) {
				sealed_coord = fdata->reg.coord;
				result = seal_validate (&seal, &sealed_coord, key,
							znode_get_level (sealed_coord.node),
							lh, SEARCH_BIAS, lock_mode,
							ZNODE_LOCK_LOPRI);
				if (result == 0) {
					/* set coord based on seal */
					coord_dup (coord, &sealed_coord);
				}
			}
		}
	}
#endif
	if (coord->node) {
		if (coord_set_properly (key, coord))
			return 0;
		result = get_next_item (coord, lh, lock_mode);
		if (!result)
			if (coord_set_properly (key, coord))
				return 0;
		/**/
		done_lh (lh);

		coord_init_zero (coord);
		init_lh (lh);
	}

	/* collect statistics on the number of calls to this function which did
	 * not get optimized */
	reiser4_stat_file_add (full_find_items);
	flags = CBK_UNIQUE;
	if (lock_mode == ZNODE_WRITE_LOCK)
		flags |= CBK_FOR_INSERT;
	return coord_by_key (current_tree, key, coord, lh,
			     lock_mode, SEARCH_BIAS,
			     TWIG_LEVEL, LEAF_LEVEL, flags);
}


/* plugin->u.file.write_flow = NULL
 * plugin->u.file.read_flow = NULL
 */

/* find position of last byte of last item of the file plus 1 */
static loff_t find_file_size (struct inode * inode)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	loff_t file_size;
	item_plugin * iplug;


	inode_file_plugin (inode)->key_by_inode (inode, get_key_offset (max_key ()), &key);

	coord_init_zero (&coord);
	init_lh (&lh);
	result = find_next_item (0, &key, &coord, &lh, ZNODE_READ_LOCK);
	if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
		/* error occured */
		done_lh (&lh);
		return (loff_t)result;
	}

	if (result == CBK_COORD_NOTFOUND) {
		/* there are no items of this file */
		done_lh (&lh);
		return 0;
	}

	/* there are items of this file (at least one) */
	result = zload (coord.node);
	if (result) {
		done_lh (&lh);
		return (loff_t)result;
	}
	iplug = item_plugin_by_coord (&coord);

	assert ("vs-853", iplug->common.real_max_key_inside);
	iplug->common.real_max_key_inside (&coord, &key);

	file_size = get_key_offset (&key) + 1;

	zrelse (coord.node);
	done_lh (&lh);
	return file_size;
}

/*
 * FIXME-VS: remove after debugging
 */
void set_fu_page (unsigned long index, struct page * page);
char * get_fu_page_data (int index);


/* part of unix_file_truncate: it is called when truncate is used to make
 * file shorter */
static int shorten (struct inode * inode)
{
	int result;
	reiser4_key from, to;
	struct page * page;
	int padd_from;
	jnode * j;
	unsigned long index;
#ifdef DEBUGGING_FSX
	int i;
#endif /* DEBUGGING_FSX */

	inode_file_plugin (inode)->key_by_inode (inode, inode->i_size, &from);
	to = from;
	set_key_offset (&to, get_key_offset (max_key ()));

	/* all items of ordinary reiser4 file are grouped together. That is why
	   we can use cut_tree. Plan B files (for instance) can not be
	   truncated that simply */
	result = cut_tree (tree_by_inode (inode), &from, &to);
	if (result)
		return result;

	index = (inode->i_size >> PAGE_CACHE_SHIFT);

#ifdef DEBUGGING_FSX
	/*
	 * FIXME-VS: remove after debugging
	 */
	/* completely truncated pages */
	assert ("vs-930", (inode->i_size <= 32768));
	for (i = index + 1; i < 8; i ++) {
		memset (get_fu_page_data (i), 0, PAGE_CACHE_SIZE);
		set_fu_page (i, 0);
	}
#endif /* DEBUGGING_FSX */

	if (inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN) &&
	    inode_get_flag (inode, REISER4_HAS_TAIL))
		/* file is built of tail items. No need to worry about zeroing
		 * last page after new file end */
		return 0;


	padd_from = inode->i_size & (PAGE_CACHE_SIZE - 1);
	if (!padd_from) {
#ifdef DEBUGGING_FSX
		/*
		 * FIXME-VS: remove after debugging
		 */
		if (index < 8) {
			memset (get_fu_page_data (index), 0, PAGE_CACHE_SIZE);
			set_fu_page (index, 0);
		}
#endif
		return 0;
	}

	/* last page is partially truncated - zero its content */
	page = read_cache_page (inode->i_mapping, index,
				unix_file_readpage_nolock,
				0);
	if (IS_ERR (page)) {
		if (likely (PTR_ERR (page) == -EINVAL)) {
			/* looks like file is built of tail items */
			return 0;
		}
		return PTR_ERR (page);
	}
	wait_on_page_locked (page);
	if (!PageUptodate (page)) {
		page_cache_release (page);
		return -EIO;
	}
	lock_page (page);

	j = jnode_of_page (page);
	if (IS_ERR (j)) {
		unlock_page (page);
		page_cache_release (page);
		return PTR_ERR (j);
	}
	if (*jnode_get_block (j) == 0 && !jnode_created (j) && !PageDirty (page)) {
		/* hole page. It is not dirty so it was not modified via
		 * mmaping */
		unlock_page (page);
		jput (j);
		page_cache_release (page);
		return 0;
	}
	unlock_page (page);
	jput (j);
	lock_page (page);

	result = txn_try_capture_page (page, ZNODE_WRITE_LOCK, 0);
	if (result) {
		unlock_page (page);
		page_cache_release (page);
		return -EIO;
	}
	
	memset (kmap (page) + padd_from, 0, PAGE_CACHE_SIZE - padd_from);
	flush_dcache_page (page);

#ifdef DEBUGGING_FSX
	/*
	 * FIXME-VS: remove after debugging
	 */
	memset (get_fu_page_data (page->index) + padd_from, 0, PAGE_CACHE_SIZE - padd_from);
#endif

	kunmap (page);
	

	jnode_set_dirty (jnode_by_page (page));
	
	unlock_page (page);
	page_cache_release (page);

	return 0;
}


/* part of unix_file_truncate: it is called when truncate is used to make file
 * longer */
static loff_t write_flow (struct file * file, struct inode * inode, flow_t * f);


/* Add hole to end of file. @file_size is current file size. @inode->i_size is
 * size file is to be expanded to */
static int expand_file (struct inode * inode, loff_t file_size)
{
	int result;
	file_plugin * fplug;
	flow_t f;
	loff_t written;


	assert ("vs-909", inode->i_size > file_size);

	fplug = inode_file_plugin (inode);
	result = fplug->flow_by_inode (inode, 0/* buf */, 1/* user space */,
				       inode->i_size - file_size, file_size,
				       WRITE_OP, &f);
	if (result)
		return result;

	written = write_flow (0, inode, &f);	
	if (written != inode->i_size - file_size) {
		/* we were not able to write expand file to desired size */
		if (written < 0)
			return (int)written;
		return -ENOSPC;
	}

	return 0;
}


/* plugin->u.file.truncate */
/* Audited by: green(2002.06.15) */
int unix_file_truncate (struct inode * inode, loff_t size)
{
	int result;
	loff_t file_size;


	inode->i_size = size;

	get_nonexclusive_access (inode);


	file_size = find_file_size (inode);
	if (file_size < 0)
		return (int)file_size;
	if (file_size < inode->i_size)
		result = expand_file (inode, file_size);
	else {		
		result = shorten (inode);
	}
	if (!result) {
		result = reiser4_write_sd (inode);
		if (result)
			warning ("vs-638", "updating stat data failed: %i", result);
	}
	drop_nonexclusive_access (inode);
	return result;
}	


/* plugin->u.write_sd_by_inode = common_file_save */


/*
 * this finds item of file corresponding to page being read/written in and calls its
 * readpage/writepage method. It is used when exclusive or sharing access to inode is
 * grabbed
 */
/* Audited by: green(2002.06.15) */
static int page_op (struct file * file, struct page * page, rw_op op)
{
	int result;
	coord_t coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin * iplug;
	znode * loaded;


	assert ("vs-860", op == WRITE_OP || op == READ_OP);
	assert ("vs-937", PageLocked (page));

	/* get key of first byte of the page */
	unix_file_key_by_inode (page->mapping->host,
				(loff_t)page->index << PAGE_CACHE_SHIFT, &key);
	
	coord_init_zero (&coord);
	init_lh (&lh);

	/* look for file metadata corresponding to first byte of page */
	result = find_next_item (file, &key, &coord, &lh,
				 op == READ_OP ? ZNODE_READ_LOCK : ZNODE_WRITE_LOCK);
	if (result != CBK_COORD_FOUND) {
		warning ("vs-280", "No file items found\n");
		goto out2;
	}

	loaded = coord.node;
	result = zload (loaded);
	if (result) {
		goto out2;
	}

	result = -EINVAL;

	/* get plugin of found item */
	iplug = item_plugin_by_coord (&coord);
	if (op == READ_OP) {
		if (!iplug->s.file.readpage) {
			goto out;
		}
		result = iplug->s.file.readpage (&coord, page);
	} else {
		if (!iplug->s.file.writepage) {
			goto out;
		}
		result = iplug->s.file.writepage (&coord, &lh, page);
	}

 out:
	zrelse (loaded);
 out2:
	done_lh (&lh);

	if (result) {
		SetPageError (page);
		unlock_page (page);
	}

	return result;
}


/* this is used a filler for read_cache_page in extent2tail where access
 * (exclusive) to file is acquired already */
int unix_file_readpage_nolock (void * file, struct page * page)
{
	return page_op (file, page, READ_OP);
}


/* plugin->u.file.readpage */
/* Audited by: green(2002.06.15) */
int unix_file_readpage (struct file * file, struct page * page)
{
	int result;

	get_nonexclusive_access (file->f_dentry->d_inode);
	result = page_op (file, page, READ_OP);
	drop_nonexclusive_access (file->f_dentry->d_inode);
	return result;
}


/* plugin->u.file.writepage */
int unix_file_writepage (struct page * page)
{
	int result;

	get_nonexclusive_access (page->mapping->host);
	result = page_op (0, page, WRITE_OP);
	drop_nonexclusive_access (page->mapping->host);
	return result;
}


/* plugin->u.file.read */
ssize_t unix_file_read (struct file * file, char * buf, size_t read_amount,
			loff_t * off)
{
	int result;
	struct inode * inode;
	coord_t coord;
	lock_handle lh;
	size_t to_read;		/* do we really need both this and read_amount? */
	item_plugin * iplug;
	reiser4_plugin_id id;

#ifdef NEW_READ_IS_READY
	sink_t userspace_sink;
#else
	flow_t f;
#endif /* NEW_READ_IS_READY */

	/* collect statistics on the number of reads */
	reiser4_stat_file_add (reads);

	inode = file->f_dentry->d_inode;
	result = 0;

	/* this should now be called userspace_sink_build, now that we have
	 * both sinks and flows.  See discussion of sinks and flows in
	 * www.namesys.com/v4/v4.html */
#ifdef NEW_READ_IS_READY
	result = userspace_sink_build (inode, buf, 1/* user space */, read_amount,
				    *off, READ_OP, &f);
#else
	/* build flow */
	result = inode_file_plugin (inode)->flow_by_inode (inode, buf, 1/* user space */,
							   read_amount,
							   *off, READ_OP, &f);
#endif
	if (result)
		return result;

	get_nonexclusive_access (inode);
	
#ifdef NEW_READ_IS_READY
	/* have generic_readahead to return number of pages to
	 * readahead. generic_readahead must not do readahead, but return
	 * number of pages to readahead */
	intrafile_readahead_amount = wrapper_for_generic_readahead(struct file * file, off, read_amount);

	while (intrafile_readahead_amount) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/* do not read out of file */
			break;		/* coord will point to current item on entry and next item on exit */
		readahead_result = find_next_item (file, &f.key, &coord, &lh,
						   ZNODE_READ_LOCK);
		if (readahead_result != CBK_COORD_FOUND)
			/* item had to be found, as it was not - we have
			 * -EIO */
			break;
		
		/* call readahead method of found item */
		iplug = item_plugin_by_coord (&coord);
		if (!iplug->s.file.readahead) {
			readahead_result = -EINVAL;
			break;
		}
		
		readahead_result = iplug->s.file.readahead (inode, &coord, &lh, &intrafile_readahead_amount);
		if (readahead_result)
			break;
	}

	unix_file_interfile_readahead(struct file * file, off, read_amount, coord);

#endif /* NEW_READ_IS_READY */


	to_read = f.length;
	while (f.length) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/* do not read out of file */
			break;
		
                page_cache_readahead (file, (unsigned long)(get_key_offset (&f.key) >> PAGE_CACHE_SHIFT));

		/* coord will point to current item on entry and next item on exit */
		coord_init_zero (&coord);
		init_lh (&lh);
		result = find_next_item (file, &f.key, &coord, &lh,
					 ZNODE_READ_LOCK);
		if (result != CBK_COORD_FOUND) {
			/* item had to be found, as it was not - we have
			 * -EIO */
			done_lh (&lh);
			break;
		}

		result = zload (coord.node);
		if (result) {
			done_lh (&lh);
			break;
		}

		iplug = item_plugin_by_coord (&coord);
		id = item_plugin_id (iplug);
		if (id != EXTENT_POINTER_ID && id != TAIL_ID) {
			result = -EIO;
			zrelse (coord.node);
			done_lh (&lh);
			break;
		}

		/* for debugging sake make sure that tail status is set
		 * correctly if it claimes to be known */
		if (REISER4_DEBUG &&
		    inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN)) {
			assert ("vs-829",
				(id == TAIL_ID && inode_get_flag (inode, REISER4_HAS_TAIL)) ||
				(id == EXTENT_POINTER_ID && !inode_get_flag (inode, REISER4_HAS_TAIL)));
		}

		/* get tail status if it is not known yet */
		if (!inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN)) {
			if (id == TAIL_ID)
				inode_set_flag (inode, REISER4_HAS_TAIL);
			else
				inode_clr_flag (inode, REISER4_HAS_TAIL);
		}

		/* call read method of found item */
		result = iplug->s.file.read (inode, &coord, &lh, &f);
		zrelse (coord.node);
		done_lh (&lh);
		if (result) {
			break;
		}
	}

	if( to_read - f.length ) {
		/* something was read. Update stat data */
		UPDATE_ATIME (inode);
		result = reiser4_write_sd (inode);
		if (result)
			warning ("vs-676", "updating stat data failed: %i",
				 result);
	}

	drop_nonexclusive_access (inode);

	/* update position in a file */
	*off += (to_read - f.length);
	/* return number of read bytes or error code if nothing is read */

/* VS-FIXME-HANS: readahead_result needs to be handled here */
	return (to_read - f.length) ? (to_read - f.length) : result;
}

#ifdef NEW_READ_IS_READY
unix_file_interfile_readahead(struct file * file, off, read_amount, coord)
{

	interfile_readahead_amount = unix_file_interfile_readahead_amount(struct file * file, off, read_amount);

	while (interfile_readahead_amount--)
	{
		right = get_right_neightbor(current);
		if (right is just after current)
		{
			coord_dup(right, current);
			zload(current);
			
		}
		else {
			break;
/* VS-FIXME-HANS: insert some coord releasing code here */
		}

	}

			
}

unix_file_interfile_readahead_amount(struct file * file, off, read_amount)
{
				/* current generic guess.  More sophisticated code can come later in v4.1+. */
	return 8;
}

#endif /* NEW_READ_IS_READY */


/* these are write modes. Certain mode is chosen depending on resulting file
 * size and current metadata of file */
typedef enum {
	WRITE_EXTENT,
	WRITE_TAIL,
	CONVERT
} write_todo;

static write_todo unix_file_how_to_write (struct inode *, flow_t *, coord_t *);


/*
 * This searches for write position in the tree and calls write method of
 * appropriate item to actually copy user data into filesystem. This loops
 * until all the data from flow @f are written to a file.
 */
static loff_t write_flow (struct file * file, struct inode * inode, flow_t * f)
{
	int result;
	coord_t coord;
	lock_handle lh;	
	size_t to_write;
	item_plugin * iplug;


	init_lh (&lh);
	coord_init_zero (&coord);

	to_write = f->length;
	while (1) {
		znode * loaded;

		/* look for file's metadata (extent or tail item) corresponding to position we write to */
		result = find_next_item (file, &f->key, &coord, &lh, ZNODE_WRITE_LOCK);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/* error occurred */
			done_lh (&lh);
			return result;
		}

		/* store what we zload so that we were able to zrelse */
		loaded = coord.node;
		result = zload (loaded);
		if (result) {
			break;
		}

		switch (unix_file_how_to_write (inode, f, &coord)) {
		case WRITE_EXTENT:
			iplug = item_plugin_by_id (EXTENT_POINTER_ID);
			/* resolves to extent_write function */

			result = iplug->s.file.write (inode, &coord, &lh, f, 0);
			if (!result) {
				inode_set_flag (inode, REISER4_TAIL_STATE_KNOWN);
				inode_clr_flag (inode, REISER4_HAS_TAIL);
			}
			break;

		case WRITE_TAIL:
			iplug = item_plugin_by_id (TAIL_ID);
			/* resolves to tail_write function */

			result = iplug->s.file.write (inode, &coord, &lh, f, 0);
			if (!result) {
				inode_set_flag (inode, REISER4_TAIL_STATE_KNOWN);
				inode_set_flag (inode, REISER4_HAS_TAIL);
			}
			break;

		case CONVERT:
			zrelse (loaded);
			done_lh (&lh);
			result = tail2extent (inode);
			if (result) {
				return result;
			}
			init_lh (&lh);
			coord_init_zero (&coord);
			continue;

		default:
			impossible ("vs-293", "unknown write mode");
		}
		zrelse (loaded);

		if (result && result != -EAGAIN)
			/* error */
			break;
		if ((loff_t)get_key_offset (&f->key) > inode->i_size)
			/* file got longer */
			inode->i_size = get_key_offset (&f->key);
		if (!to_write) {
			/* expanding truncate */
			if ((loff_t)get_key_offset (&f->key) < inode->i_size)
				continue;
		}
		if (f->length == 0)
			/* write is done */
			break;
	}
	if (coord.node && coord_set_properly (&f->key, &coord) && file) {
		reiser4_file_fsdata * fdata;
		seal_t seal;

		fdata = reiser4_get_file_fsdata (file);
		if (!IS_ERR (fdata)) {
			/* re-set seal of last access to file */
			seal_init (&seal, &coord, &f->key);
			fdata->reg.last_access = seal;
			fdata->reg.coord = coord;
			fdata->reg.level = znode_get_level (coord.node);
		}
	}
	done_lh (&lh);

	return to_write - f->length;
}


/* plugin->u.file.write */
ssize_t unix_file_write (struct file * file, /* file to write to */
			 const char * buf, /* comments are needed */
			 size_t count, /* number of bytes ot write */
			 loff_t * off /* position to write which */)
{
	int result;
	struct inode * inode;
	flow_t f;
	ssize_t written;


	assert ("vs-855", count > 0);

	/* collect statistics on the number of writes */
	reiser4_stat_file_add (writes);

	inode = file->f_dentry->d_inode;

	get_nonexclusive_access (inode);

	if (inode->i_size < *off) {
		loff_t old_size;

		/* append file with a hole. This allows extent_write and
		 * tail_write to not decide when hole appending is
		 * necessary. When it is required f->length == 0 */
		old_size = inode->i_size;
		inode->i_size = *off;
		result = expand_file (inode, old_size);
		if (result) {
			/*
			 * FIXME-VS: i_size may now be set incorrectly
			 */
			drop_nonexclusive_access (inode);
			inode->i_size = old_size;
			return result;
		}
	}

	/* build flow */
	result = inode_file_plugin (inode)->flow_by_inode (inode, (char *)buf,
							   1/* user space */, count, *off,
							   WRITE_OP, &f);
	if (result)
		return result;

	written = write_flow (file, inode, &f);
	if (written < 0) {
		drop_nonexclusive_access (inode);
		return written;
	}
	
	if (written) {
		/* something was written. Update stat data */
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		result = reiser4_write_sd (inode);
		if (result)
			warning ("vs-636", "updating stat data failed: %i",
				 result);
	}

	drop_nonexclusive_access (inode);

	/* update position in a file */
	*off += written;
	/* return number of written bytes */
	return written;
}

#if 0
/*
 * unix files of reiser4 are built either of extents only or of tail items
 * only. If looking for file item coord_by_key stopped at twig level - file
 * consists of extents only, if at leaf level - file is built of extents only
 * FIXME-VS: it is possible to imagine different ways for finding that
 */
/* AUDIT: Comment above is incorrect, both cases it looks at is dealing with extents, how about file tails? */
/* Audited by: green(2002.06.15) */
static int built_of_extents (struct inode * inode UNUSED_ARG,
			     coord_t * coord)
{
	return znode_get_level (coord->node) == TWIG_LEVEL;
}
#endif


/* returns 1 if file of that size (@new_size) has to be stored in unformatted
 * nodes */
/* Audited by: green(2002.06.15) */
static int should_have_notail (struct inode * inode, loff_t new_size)
{
	if (!inode_tail_plugin (inode))
		return 1;
	return !inode_tail_plugin (inode)->have_tail (inode, new_size);

}


static write_todo unix_file_how_to_write (struct inode * inode, flow_t * f,
					  coord_t * coord)
{
	reiser4_item_data data;
	loff_t new_size;


	/* size file will have after write */
	new_size = get_key_offset (&f->key) + f->length;

	if (znode_get_level (coord->node) == TWIG_LEVEL) {
		/* extent item of this file found */
		data.iplug = item_plugin_by_id (EXTENT_POINTER_ID);
		assert ("vs-919", item_can_contain_key (coord, &f->key, &data));
		return WRITE_EXTENT;
	}

	assert ("vs-920", znode_get_level (coord->node) == LEAF_LEVEL);

	data.iplug = item_plugin_by_id (TAIL_ID);
	if (!node_is_empty (coord->node) &&
	    coord_is_existing_item (coord) &&
	    item_can_contain_key (coord, &f->key, &data)) {
		/* tail item of this file found */
		if (should_have_notail (inode, new_size))
			return CONVERT;
		return WRITE_TAIL;
	}

	/* there are no any items of this file yet */
	if (should_have_notail (inode, new_size))
		return WRITE_EXTENT;
	return WRITE_TAIL;
}

#if 0
/* decide how to write flow @f into file @inode */
/* Audited by: green(2002.06.15) */
static write_todo unix_file_how_to_write2 (struct inode * inode, flow_t * f,
					   coord_t * coord)
{
	loff_t new_size;


	/* size file will have after write */
	new_size = get_key_offset (&f->key) + f->length;

	if (new_size <= inode->i_size && f->length) {
		/* file does not get longer - no conversion will be
		 * performed */
		/* AUDIT: Will this also work correctly if we start overwritting
		   extend but then contine to overwrite over the tail? */
		if (built_of_extents (inode, coord))
			return WRITE_EXTENT;
		else
			return WRITE_TAIL;
	}

	assert ("vs-377", inode_tail_plugin (inode)->have_tail);

	if (coord->between == AT_UNIT || coord->between == AFTER_UNIT) {
		/* file is not empty (there is at least one its item) and will
		 * get longer or is being expanded on truncate */
		if (should_have_notail (inode, new_size)) {
			/* that long file (@new_size bytes) is supposed to be
			 * built of extents */
			if (built_of_extents (inode, coord)) {
				/* it is built that way already */
				return WRITE_EXTENT;
			} else {
				/* file is built of tail items, conversion is
				 * required */
				return CONVERT;
			}
		} else {
			/* "notail" is not required, so keep file in its
			 * current form */
			if (built_of_extents (inode, coord))
				return WRITE_EXTENT;
			else
				return WRITE_TAIL;
		}
	}

	/* there are no any items of this file. FIXME-VS: should we write by
	 * extents? */
	if (should_have_notail (inode, new_size))
		return WRITE_EXTENT;
	else
		return WRITE_TAIL;
}
#endif



/* plugin->u.file.release
 * convert all extent items into tail items if necessary */
/* Audited by: green(2002.06.15) */
int unix_file_release (struct file * file)
{
	struct inode * inode;

	inode = file ->f_dentry->d_inode;

	if (inode->i_size == 0)
		return 0;

	/*
	 * FIXME-VS: it is not clear where to do extent2tail conversion yet
	 */
	if (!inode_get_flag (inode, REISER4_TAIL_STATE_KNOWN))
		/* there were no accesses to file body. Leave it as it is */
		return 0;

	if (should_have_notail (inode, inode->i_size)) {
		if (inode_get_flag (inode, REISER4_HAS_TAIL))
			info ("file_release: "
			      "file is built of tails instead of extents\n");
		return 0;
	}
	if (inode_get_flag (inode, REISER4_HAS_TAIL))
		/* file is already built of tails */
		return 0;
	return extent2tail (file);
}

/* plugin->u.file.mmap
 * make sure that file is built of extent blocks
 */
/* Audited by: green(2002.06.15) */
int unix_file_mmap (struct file * file, struct vm_area_struct * vma)
{
	struct inode * inode;
	int result;

	inode = file->f_dentry->d_inode;

	/* tail2extent expects file to be nonexclusively locked */
	get_nonexclusive_access (inode);
	result = tail2extent (inode);
	drop_nonexclusive_access (inode);
	if (result)
		return result;
	return generic_file_mmap (file, vma);
}

/* plugin->u.file.get_block */
int unix_file_get_block(struct inode *inode UNUSED_ARG,
			sector_t block UNUSED_ARG,
			struct buffer_head *bh_result UNUSED_ARG,
			int create UNUSED_ARG)
{
	/* FIXME-VS: not ready */
	return -EINVAL;
}



/* plugin->u.file.flow_by_inode  = common_build_flow */


/* plugin->u.file.key_by_inode */
/* Audited by: green(2002.06.15) */
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
/* Audited by: green(2002.06.15) */
int unix_file_create( struct inode *object, struct inode *parent UNUSED_ARG,
		      reiser4_object_create_data *data UNUSED_ARG )
{
	assert( "nikita-744", object != NULL );
	assert( "nikita-745", parent != NULL );
	assert( "nikita-747", data != NULL );
	assert( "nikita-748", inode_get_flag( object, REISER4_NO_STAT_DATA ) );
	assert( "nikita-749", 
		( data -> id == REGULAR_FILE_PLUGIN_ID ) ||
		( data -> id == SPECIAL_FILE_PLUGIN_ID ) );
	
	return reiser4_write_sd( object );
}


/* plugin->u.file.delete = NULL
 * plugin->u.file.add_link = NULL
 * plugin->u.file.rem_link = NULL
 */


/* plugin->u.file.owns_item 
 * this is common_file_owns_item with assertion */
/* Audited by: green(2002.06.15) */
int unix_file_owns_item( const struct inode *inode /* object to check
						    * against */, 
			 const coord_t *coord /* coord to check */ )
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
