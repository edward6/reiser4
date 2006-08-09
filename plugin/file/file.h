/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* this file contains declarations of methods implementing file plugins
   (UNIX_FILE_PLUGIN_ID, SYMLINK_FILE_PLUGIN_ID and CRC_FILE_PLUGIN_ID) */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

/* declarations of functions implementing UNIX_FILE_PLUGIN_ID file plugin */

/* inode operations */
int setattr_unix_file(struct dentry *, struct iattr *);

/* file operations */
ssize_t read_unix_file(struct file *, char __user *buf, size_t read_amount,
		       loff_t *off);
ssize_t write_unix_file(struct file *, const char __user *buf, size_t write_amount,
			loff_t *off);
int ioctl_unix_file(struct inode *, struct file *, unsigned int cmd,
		    unsigned long arg);
int mmap_unix_file(struct file *, struct vm_area_struct *);
int open_unix_file(struct inode *, struct file *);
int release_unix_file(struct inode *, struct file *);
int sync_unix_file(struct file *, struct dentry *, int datasync);
ssize_t sendfile_unix_file(struct file *, loff_t *ppos, size_t count,
			   read_actor_t, void *target);

/* address space operations */
int readpage_unix_file(struct file *, struct page *);
int readpages_unix_file(struct file*, struct address_space*, struct list_head*, unsigned);
int writepages_unix_file(struct address_space *, struct writeback_control *);
int prepare_write_unix_file(struct file *, struct page *, unsigned from,
			    unsigned to);
int commit_write_unix_file(struct file *, struct page *, unsigned from,
			   unsigned to);
long batch_write_unix_file(struct file *, struct write_descriptor *,
			   size_t *written);
sector_t bmap_unix_file(struct address_space *, sector_t lblock);

/* file plugin operations */
int flow_by_inode_unix_file(struct inode *, const char __user *buf,
			    int user, loff_t, loff_t, rw_op, flow_t *);
int owns_item_unix_file(const struct inode *, const coord_t *);
void init_inode_data_unix_file(struct inode *, reiser4_object_create_data *,
			       int create);
int delete_object_unix_file(struct inode *);

/*
 * all the write into unix file is performed by item write method. Write method
 * of unix file plugin only decides which item plugin (extent or tail) and in
 * which mode (one from the enum below) to call
 */
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

struct formatting_plugin;
struct inode;

/* unix file plugin specific part of reiser4 inode */
typedef struct unix_file_info {
	/*
	 * this read-write lock protects file containerization change. Accesses
	 * which do not change file containerization (see file_container_t)
	 * (read, readpage, writepage, write (until tail conversion is
	 * involved)) take read-lock. Accesses which modify file
	 * containerization (truncate, conversion from tail to extent and back)
	 * take write-lock.
	 */
	struct rw_semaphore latch;
	/*
	 * this semaphore is used to serialize writes instead of inode->i_mutex,
	 * because write_unix_file uses get_user_pages which is to be used
	 * under mm->mmap_sem and because it is required to take mm->mmap_sem
	 * before inode->i_mutex, so inode->i_mutex would have to be unlocked
	 * before calling to get_user_pages which is unacceptable
	 */
	struct semaphore write;
	/* this enum specifies which items are used to build the file */
	file_container_t container;
	/*
	 * plugin which controls when file is to be converted to extents and
	 * back to tail
	 */
	struct formatting_plugin *tplug;
	/* if this is set, file is in exclusive use */
	int exclusive_use;
#if REISER4_DEBUG
	/* pointer to task struct of thread owning exclusive access to file */
	void *ea_owner;
	atomic_t nr_neas;
	void *last_reader;
#endif
} unix_file_info_t;

struct unix_file_info *unix_file_inode_data(const struct inode *inode);
void get_exclusive_access(unix_file_info_t *);
void drop_exclusive_access(unix_file_info_t *);
void get_nonexclusive_access(unix_file_info_t *);
void drop_nonexclusive_access(unix_file_info_t *);
int try_to_get_nonexclusive_access(unix_file_info_t *);
int find_file_item(hint_t *, const reiser4_key *, znode_lock_mode,
		   struct inode *);
int find_file_item_nohint(coord_t *, lock_handle *,
			  const reiser4_key *, znode_lock_mode,
			  struct inode *);

int load_file_hint(struct file *, hint_t *);
void save_file_hint(struct file *, const hint_t *);

