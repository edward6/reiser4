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
int unix_file_readpage_nolock (struct file * file UNUSED_ARG, struct page * page)
{
	int result;
	new_coord coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin * iplug;
	struct readpage_arg arg;


	/* get key of first byte of the page */
	unix_file_key_by_inode (page->mapping->host,
				(loff_t)page->index << PAGE_CACHE_SHIFT, &key);
	
	ncoord_init_zero (&coord);
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
/* Audited by: green(2002.06.15) */
ssize_t unix_file_read (struct file * file, char * buf, size_t size,
			loff_t * off)
{
	int result;
	struct inode * inode;
	new_coord coord;
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

	get_nonexclusive_access (inode);

	to_read = f.length;
	while (f.length) {
		if ((loff_t)get_key_offset (&f.key) >= inode->i_size)
			/* do not read out of file */
			break;
		
		/* look for file metadata corresponding to position we read
		 * from */
		/* AUDIT: lock handle not initialized prior to usage */
		result = find_item (&f.key, &coord, &lh,
				    ZNODE_READ_LOCK);
		switch (result) {
		case CBK_COORD_FOUND:
			/* call read method of found item */
			iplug = item_plugin_by_coord (&coord);
			if (!iplug->s.file.read)
				result = -EINVAL;
			else {
				/* use generic read ahead if item has readpage
				 * method */
				if (iplug->s.file.readpage)
					page_cache_readahead (file,
							      (unsigned long)(get_key_offset (&f.key) >> PAGE_CACHE_SHIFT));
					
				result = iplug->s.file.read (inode, &coord,
							     &lh, &f);
			}
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
	return (to_read - f.length) ? (to_read - f.length) : result;
}


/* these are write modes. Certain mode is chosen depending on resulting file
 * size and current metadata of file */
typedef enum {
	WRITE_EXTENT,
	WRITE_TAIL,
	CONVERT
} write_todo;

static write_todo unix_file_how_to_write (struct inode *, flow_t *, new_coord *);

/* plugin->u.file.write */
/* Audited by: green(2002.06.15) */
ssize_t unix_file_write (struct file * file, /* file to write to */
			 const char * buf, /* comments are needed */
			 size_t size, /* number of bytes ot write */
			 loff_t * off /* position to write which */)
{
	int result;
	struct inode * inode;
	new_coord coord;
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

	to_write = f.length;
	while (f.length) {
		znode * loaded;

		/* look for file metadata corresponding to position we write
		 * to */
		/* AUDIT: lock handle used uninitialised here */
		result = find_item (&f.key, &coord, &lh, ZNODE_WRITE_LOCK);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/* error occured */
			done_lh (&lh);
			break;
		}

		/* store what we zload so that we were able to zrelse */
		loaded = coord.node;
		result = zload (loaded);
		if (result) {
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
			zrelse (loaded);
			done_lh (&lh);
			result = tail2extent (inode);
			if (result) {
				drop_nonexclusive_access (inode);
				return result;
			}
			continue;

		default:
			impossible ("vs-293", "unknown write mode");
		}
		zrelse (loaded);
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
			     new_coord * coord)
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
					  new_coord * coord)
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
			 const new_coord *coord /* coord to check */ )
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
