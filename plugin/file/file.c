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
		spin_lock_dk (current_tree);
		*next_key = *znode_get_rd_key (coord->node);
		spin_unlock_dk (current_tree);
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


	result = zload (coord->node);
	if (result)
		return 0;
	/* FIXME-VS: fow now */
	assert ("vs-735", !node_is_empty (coord->node));
	assert ("vs-736", coord->between != BEFORE_ITEM);
	assert ("vs-737", coord->between != BEFORE_UNIT);


	if (coord_is_existing_unit (coord)) {
		/* check whether @key is inside of this unit */
		item_plugin * iplug;

		iplug = item_plugin_by_coord (coord);
		assert ("vs-716", iplug && iplug->common.key_in_coord);

		if (iplug->common.key_in_coord (coord, key)) {
			/*
			 * FIXME-VS: should coord be updated?
			 */
			zrelse (coord->node);
			return 1;
		}
	}

	/* get key of item after which coord is set */
	coord_dup (&item, coord);
	item.unit_pos = 0;
	item.between = AT_UNIT;
	item_key_by_coord (&item, &item_key);
	iplug = item_plugin_by_coord (&item);


	/* @coord requires re-setting, check whether @key is in this node
	 * before calling node's lookup */
	/*
	 * FIXME-VS: znode_contains_key is not appropriate here because it
	 * does: left delimiting key <= key <= right delimiting key. We need
	 * here: left_delimiting key <= key < right delimiting key
	 */
	spin_lock_dk (current_tree);
	result = (keyle (znode_get_ld_key (coord->node), key) &&
		  keylt (key, znode_get_rd_key (coord->node)));
	spin_unlock_dk (current_tree);
	
	if (!result) {
		/* node does not contain @key */
		zrelse (coord->node);
		return 0;
	}

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
			assert ("vs-740", keygt (key, &max_key));
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
	if (result == 0) {
		done_lh (lh);
		
		result = zload (lh_right_neighbor.node);
		if (result != 0)
			return result;
		coord_init_first_unit (coord, lh_right_neighbor.node);
		move_lh (lh, &lh_right_neighbor);
		zrelse (lh_right_neighbor.node);
	}
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
	return coord_by_key (current_tree, key, coord, lh,
			     lock_mode, SEARCH_BIAS,
			     TWIG_LEVEL, LEAF_LEVEL, CBK_UNIQUE | CBK_FOR_INSERT);
}


/* plugin->u.file.write_flow = NULL
 * plugin->u.file.read_flow = NULL
 */


/* plugin->u.file.truncate */
/* Audited by: green(2002.06.15) */
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

	get_nonexclusive_access (inode);

	/* all items of ordinary reiser4 file are grouped together. That is why
	   we can use cut_tree. Plan B files (for instance) can not be
	   truncated that simply */
	result = cut_tree (tree_by_inode (inode), &from, &to);
	if (result) {
		drop_nonexclusive_access (inode);
		return result;
	}

	assert ("vs-637",
		inode_file_plugin( inode ) -> write_sd_by_inode ==
		common_file_save);
	if ((result = common_file_save (inode)))
		warning ("vs-638", "updating stat data failed\n");

	drop_nonexclusive_access (inode);
	return result;
}


/* plugin->u.write_sd_by_inode = common_file_save */


/*
 * this finds item of file corresponding to page being read in and calls its
 * readpage method. It is used when exclusive or sharing access to inode is
 * grabbed
 */