#include "../item/extent.h"
#include "../item/tail.h"
#include "../item/ctail.h"

struct uf_coord {
	coord_t coord;
	lock_handle *lh;
	int valid;
	union {
		extent_coord_extension_t extent;
		tail_coord_extension_t tail;
		ctail_coord_extension_t ctail;
	} extension;
};

#include "../../forward.h"
#include "../../seal.h"
#include "../../lock.h"

/*
 * This structure is used to speed up file operations (reads and writes).  A
 * hint is a suggestion about where a key resolved to last time.  A seal
 * indicates whether a node has been modified since a hint was last recorded.
 * You check the seal, and if the seal is still valid, you can use the hint
 * without traversing the tree again.
 */
struct hint {
	seal_t seal; /* a seal over last file item accessed */
	uf_coord_t ext_coord;
	loff_t offset;
	znode_lock_mode mode;
	lock_handle lh;
};

void set_hint(hint_t *, const reiser4_key *, znode_lock_mode);
int hint_is_set(const hint_t *);
void unset_hint(hint_t *);
int hint_validate(hint_t *, const reiser4_key *, int check_key,
		  znode_lock_mode);
void hint_init_zero(hint_t *);

int update_file_size(struct inode *, reiser4_key *, int update_sd);
int cut_file_items(struct inode *, loff_t new_size, int update_sd,
		   loff_t cur_size, int (*update_actor) (struct inode *,
							 reiser4_key *, int));

#if REISER4_DEBUG

/* return 1 is exclusive access is obtained, 0 - otherwise */
static inline int ea_obtained(unix_file_info_t * uf_info)
{
	int ret;

	ret = down_read_trylock(&uf_info->latch);
	if (ret)
		up_read(&uf_info->latch);
	return !ret;
}

#endif

/* declarations of functions implementing SYMLINK_FILE_PLUGIN_ID file plugin */
int create_symlink(struct inode *symlink, struct inode *dir,
		   reiser4_object_create_data *);
void destroy_inode_symlink(struct inode *);

/* declarations of functions implementing CRC_FILE_PLUGIN_ID file plugin */

/* inode operations */
int setattr_cryptcompress(struct dentry *, struct iattr *);

/* file operations */
ssize_t read_cryptcompress(struct file *, char __user *buf, size_t read_amount,
			   loff_t * off);
ssize_t write_cryptcompress(struct file *, const char __user *buf, size_t write_amount,
			    loff_t * off);
int mmap_cryptcompress(struct file *, struct vm_area_struct *);
ssize_t sendfile_cryptcompress(struct file *file, loff_t *ppos, size_t count,
			       read_actor_t actor, void *target);
int release_cryptcompress(struct inode *, struct file *);

/* address space operations */
extern int readpage_cryptcompress(struct file *, struct page *);
extern int writepages_cryptcompress(struct address_space *,
				     struct writeback_control *);

/* file plugin operations */
int flow_by_inode_cryptcompress(struct inode *, const char __user *buf,
				int user, loff_t, loff_t, rw_op, flow_t *);
int key_by_inode_cryptcompress(struct inode *, loff_t off, reiser4_key *);
int create_cryptcompress(struct inode *, struct inode *,
			 reiser4_object_create_data *);
int delete_cryptcompress(struct inode *);
void init_inode_data_cryptcompress(struct inode *, reiser4_object_create_data *,
				   int create);
int cut_tree_worker_cryptcompress(tap_t *, const reiser4_key * from_key,
				  const reiser4_key * to_key,
				  reiser4_key * smallest_removed,
				  struct inode *object, int truncate,
				  int *progress);
void destroy_inode_cryptcompress(struct inode *);

extern reiser4_plugin_ops cryptcompress_plugin_ops;

#define WRITE_GRANULARITY 32

int tail2extent(unix_file_info_t *);
int extent2tail(unix_file_info_t *);

int goto_right_neighbor(coord_t *, lock_handle *);
int find_or_create_extent(struct page *);
int equal_to_ldk(znode *, const reiser4_key *);

void init_uf_coord(uf_coord_t *uf_coord, lock_handle *lh);

static inline int cbk_errored(int cbk_result)
{
	return (cbk_result != CBK_COORD_NOTFOUND
		&& cbk_result != CBK_COORD_FOUND);
}

/* __REISER4_FILE_H__ */
#endif

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * scroll-step: 1
 * End:
*/
