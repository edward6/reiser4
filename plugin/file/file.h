/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * this file contains:
 * declarations of functions implementing file plugin for ordinary file
 */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__


void get_exclusive_access      (struct inode * inode);
void drop_exclusive_access     (struct inode * inode);
void get_nonexclusive_access   (struct inode * inode);
void drop_nonexclusive_access  (struct inode * inode);
int  tail2extent               (struct inode * inode);
int  extent2tail               (struct file * file);
int  unix_file_readpage_nolock (struct file * file, struct page * page);
int  find_item                 (reiser4_key * key, new_coord * coord,
				lock_handle * lh, znode_lock_mode lock_mode);



/* plugin->file.* */
int     unix_file_create   (struct inode *object, struct inode *parent,
				 reiser4_object_create_data *data );
ssize_t unix_file_write    (struct file * file, const char * buf, size_t size,
				loff_t *off);
ssize_t unix_file_read     (struct file * file, char * buf, size_t size,
				loff_t * off);
int     unix_file_release  (struct file * file);
int     unix_file_mmap     (struct file * file, struct vm_area_struct * vma);
int     unix_file_truncate (struct inode * inode, loff_t size);
int     unix_file_key_by_inode ( struct inode *, loff_t off, reiser4_key * );
int     unix_file_create   (struct inode * object, struct inode * parent,
				reiser4_object_create_data *data);
int     unix_file_readpage (struct file * file, struct page * page);
int     unix_file_owns_item( const struct inode *, const new_coord *);


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
