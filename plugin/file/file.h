/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

/* declarations of functions implementing file plugin for unix file plugin */
int unix_file_truncate(struct inode *, loff_t size);
int unix_file_readpage(void *, struct page *);
void unix_file_readpages(struct file *, struct address_space *,
			 struct list_head *pages);
void unix_file_init_inode(struct inode *inode, int create);

int unix_file_writepage(struct page *);
ssize_t unix_file_read(struct file *, char *buf, size_t size, loff_t * off);
ssize_t unix_file_write(struct file *, const char *buf, size_t size, loff_t * off);
int unix_file_release(struct file *);
int unix_file_ioctl(struct inode *, struct file *, unsigned int cmd, unsigned long arg);
int unix_file_mmap(struct file *, struct vm_area_struct *vma);
int unix_file_get_block(struct inode *, sector_t block, struct buffer_head *bh_result, int create);

int unix_file_key_by_inode(struct inode *, loff_t off, reiser4_key *);
int unix_file_delete(struct inode *);
int unix_file_create(struct inode *object, struct inode *parent, reiser4_object_create_data * data);
int unix_file_owns_item(const struct inode *, const coord_t *);
int unix_file_setattr(struct inode *inode, struct iattr *attr);

/* these are used by item methods */
typedef enum {
	FIRST_ITEM = 1,
	APPEND_ITEM = 2,
	OVERWRITE_ITEM = 3,
	RESEARCH = 4
} write_mode;

write_mode how_to_write(coord_t *, lock_handle *, const reiser4_key *);
void set_hint(struct sealed_coord *, const reiser4_key *, coord_t *);
void unset_hint(struct sealed_coord *);
int hint_validate(struct sealed_coord *, const reiser4_key *, coord_t *, lock_handle *);
int update_inode_and_sd_if_necessary(struct inode *, loff_t new_size, int update_i_size);

typedef enum {
	UNIX_FILE_STATE_UNKNOWN = 0,
	UNIX_FILE_BUILT_OF_TAILS = 1,
	UNIX_FILE_BUILT_OF_EXTENTS = 2,
	UNIX_FILE_EMPTY = 3
} file_state;

#include "../../latch.h"

typedef struct unix_file_info {
	/* truncate, tail2extent and extent2tail use down_write, read, write, readpage - down_read */
	rw_latch_t latch;
	int state;
#if REISER4_DEBUG
	/* pointer to task struct of thread owning exclusive access to file */
	void *ea_owner;
#endif
} unix_file_info_t;

inline struct unix_file_info *unix_file_inode_data(const struct inode * inode);

/* this is to avoid repeated calculation of item's length and key */
struct coord_item_info {
	reiser4_key key;
	unsigned nr_units;
};


/* __REISER4_FILE_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
