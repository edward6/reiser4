/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */


#include "../../reiser4.h"

/*
 * all file data have to be stored in unformatted nodes
 */
#define should_have_notail(inode,new_size) \
!get_object_state (inode)->tail->have_tail (inode, new_size)

/*
 * not all file data have to be stored in unformatted nodes
 */
#define should_have_tail(inode) \
get_object_state (inode)->tail->have_tail (inode, inode->i_size)

/* Dead USING_INSERT_UNITYPE_FLOW code removed from version 1.76 of this file. */

typedef enum {
	WRITE_EXTENT,
	WRITE_TAIL,
	CONVERT
} write_todo;


/*
 * look for item of file @inode corresponding to @key
 */
static int find_item (reiser4_key * key, tree_coord * coord,
		      reiser4_lock_handle * lh,
		      znode_lock_mode lock_mode)
{
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


/*
 * decide how to write @count bytes to position @offset of file @inode
 */
write_todo what_todo (struct inode * inode, flow_t * f, tree_coord * coord)
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

	assert ("vs-377", get_object_state (inode)->tail->have_tail);

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


static int tail2extent (struct inode * inode UNUSED_ARG,
			tree_coord * coord UNUSED_ARG,
			reiser4_lock_handle * lh UNUSED_ARG)
{
	return 0;
}


/*
 * plugin->u.file.write
 */
ssize_t ordinary_file_write (struct file * file, char * buf, size_t size,
			     loff_t * off)
{
	int result;
	struct inode * inode;
	tree_coord coord;
	reiser4_lock_handle lh;	
	size_t to_write;
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
	 *
	 * FIXME: looks like this does exactly what common_build_flow does,
	 * which should be the file's flow_by_inode method?  it should use
	 * that method?
	 */
	f.length = size;
	f.data = buf;
	build_sd_key (inode, &f.key);
	set_key_type (&f.key, KEY_BODY_MINOR );
	set_key_offset (&f.key, ( __u64 ) *off);

	to_write = f.length;
	while (f.length) {
		init_coord (&coord);
		init_lh (&lh);

		if (get_key_objectid (&f.key) == 0x100c6)
			info ("writing to file 0x100c6\n");
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
			iplug = item_plugin_by_id (EXTENT_POINTER_IT);
			/* resolves to extent_write function */

			result = iplug->s.file.write (inode, &coord, &lh, &f);
			break;

		case WRITE_TAIL:
			iplug = item_plugin_by_id (TAIL_IT);
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
 * plugin->u.file.readpage
 * this finds item of file corresponding to page being read in and calls its
 * readpage method
 */
int ordinary_readpage (struct file * file UNUSED_ARG, struct page * page)
{
	int result;
	tree_coord coord;
	reiser4_lock_handle lh;
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


/*
 * plugin->u.file.read
 */
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
	reiser4_lock_handle lh;
	size_t to_read;
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
	f.length = size;
	f.data = buf;
	build_sd_key (inode, &f.key);
	set_key_type (&f.key, KEY_BODY_MINOR );
	set_key_offset (&f.key, ( __u64 ) *off);

	to_read = f.length;
	while (f.length) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/*
			 * do not read out of file
			 */
			break;
		init_coord (&coord);
		init_lh (&lh);
		
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


/*
 * plugin->u.file.truncate
 */
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


/*
 * plugin->u.file.create
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
