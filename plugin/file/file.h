/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

/* declarations of functions implementing file plugin for unix file plugin */
int truncate_unix_file(struct inode *, loff_t size);
int readpage_unix_file(void *, struct page *);
int writepage_unix_file(struct page *);
ssize_t read_unix_file(struct file *, char *buf, size_t size, loff_t *off);
ssize_t write_unix_file(struct file *, const char *buf, size_t size, loff_t *off);
int release_unix_file(struct inode *inode, struct file *);
int ioctl_unix_file(struct inode *, struct file *, unsigned int cmd, unsigned long arg);
int mmap_unix_file(struct file *, struct vm_area_struct *vma);
int get_block_unix_file(struct inode *, sector_t block, struct buffer_head *bh_result, int create);
int flow_by_inode_unix_file(struct inode *, char *buf, int user, size_t, loff_t, rw_op, flow_t *);
int key_by_inode_unix_file(struct inode *, loff_t off, reiser4_key *);
int delete_unix_file(struct inode *);
int owns_item_unix_file(const struct inode *, const coord_t *);
int setattr_unix_file(struct inode *, struct iattr *);
void readpages_unix_file(struct file *, struct address_space *, struct list_head *pages);
void init_inode_data_unix_file(struct inode *, reiser4_object_create_data *, int create);
int pre_delete_unix_file(struct inode *);

/* all the write into unix file is performed by item write method. Write method of unix file plugin only decides which
   item plugin (extent or tail) and in which mode (one from the enum below) to call */
typedef enum {
	FIRST_ITEM = 1,
	APPEND_ITEM = 2,
	OVERWRITE_ITEM = 3
} write_mode_t;


/* unix file may be in one the following states */
typedef enum {
	UF_CONTAINER_UNKNOWN = 0,
	UF_CONTAINER_TAILS = 1,
	UF_CONTAINER_EXTENTS = 2,
	UF_CONTAINER_EMPTY = 3
} file_container_t;

#include "../../latch.h"

struct tail_plugin;
struct inode;

/* unix file plugin specific part of reiser4 inode */
typedef struct unix_file_info {
	rw_latch_t latch; /* this read-write lock protects file containerization change. Accesses which do not change file containerization (see file_container_t)
			     (read, readpage, writepage, write (until tail conversion is involved)) take
			     read-lock. Accesses which modify file containerization (truncate, conversion from tail to extent and
			     back) take write-lock. */
	file_container_t container; /* this shows which items are used to build the file */
	struct tail_plugin *tplug; /* tail policy plugin which controls when file is to be converted to extents and back
				      to tail */
	struct inode *inode;
	int exclusive_use;
#if REISER4_DEBUG
	void *ea_owner; /* pointer to task struct of thread owning exclusive access to file */
#endif
#if REISER4_LARGE_KEY
	__u64 ordering;
#endif
} unix_file_info_t;

inline struct unix_file_info *unix_file_inode_data(const struct inode * inode);


#include "../../coord.h"
#include "../item/extent.h"
#include "../item/tail.h"

struct uf_coord {
	coord_t base_coord;
	lock_handle *lh;
	int valid;
	union {
		extent_coord_extension_t extent;
		tail_coord_extension_t tail;
	} extension;
};

#include "../../seal.h"

/* FIXME: comments */
struct hint {
	seal_t seal;
	uf_coord_t coord;
	loff_t offset;
	tree_level level;
};

void set_hint(hint_t *, const reiser4_key *);
void unset_hint(hint_t *);
int hint_validate(hint_t *, const reiser4_key *, int check_key, znode_lock_mode);
int update_inode_and_sd_if_necessary(struct inode *, loff_t new_size, int update_i_size, int update_sd);

#if REISER4_LARGE_KEY
static inline __u64 get_inode_ordering(const struct inode *inode)
{
	return unix_file_inode_data(inode)->ordering;
}

static inline void set_inode_ordering(const struct inode *inode, __u64 ordering)
{
	unix_file_inode_data(inode)->ordering = ordering;
}

#else

#define get_inode_ordering(inode) (0)
#define set_inode_ordering(inode, val) noop

#endif

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
