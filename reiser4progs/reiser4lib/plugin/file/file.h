/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Declarations of data-types/functions for file (object) plugins.
 * see fs/reiser4/plugin/plugin.c for details
 */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

				/* a flow is a datum being added to or
                                   read from the tree.  The tree will
                                   slice the flow into items
                                   while storing it into nodes, but
                                   all of that is hidden from anything
                                   outside the tree.  */


struct flow {
	reiser4_key  key;    /* key of start of flow's data */
	size_t       length; /* length of flow's data */
	char        *data;   /* start of flow's data */
};


/** create sd for ordinary file. Just pass control to
    fs/reiser4/plugin/object.c:common_file_save() */
extern int ordinary_file_create( struct inode *object, struct inode *parent,
				 reiser4_object_create_data *data );
ssize_t reiser4_ordinary_file_write (struct file * file, 
				     flow * f, loff_t *off);
ssize_t reiser4_ordinary_file_read (struct file * file,
				    flow * f, loff_t * off);
int reiser4_ordinary_file_truncate (struct inode * inode, loff_t size);
int reiser4_ordinary_file_find_item (reiser4_tree * tree, reiser4_key * key,
				     tree_coord * coord,
				     reiser4_lock_handle * lh);
int reiser4_ordinary_readpage (struct file * file, struct page * page);


struct page * reiser4_get_page (struct inode *, unsigned long long);
int reiser4_copy_to_page (struct page *, flow *, unsigned);
int reiser4_copy_from_page (struct page *, flow *, unsigned);
void reiser4_put_page (struct page *);


/* part of item plugin. These are operations specific to items regular file
   metadata are built of */
typedef struct file_ops {
	int (* write) (struct inode *,
		       tree_coord *,
		       reiser4_lock_handle *,
		       flow *);
	int (* fill_page) (struct page *, tree_coord *,
			   reiser4_lock_handle *);
} file_ops;


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
