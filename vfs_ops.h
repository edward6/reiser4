/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* vfs_ops.c's exported symbols */

#if !defined( __FS_REISER4_VFS_OPS_H__ )
#define __FS_REISER4_VFS_OPS_H__

#include "forward.h"
#include "coord.h"
#include "seal.h"
#include "type_safe_list.h"
#include "plugin/file/file.h"
#include "super.h"
#include "readahead.h"

#include <linux/types.h>	/* for loff_t */
#include <linux/fs.h>		/* for struct address_space */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/mm.h>
#include <linux/backing-dev.h>

/* address space operations */
int reiser4_writepage(struct page *, struct writeback_control *);
int reiser4_set_page_dirty(struct page *);
int reiser4_readpages(struct file *, struct address_space *,
		      struct list_head *pages, unsigned nr_pages);
int reiser4_invalidatepage(struct page *, unsigned long offset);
int reiser4_releasepage(struct page *, int gfp);

extern int reiser4_mark_inode_dirty(struct inode *object);
extern int reiser4_update_sd(struct inode *object);
extern int reiser4_add_nlink(struct inode *, struct inode *, int);
extern int reiser4_del_nlink(struct inode *, struct inode *, int);

extern struct super_operations reiser4_super_operations;
extern struct export_operations reiser4_export_operations;
extern struct dentry_operations reiser4_dentry_operations;

extern int reiser4_start_up_io(struct page *page);
extern void reiser4_clear_page_dirty(struct page *);
extern void reiser4_throttle_write(struct inode *);
ON_DEBUG(int jnode_is_releasable(jnode *));

#define CAPTURE_APAGE_BURST (1024l)
void writeout(struct super_block *, struct writeback_control *);

/* locking: fields of per file descriptor readdir_pos and ->f_pos are
 * protected by ->i_sem on inode. Under this lock following invariant
 * holds:
 *
 *     file descriptor is "looking" at the entry_no-th directory entry from
 *     the beginning of directory. This entry has key dir_entry_key and is
 *     pos-th entry with duplicate-key sequence.
 *
 */

/* logical position within directory */
typedef struct {
	/* key of directory entry (actually, part of a key sufficient to
	   identify directory entry)  */
	de_id dir_entry_key;
	/* ordinal number of directory entry among all entries with the same
	   key. (Starting from 0.) */
	unsigned pos;
} dir_pos;

typedef struct {
	/* f_pos corresponding to this readdir position */
	__u64 fpos;
	/* logical position within directory */
	dir_pos position;
	/* logical number of directory entry within
	   directory  */
	__u64 entry_no;
} readdir_pos;

/*
 * this is used to speed up lookups for directory entry: on initial call to
 * ->lookup() seal and coord of directory entry (if found, that is) are stored
 * in struct dentry and reused later to avoid tree traversals.
 */
typedef struct de_location {
	/* seal covering directory entry */
	seal_t entry_seal;
	/* coord of directory entry */
	coord_t entry_coord;
	/* ordinal number of directory entry among all entries with the same
	   key. (Starting from 0.) */
	int pos;
} de_location;

/* &reiser4_dentry_fsdata - reiser4-specific data attached to dentries.

   This is allocated dynamically and released in d_op->d_release()

   Currently it only contains cached location (hint) of directory entry, but
   it is expected that other information will be accumulated here.
*/
typedef struct reiser4_dentry_fsdata {
	/* here will go fields filled by ->lookup() to speedup next
	   create/unlink, like blocknr of znode with stat-data, or key
	   of stat-data.
	 */
	de_location dec;
	int stateless;		/* created through reiser4_decode_fh, needs special
				 * treatment in readdir. */
} reiser4_dentry_fsdata;

/* declare data types and manipulation functions for readdir list. */
TYPE_SAFE_LIST_DECLARE(readdir);

struct dir_cursor;

/* &reiser4_dentry_fsdata - reiser4-specific data attached to files.

   This is allocated dynamically and released in reiser4_release() */
struct reiser4_file_fsdata {
	/* pointer back to the struct file which this reiser4_file_fsdata is
	 * part of */
	struct file *back;
	/* detached cursor for stateless readdir. */
	struct dir_cursor *cursor;
	/* We need both directory and regular file parts here, because there
	   are file system objects that are files and directories. */
	struct {
		readdir_pos readdir;
		readdir_list_link linkage;
	} dir;
	/* hints to speed up operations with regular files: read and write. */
	struct {
		hint_t hint;
	} reg;
	/* */
	struct {
		/* this is called by reiser4_readpages if set */
		void (*readpages) (struct address_space *,
				   struct list_head * pages, void *data);
		/* reiser4_readpaextended coord. It is set by read_extent before
		   calling page_cache_readahead */
		void *data;
	} ra2;
	struct reiser4_file_ra_state ra1;

};

TYPE_SAFE_LIST_DEFINE(readdir, reiser4_file_fsdata, dir.linkage);

extern reiser4_dentry_fsdata *reiser4_get_dentry_fsdata(struct dentry *dentry);
extern reiser4_file_fsdata *reiser4_get_file_fsdata(struct file *f);
extern void reiser4_free_dentry_fsdata(struct dentry *dentry);
extern void reiser4_free_file_fsdata(struct file *f);
extern void reiser4_free_fsdata(reiser4_file_fsdata * fsdata);

extern reiser4_file_fsdata *create_fsdata(struct file *file, unsigned int gfp);

extern void reiser4_handle_error(void);
extern int reiser4_parse_options(struct super_block *, char *);

extern int d_cursor_init(void);
extern void d_cursor_done(void);

/* __FS_REISER4_VFS_OPS_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
