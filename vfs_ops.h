/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* vfs_ops.c's exported symbols */

#if !defined( __FS_REISER4_VFS_OPS_H__ )
#define __FS_REISER4_VFS_OPS_H__

#include "forward.h"
#include "coord.h"
#include "seal.h"
#include "tslist.h"
#include "plugin/dir/dir.h"
#include "plugin/file/file.h"

#include <linux/types.h>	/* for loff_t */
#include <linux/fs.h>		/* for struct address_space */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/mm.h>

extern int reiser4_mark_inode_dirty(struct inode *object);
extern int reiser4_update_sd(struct inode *object);
extern int reiser4_add_nlink(struct inode *, struct inode *, int);
extern int reiser4_del_nlink(struct inode *, struct inode *, int);

/* NIKITA-FIXME-HANS: always remove dead code after two weeks for funeral have passed */
/*extern int truncate_object(struct inode *inode, loff_t size);*/

extern void reiser4_free_dentry_fsdata(struct dentry *dentry);

extern struct file_operations reiser4_file_operations;
extern struct inode_operations reiser4_inode_operations;
extern struct inode_operations reiser4_symlink_inode_operations;
extern struct super_operations reiser4_super_operations;
extern struct address_space_operations reiser4_as_operations;
extern struct dentry_operations reiser4_dentry_operation;

static inline int set_page_dirty_internal (struct page * page) /* NIKITA-FIXME-HANS: what does internal mean in this context? */
{
	return __set_page_dirty_nobuffers (page);
}

extern int reiser4_invalidatepage(struct page *page, unsigned long offset);
extern int reiser4_releasepage(struct page *page, int gfp);
extern int reiser4_writepages(struct address_space *, struct writeback_control *wbc);
extern int reiser4_sync_page(struct page *page);

/* NIKITA-FIXME-HANS: is control the right adjective?  how about a summary sentence defining what this structure accomplishes
 * functionally? */
typedef struct de_control {
	/* seal covering directory entry */
	seal_t entry_seal;
	/* coord of directory entry */
	coord_t entry_coord;
	/* ordinal number of directory entry among all entries with the same
	   key. (Starting from 0.) */
	int pos;
} de_control;

/* &reiser4_dentry_fsdata - reiser4-specific data attached to dentries.
  
   This is allocated dynamically and released in d_op->d_release() 

NIKITA-FIXME-HANS: Why the use of a struct with only one thing in it?*/
typedef struct reiser4_dentry_fsdata {
	/* here will go fields filled by ->lookup() to speedup next
	   create/unlink, like blocknr of znode with stat-data, or key
	   of stat-data.
	*/
	de_control dec;
} reiser4_dentry_fsdata;

/* NIKITA-FIXME-HANS: brief me on what this does */
TS_LIST_DECLARE(readdir);

/* &reiser4_dentry_fsdata - reiser4-specific data attached to files.
  
   This is allocated dynamically and released in reiser4_release() */
typedef struct reiser4_file_fsdata {

	/* NIKITA-FIXME-HANS: go through all your code and comment, including here */
	struct file *back;
	/* We need both directory and regular file parts here, because there
	   are file system objects that are files and directories. */
	struct {
		readdir_pos readdir;
		readdir_list_link linkage;
	} dir;
	struct {
		hint_t hint;
		/* this is set by read_extent before calling page_cache_readahead */
		void *coord;
	} reg;
/* NIKITA-FIXME-HANS: reg means what? */
} reiser4_file_fsdata;

TS_LIST_DEFINE(readdir, reiser4_file_fsdata, dir.linkage);

extern reiser4_dentry_fsdata *reiser4_get_dentry_fsdata(struct dentry *dentry);
extern reiser4_file_fsdata *reiser4_get_file_fsdata(struct file *f);
extern void reiser4_handle_error(void);

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
