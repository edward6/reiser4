/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

#include "../../seal.h"

/* declarations of functions implementing file plugin for unix file plugin */
int truncate_unix_file(struct inode *, loff_t size);
int readpage_unix_file(void *, struct page *);
void readpages_unix_file(struct file *, struct address_space *,
			 struct list_head *pages);
void init_inode_data_unix_file(struct inode *, int create);
int pre_delete_unix_file(struct inode *);

int writepage_unix_file(struct page *);
ssize_t read_unix_file(struct file *, char *buf, size_t size, loff_t * off);
ssize_t write_unix_file(struct file *, const char *buf, size_t size, loff_t * off);
int release_unix_file(struct file *);
int ioctl_unix_file(struct inode *, struct file *, unsigned int cmd, unsigned long arg);
int mmap_unix_file(struct file *, struct vm_area_struct *vma);
int get_block_unix_file(struct inode *, sector_t block, struct buffer_head *bh_result, int create);
int flow_by_inode_unix_file(struct inode *, char *buf, int user, size_t, loff_t, rw_op, flow_t *);
int key_by_inode_unix_file(struct inode *, loff_t off, reiser4_key *);
int delete_unix_file(struct inode *);
int create_unix_file(struct inode *object, struct inode *parent, reiser4_object_create_data * data);
int owns_item_unix_file(const struct inode *, const coord_t *);
int setattr_unix_file(struct inode *inode, struct iattr *attr);

/* these are used by item methods */
typedef enum {
	FIRST_ITEM = 1,
	APPEND_ITEM = 2,
	OVERWRITE_ITEM = 3,
	RESEARCH = 4
} write_mode;

write_mode how_to_write(coord_t *, const reiser4_key *);

typedef enum {
	COORD_RIGHT_STATE = 1,
	COORD_WRONG_STATE = 2,
	COORD_UNKNOWN_STATE = 3
} coord_state_t;

#define HINT_MAGIC 1081120
typedef struct {
	seal_t seal;
	coord_t coord;
	loff_t offset;
	tree_level level;
	znode_lock_mode lock;
#if REISER4_DEBUG
	int magic;
#endif
} hint_t;

void set_hint(hint_t *, const reiser4_key *, coord_t *, coord_state_t);
void unset_hint(hint_t *);
int hint_validate(hint_t *, const reiser4_key *, lock_handle *, int check_key);
int update_inode_and_sd_if_necessary(struct inode *, loff_t new_size, int update_i_size, int update_sd);

typedef enum {
	UNIX_FILE_STATE_UNKNOWN = 0,
	UNIX_FILE_BUILT_OF_TAILS = 1,
	UNIX_FILE_BUILT_OF_EXTENTS = 2,
	UNIX_FILE_EMPTY = 3
} file_state;

#include "../../latch.h"

struct tail_plugin;
struct inode;

typedef struct unix_file_info {
	/* truncate, tail2extent and extent2tail use down_write, read, write, readpage - down_read */
	rw_latch_t latch;
	file_state state;
	struct tail_plugin *tplug;
	struct inode *inode;
	int exclusive_use;
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