/* Audited by: green(2002.06.15) */
int unix_file_readpage_nolock (struct file * file, struct page * page)
{
	int result;
	coord_t coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin * iplug;
	struct readpage_arg arg;


	/* get key of first byte of the page */
	unix_file_key_by_inode (page->mapping->host,
				(loff_t)page->index << PAGE_CACHE_SHIFT, &key);
	
	coord_init_zero (&coord);
	init_lh (&lh);

	/* look for file metadata corresponding to first byte of page */
	result = find_next_item (file, &key, &coord, &lh, ZNODE_READ_LOCK);
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
/* Audited by: green(2002.06.15) */
int unix_file_readpage (struct file * file, struct page * page)
{
	int result;

	get_nonexclusive_access (file->f_dentry->d_inode);
	result = unix_file_readpage_nolock (file, page);
	drop_nonexclusive_access (file->f_dentry->d_inode);
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
#ifdef NEW_READ_IS_READY
	sink_t userspace_sink;
#else
	flow_t f;
#endif /* NEW_READ_IS_READY */

	/* collect statistics on the number of reads */
	reiser4_stat_file_add (reads);

	inode = file->f_dentry->d_inode;
	result = 0;

	/* build flow */
	assert ("vs-528",
		inode_file_plugin (inode)->flow_by_inode == common_build_flow);
	/* this should now be called userspace_sink_build, now that we have
	 * both sinks and flows.  See discussion of sinks and flows in
	 * www.namesys.com/v4/v4.html */
#ifdef NEW_READ_IS_READ
	result = userspace_sink_build (inode, buf, 1/* user space */, read_amount,
				    *off, READ_OP, &f);
#else
	result = common_build_flow (inode, buf, 1/* user space */, read_amount,
				    *off, READ_OP, &f);
#endif
	if (result)
		return result;

	get_nonexclusive_access (inode);
	
#ifdef NEW_READ_IS_READY
	intrafile_readahead_amount = unix_file_readahead(struct file * file, off, read_amount);
#endif /* NEW_READ_IS_READY */

	coord_init_zero (&coord);
	init_lh (&lh);

	to_read = f.length;
	while (f.length) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/* do not read out of file */
			break;

		/* coord will point to current item on entry and next item on exit */
		result = find_next_item (file, &f.key, &coord, &lh,
					 ZNODE_READ_LOCK);
		if (result != CBK_COORD_FOUND)
			/* item had to be found, as it was not - we have
			 * -EIO */
			break;

		result = zload (coord.node);
		if (result) {
			break;
		}

		/* call read method of found item */
		iplug = item_plugin_by_coord (&coord);
		if (!iplug->s.file.read) {
			result = -EINVAL;
			zrelse (coord.node);
			break;
		}
		
		result = iplug->s.file.read (inode, &coord, &lh, &f);
		zrelse (coord.node);
		if (result) {
			break;
		}
	}

#ifdef NEW_READ_IS_READY
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

	done_lh (&lh);
	if( to_read - f.length ) {
		/* something was read. Update stat data */
		UPDATE_ATIME (inode);
		assert ("vs-675",
			inode_file_plugin (inode)->write_sd_by_inode ==
			common_file_save);
		if (common_file_save (inode))
			warning ("vs-676", "updating stat data failed\n");
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

/* plugin->u.file.write */
ssize_t unix_file_write (struct file * file, /* file to write to */
			 const char * buf, /* comments are needed */
			 size_t size, /* number of bytes ot write */
			 loff_t * off /* position to write which */)
{
	int result;
	struct inode * inode;
	coord_t coord;
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

	result = common_build_flow (inode, (char *)buf, 1/* user space */, size, *off,
				    WRITE_OP, &f);
	if (result)
		return result;

	get_nonexclusive_access (inode);

	init_lh (&lh);
	coord_init_zero (&coord);

	to_write = f.length;
	while (f.length) {
		znode * loaded;

		/* look for file metadata corresponding to position we write
		 * to */
		result = find_next_item (file, &f.key, &coord, &lh, ZNODE_WRITE_LOCK);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/* error occured */
			break;
		}

		/* store what we zload so that we were able to zrelse */
		loaded = coord.node;
		result = zload (loaded);
		if (result) {
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
			zrelse (loaded);
			done_lh (&lh);
			result = tail2extent (inode);
			if (result) {
				drop_nonexclusive_access (inode);
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
		if ((loff_t)get_key_offset (&f.key) > inode->i_size)
			/* file got longer */
			inode->i_size = get_key_offset (&f.key);
	}
	if (coord_set_properly (&f.key, &coord)) {
		reiser4_file_fsdata * fdata;
		seal_t seal;

		fdata = reiser4_get_file_fsdata (file);
		if (!IS_ERR (fdata)) {
			/* re-set seal of last access to file */
			seal_init (&seal, &coord, &f.key);
			fdata->reg.last_access = seal;
			fdata->reg.coord = coord;
			fdata->reg.level = znode_get_level (coord.node);
		}
	}
	done_lh (&lh);

	if( to_write - f.length ) {
		/* something was written. Update stat data */
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		assert ("vs-639",
			inode_file_plugin (inode)->write_sd_by_inode ==
			common_file_save);
		if (common_file_save (inode))
			warning ("vs-636", "updating stat data failed\n");
	}

	drop_nonexclusive_access (inode);

	/* update position in a file */
	*off += (to_write - f.length);
	/* return number of written bytes or error code if nothing is written */
	return (to_write - f.length) ? (to_write - f.length) : result;
}


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


/* returns 1 if file of that size (@new_size) has to be stored in unformatted
 * nodes */
/* Audited by: green(2002.06.15) */
static int should_have_notail (struct inode * inode, loff_t new_size)
{
	if (!inode_tail_plugin (inode))
		return 1;
	return !inode_tail_plugin (inode)->have_tail (inode, new_size);

}


/* decide how to write flow @f into file @inode */
/* Audited by: green(2002.06.15) */
static write_todo unix_file_how_to_write (struct inode * inode, flow_t * f,
					  coord_t * coord)
{
	loff_t new_size;


	/* size file will have after write */
	new_size = get_key_offset (&f->key) + f->length;

	if (new_size <= inode->i_size) {
		/* if file does not get longer - no conversion will be
		 * performed */
		/* AUDIT: Will this also work correctly if we start overwritting
		   extend but then contine to overwrite over the tail? */
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


/* plugin->u.file.release
 * convert all extent items into tail items if necessary */
/* Audited by: green(2002.06.15) */
int unix_file_release (struct file * file)
{
	struct inode * inode;

	inode = file ->f_dentry->d_inode;

	if (should_have_notail (inode, inode->i_size))
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
