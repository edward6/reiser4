/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Declarations of data-types/functions for file (object) plugins.
 * see fs/reiser4/plugin/plugin.c for details
 */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

/* a flow is a sequence of bytes being written to or read from the tree.  The
   tree will slice the flow into items while storing it into nodes, but all of
   that is hidden from anything outside the tree.  */

/* FIXME-VS: shouldn't this be somewhere else? */
struct flow {
	reiser4_key  key;    /* key of start of flow's sequence of bytes */
	size_t       length; /* length of flow's sequence of bytes */
	char        *data;   /* start of flow's sequence of bytes */
};


/* plugin->file.* */
int     ordinary_file_create   (struct inode *object, struct inode *parent,
				 reiser4_object_create_data *data );
ssize_t ordinary_file_write    (struct file * file, char * buf, size_t size,
				loff_t *off);
ssize_t ordinary_file_read     (struct file * file, char * buf, size_t size,
				loff_t * off);
int     ordinary_file_release  (struct file * file);
int     ordinary_file_truncate (struct inode * inode, loff_t size);
int     ordinary_file_create   (struct inode * object, struct inode * parent,
				reiser4_object_create_data *data);
int     ordinary_readpage      (struct file * file, struct page * page);
int     ordinary_file_owns_item( const struct inode *, const tree_coord *);


/* __REISER4_FILE_H__ */
#endif

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
