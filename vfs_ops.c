/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Interface to VFS. Reiser4 {file|inode|address_space|dentry}_operations
 * are defined here.
 */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/file/file.h"
#include "plugin/security/perm.h"
#include "plugin/oid/oid.h"
#include "plugin/disk_format/disk_format.h"
#include "plugin/plugin.h"
#include "plugin/object.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "trace.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "ioctl.h"

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>

/* inode operations */

static int reiser4_create(struct inode *, struct dentry *, int);
static struct dentry *reiser4_lookup(struct inode *, struct dentry *);
static int reiser4_link(struct dentry *, struct inode *, struct dentry *);
static int reiser4_unlink(struct inode *, struct dentry *);
static int reiser4_rmdir(struct inode *, struct dentry *);
static int reiser4_symlink(struct inode *, struct dentry *, const char *);
static int reiser4_mkdir(struct inode *, struct dentry *, int);
static int reiser4_mknod(struct inode *, struct dentry *, int, int);
static int reiser4_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);
static int reiser4_readlink(struct dentry *, char *, int);
static int reiser4_follow_link(struct dentry *, struct nameidata *);
static void reiser4_truncate(struct inode *);
static int reiser4_permission(struct inode *, int);
static int reiser4_setattr(struct dentry *, struct iattr *);
static int reiser4_getattr(struct vfsmount *mnt, struct dentry *, struct kstat *);

#if 0
static int reiser4_setxattr(struct dentry *, const char *, void *, size_t, int);
static ssize_t reiser4_getxattr(struct dentry *, const char *, void *, size_t);
static ssize_t reiser4_listxattr(struct dentry *, char *, size_t);
static int reiser4_removexattr(struct dentry *, const char *);
#endif

/* file operations */

static loff_t reiser4_llseek(struct file *, loff_t, int);
static ssize_t reiser4_read(struct file *, char *, size_t, loff_t *);
static ssize_t reiser4_write(struct file *, const char *, size_t, loff_t *);
static int reiser4_readdir(struct file *, void *, filldir_t);
static int reiser4_mmap(struct file *, struct vm_area_struct *);
static int reiser4_release(struct inode *, struct file *);
#if 0
static unsigned int reiser4_poll(struct file *, struct poll_table_struct *);
static int reiser4_open(struct inode *, struct file *);
static int reiser4_flush(struct file *);
static int reiser4_fsync(struct file *, struct dentry *, int datasync);
static int reiser4_fasync(int, struct file *, int);
static int reiser4_lock(struct file *, int, struct file_lock *);
static ssize_t reiser4_readv(struct file *, const struct iovec *, unsigned long, loff_t *);
static ssize_t reiser4_writev(struct file *, const struct iovec *, unsigned long, loff_t *);
static ssize_t reiser4_sendpage(struct file *, struct page *, int, size_t, loff_t *, int);
static unsigned long reiser4_get_unmapped_area(struct file *, unsigned long,
					       unsigned long, unsigned long, unsigned long);
#endif

/* super operations */

static struct inode *reiser4_alloc_inode(struct super_block *super);
static void reiser4_destroy_inode(struct inode *inode);
static void reiser4_dirty_inode(struct inode *);
static void reiser4_drop_inode(struct inode *);
static void reiser4_delete_inode(struct inode *);
static void reiser4_write_super(struct super_block *);
static int reiser4_statfs(struct super_block *, struct statfs *);
static void reiser4_kill_super(struct super_block *);
static int reiser4_show_options(struct seq_file *m, struct vfsmount *mnt);
static int reiser4_fill_super(struct super_block *s, void *data, int silent);
#if 0
static void reiser4_write_inode(struct inode *, int);
static void reiser4_put_inode(struct inode *);
static void reiser4_write_super_lockfs(struct super_block *);
static void reiser4_unlockfs(struct super_block *);
static int reiser4_remount_fs(struct super_block *, int *, char *);
static void reiser4_clear_inode(struct inode *);
static struct dentry *reiser4_fh_to_dentry(struct super_block *sb, __u32 * fh, int len, int fhtype, int parent);
static int reiser4_dentry_to_fh(struct dentry *, __u32 * fh, int *lenp, int need_parent);
#endif

/* address space operations */

static int reiser4_writepage(struct page *);
static int reiser4_readpage(struct file *, struct page *);
static int reiser4_vm_writeback(struct page *page, struct writeback_control *wbc);
/*
static int reiser4_prepare_write(struct file *, 
				 struct page *, unsigned, unsigned);
static int reiser4_commit_write(struct file *, 
				struct page *, unsigned, unsigned);
*/
static sector_t reiser4_bmap(struct address_space *, sector_t);
/*
static int reiser4_direct_IO(int, struct inode *, 
			     struct kiobuf *, unsigned long, int);
*/
extern struct dentry_operations reiser4_dentry_operation;

static struct file_system_type reiser4_fs_type;

static int invoke_create_method(struct inode *parent, struct dentry *dentry, reiser4_object_create_data * data);

/**
 * reiser4_lookup() - entry point for ->lookup() method.
 *
 * This is a wrapper for lookup_object which is a wrapper for the directory plugin that does the lookup.
 *
 * This is installed in ->lookup() in reiser4_inode_operations.
 */
/* Audited by: umka (2002.06.12) */
static struct dentry *
reiser4_lookup(struct inode *parent,	/* directory within which we are to look for the name
					 * specified in dentry */
	       struct dentry *dentry	/* this contains the name that is to be looked for on entry,
					   and on exit contains a filled in dentry with a pointer to
					   the inode (unless name not found) */
    )
{
	dir_plugin *dplug;
	int retval;
	struct dentry *result;
	REISER4_ENTRY_PTR(parent->i_sb);

	assert("nikita-403", parent != NULL);
	assert("nikita-404", dentry != NULL);

	/* find @parent directory plugin and make sure that it has lookup
	 * method */
	dplug = inode_dir_plugin(parent);
	if (dplug == NULL || !dplug->resolve_into_inode /*lookup */ ) {
		REISER4_EXIT_PTR(ERR_PTR(-ENOTDIR));
	}

	/* call its lookup method */
	retval = dplug->resolve_into_inode(parent, dentry);
	if (retval == 0) {
		struct inode *obj;
		file_plugin *fplug;

		obj = dentry->d_inode;
		assert("nikita-2645", obj != NULL);
		fplug = inode_file_plugin(obj);
		retval = fplug->bind(obj, parent);
	} else if (retval == -ENOENT)
		/* object not found */
		d_add(dentry, NULL);

	if ((retval == 0) || (retval == -ENOENT))
		/* success */
		result = NULL;
	else
		result = ERR_PTR(retval);
	REISER4_EXIT_PTR(result);
}

/** ->create() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int
reiser4_create(struct inode *parent	/* inode of parent
					 * directory */ ,
	       struct dentry *dentry	/* dentry of new object to
					 * create */ ,
	       int mode /* new object mode */ )
{
	reiser4_object_create_data data;

	data.mode = S_IFREG | mode;
	data.id = REGULAR_FILE_PLUGIN_ID;
	return invoke_create_method(parent, dentry, &data);
}

/** ->mkdir() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int
reiser4_mkdir(struct inode *parent	/* inode of parent
					 * directory */ ,
	      struct dentry *dentry	/* dentry of new object to
					 * create */ ,
	      int mode /* new object's mode */ )
{
	reiser4_object_create_data data;

	data.mode = S_IFDIR | mode;
	data.id = DIRECTORY_FILE_PLUGIN_ID;
	return invoke_create_method(parent, dentry, &data);
}

/** ->symlink() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int
reiser4_symlink(struct inode *parent	/* inode of parent
					 * directory */ ,
		struct dentry *dentry	/* dentry of new object to
					 * create */ ,
		const char *linkname	/* pathname to put into
					 * symlink */ )
{
	reiser4_object_create_data data;

	data.name = linkname;
	data.id = SYMLINK_FILE_PLUGIN_ID;
	data.mode = S_IFLNK | S_IRWXUGO;
	return invoke_create_method(parent, dentry, &data);
}

/** ->mknod() VFS method in reiser4 inode_operations */
/* Audited by: umka (2002.06.12) */
static int
reiser4_mknod(struct inode *parent /* inode of parent directory */ ,
	      struct dentry *dentry	/* dentry of new object to
					 * create */ ,
	      int mode /* new object's mode */ ,
	      int rdev /* minor and major of new device node */ )
{
	reiser4_object_create_data data;

	data.mode = mode;
	data.rdev = rdev;
	data.id = SPECIAL_FILE_PLUGIN_ID;
	return invoke_create_method(parent, dentry, &data);
}

/** ->rename() inode operation */
static int
reiser4_rename(struct inode *old_dir, struct dentry *old, struct inode *new_dir, struct dentry *new)
{
	int result;
	REISER4_ENTRY(old_dir->i_sb);

	assert("nikita-2314", old_dir != NULL);
	assert("nikita-2315", old != NULL);
	assert("nikita-2316", new_dir != NULL);
	assert("nikita-2317", new != NULL);

	result = perm_chk(old_dir, rename, old_dir, old, new_dir, new);
	if (result == 0) {
		dir_plugin *dplug;

		dplug = inode_dir_plugin(old_dir);
		assert("nikita-2271", dplug != NULL);
		if (dplug->rename != NULL)
			result = dplug->rename(old_dir, old, new_dir, new);
		else
			result = -EPERM;
	}
	REISER4_EXIT(result);
}

static int
reiser4_readlink(struct dentry *dentry, char *buf, int buflen)
{
	assert("vs-852", S_ISLNK(dentry->d_inode->i_mode));
	if (!dentry->d_inode->u.generic_ip || !inode_get_flag(dentry->d_inode, REISER4_GENERIC_VP_USED))
		return -EINVAL;
	return vfs_readlink(dentry, buf, buflen, dentry->d_inode->u.generic_ip);
}

static int
reiser4_follow_link(struct dentry *dentry, struct nameidata *data)
{
	assert("vs-851", S_ISLNK(dentry->d_inode->i_mode));

	if (!dentry->d_inode->u.generic_ip || !inode_get_flag(dentry->d_inode, REISER4_GENERIC_VP_USED))
		return -EINVAL;
	return vfs_follow_link(data, dentry->d_inode->u.generic_ip);
}

/*
 * ->setattr() inode operation
 *
 * Called from notify_change.
 */
static int
reiser4_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int result;
	REISER4_ENTRY(inode->i_sb);

	assert("nikita-2269", attr != NULL);

	result = perm_chk(inode, setattr, dentry, attr);
	if (result == 0) {
		file_plugin *fplug;

		fplug = inode_file_plugin(inode);
		assert("nikita-2271", fplug != NULL);
		assert("nikita-2296", fplug->setattr != NULL);
		result = fplug->setattr(inode, attr);
	}
	REISER4_EXIT(result);
}

/**
 * ->getattr() inode operation called (indirectly) by sys_stat().
 */
static int
reiser4_getattr(struct vfsmount *mnt UNUSED_ARG, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int result;
	REISER4_ENTRY(inode->i_sb);

	result = perm_chk(inode, getattr, mnt, dentry, stat);
	if (result == 0) {
		file_plugin *fplug;

		fplug = inode_file_plugin(inode);
		assert("nikita-2295", fplug != NULL);
		assert("nikita-2297", fplug->getattr != NULL);
		result = fplug->getattr(mnt, dentry, stat);
	}
	REISER4_EXIT(result);
}

/** ->read() VFS method in reiser4 file_operations */
static ssize_t
reiser4_read(struct file *file /* file to read from */ ,
	     char *buf		/* user-space buffer to put data read
				 * from the file */ ,
	     size_t size /* bytes to read */ ,
	     loff_t * off	/* offset to start reading from. This
				 * is updated to indicate actual
				 * number of bytes read */ )
{
	file_plugin *fplug;
	ssize_t result;

	REISER4_ENTRY(file->f_dentry->d_inode->i_sb);

	assert("umka-072", file != NULL);
	assert("umka-073", buf != NULL);
	assert("umka-074", off != NULL);

	reiser4_stat_vfs_calls_add(reads);

	trace_on(TRACE_VFS_OPS,
		 "READ: (i_ino %li, size %lld): %u bytes from pos %lli\n",
		 file->f_dentry->d_inode->i_ino, file->f_dentry->d_inode->i_size, size, *off);

	fplug = inode_file_plugin(file->f_dentry->d_inode);
	assert("nikita-417", fplug != NULL);

	if (fplug->read == NULL) {
		result = -EPERM;
	} else {
		result = fplug->read(file, buf, size, off);
	}

	REISER4_EXIT(result);
}

/** ->write() VFS method in reiser4 file_operations */
static ssize_t
reiser4_write(struct file *file /* file to write on */ ,
	      const char *buf	/* user-space buffer to get data
				 * to write on the file */ ,
	      size_t size /* bytes to write */ ,
	      loff_t * off	/* offset to start writing
				 * from. This is updated to indicate
				 * actual number of bytes written */ )
{
	file_plugin *fplug;
	struct inode *inode;
	ssize_t result;

	REISER4_ENTRY((inode = file->f_dentry->d_inode)->i_sb);

	assert("nikita-1421", file != NULL);
	assert("nikita-1422", buf != NULL);
	assert("nikita-1424", off != NULL);

	reiser4_stat_vfs_calls_add(writes);

	trace_on(TRACE_VFS_OPS,
		 "WRITE: (i_ino %li, size %lld): %u bytes to pos %lli\n", inode->i_ino, inode->i_size, size, *off);

	if (size != 0) {
		down(&inode->i_sem);
		fplug = inode_file_plugin(inode);
		if (fplug->write != NULL)
			result = fplug->write(file, buf, size, off);
		else
			result = -EPERM;
		up(&inode->i_sem);
	} else
		result = 0;
	REISER4_EXIT(result);
}

/** ->truncate() VFS method in reiser4 inode_operations */
static void
reiser4_truncate(struct inode *inode /* inode to truncate */ )
{
	__REISER4_ENTRY(inode->i_sb,);

	assert("umka-075", inode != NULL);

	trace_on(TRACE_VFS_OPS, "TRUNCATE: i_ino %li to size %lli\n", inode->i_ino, inode->i_size);

	truncate_object(inode, inode->i_size);
	/* 
	 * for mysterious reasons ->truncate() VFS call doesn't return
	 * value 
	 */

	__REISER4_EXIT(&__context);
}

/** return number of files in a filesystem. It is used in reiser4_statfs to
 * fill ->f_ffiles */
/* Audited by: umka (2002.06.12) */
static long
oids_used(struct super_block *s	/* super block of file system in
				 * queried */ )
{
	oid_allocator_plugin *oplug;
	__u64 used;

	assert("umka-076", s != NULL);
	assert("vs-484", get_super_private(s));

	oplug = get_super_private(s)->oid_plug;
	if (!oplug || !oplug->oids_used)
		return (long) -1;

	used = oplug->oids_used(&get_super_private(s)->oid_allocator);
	if (used < (__u64) ((long) ~0) >> 1)
		return (long) used;
	else
		return (long) -1;
}

/** number of oids available for use by users. It is used in reiser4_statfs to
 * fill ->f_ffree */
/* Audited by: umka (2002.06.12) */
static long
oids_free(struct super_block *s	/* super block of file system in
				 * queried */ )
{
	oid_allocator_plugin *oplug;
	__u64 used;

	assert("umka-077", s != NULL);
	assert("vs-485", get_super_private(s));

	oplug = get_super_private(s)->oid_plug;
	if (!oplug || !oplug->oids_free)
		return (long) -1;

	used = oplug->oids_free(&get_super_private(s)->oid_allocator);
	if (used < (__u64) ((long) ~0) >> 1)
		return (long) used;
	else
		return (long) -1;
}

/** ->statfs() VFS method in reiser4 super_operations */
/* Audited by: umka (2002.06.12) */
static int
reiser4_statfs(struct super_block *super	/* super block of file
						 * system in queried */ ,
	       struct statfs *buf	/* buffer to fill with
					 * statistics */ )
{
	long bfree;
	
	REISER4_ENTRY(super);

	assert("nikita-408", super != NULL);
	assert("nikita-409", buf != NULL);

	buf->f_type = statfs_type(super);
	buf->f_bsize = super->s_blocksize;

	/* UMKA: At Green's propose we do not show the real block count on
	 * the device. */
	buf->f_blocks = reiser4_block_count(super) - reiser4_fs_reserved_space(super);

	/* UMKA: We should do not show the reserved space */
        bfree = (long)(reiser4_free_blocks(super) > reiser4_fs_reserved_space(super) ?
		reiser4_free_blocks(super) - reiser4_fs_reserved_space(super) : 0);
	    
	buf->f_bfree = bfree;
	
	buf->f_bavail = buf->f_bfree - reiser4_reserved_blocks(super, 0, 0);
	buf->f_files = oids_used(super);
	buf->f_ffree = oids_free(super);

	/* maximal acceptable name length depends on directory plugin. */
	buf->f_namelen = -1;
	REISER4_EXIT(0);
}

/*
 * address space operations
 */

/** ->writepage() VFS method in reiser4 address_space_operations */
static int
reiser4_writepage(struct page *page)
{
	int result;
	file_plugin *fplug;
	struct writeback_control wbc;
	REISER4_ENTRY(page->mapping->host->i_sb);

	trace_on(TRACE_VFS_OPS, "WRITEPAGE: (i_ino %li, page index %lu)\n", page->mapping->host->i_ino, page->index);

	fplug = inode_file_plugin(page->mapping->host);
	if (fplug->writepage != NULL)
		result = fplug->writepage(page);
	else
		result = -EINVAL;
	if (result != 0) {
		SetPageError(page);
		reiser4_unlock_page(page);
		REISER4_EXIT(result);
	}

	xmemset(&wbc, 0, sizeof wbc);
	wbc.nr_to_write = 1;

	/* The mpage_writepages() calls reiser4_writepage with a locked, but
	 * clean page.  An extra reference should protect this page from
	 * removing from memory */
	page_cache_get(page);
	result = page_common_writeback(page, &wbc, JNODE_FLUSH_MEMORY_UNFORMATTED);
	page_cache_release(page);
	REISER4_EXIT(result);
}

/** ->readpage() VFS method in reiser4 address_space_operations */
static int
reiser4_readpage(struct file *f /* file to read from */ ,
		 struct page *page	/* page where to read data
					 * into */ )
{
	struct inode *inode;
	file_plugin *fplug;
	int result;
	REISER4_ENTRY(f->f_dentry->d_inode->i_sb);

	assert("umka-078", f != NULL);
	assert("umka-079", page != NULL);
	assert("nikita-2280", PageLocked(page));
	assert("vs-976", !PageUptodate(page));

	assert("vs-318", page->mapping && page->mapping->host);
	assert("nikita-1352", (f == NULL) || (f->f_dentry->d_inode == page->mapping->host));

	/*
	 * ->readpage can be called from page fault service routine
	 */
	ON_DEBUG_CONTEXT(assert("nikita-2661", !lock_counters()->spin_locked));

	inode = page->mapping->host;
	fplug = inode_file_plugin(inode);
	if (fplug->readpage != NULL)
		result = fplug->readpage(f, page);
	else
		result = -EINVAL;
	if (result != 0) {
		SetPageError(page);
		reiser4_unlock_page(page);
	}
	REISER4_EXIT(0);
}

/* nikita-fixme-hans: comment all functions and their parameters */
static int
reiser4_vm_writeback(struct page *page, struct writeback_control *wbc)
{
	return page_common_writeback(page, wbc, JNODE_FLUSH_MEMORY_UNFORMATTED);
}

/* 
   ->writepages()
   ->vm_writeback()
   ->set_page_dirty()
   ->readpages()
   ->prepare_write()
   ->commit_write()
*/

/* ->bmap() VFS method in reiser4 address_space_operations */
static sector_t
reiser4_bmap(struct address_space *mapping, sector_t block)
{
	file_plugin *fplug;
	REISER4_ENTRY(mapping->host->i_sb);

	fplug = inode_file_plugin(mapping->host);
	if (!fplug || !fplug->get_block) {
		return -EINVAL;
	}

	REISER4_EXIT(generic_block_bmap(mapping, block, fplug->get_block));
}

/* ->invalidatepage()
   ->releasepage()
*/

/* 
 * FIXME-VS: 
 * for some reasons we are not satisfied with address space's readpages() method.
 *
 * Andrew Morton says:
 * Probably, we make do_page_cache_readahead an a_op.  It's a pretty small
 * function, so the fs can take a copy and massage it to suit.
 * We'll have to get that a_op to pass back to page_cache_readahead() the
 * start/nr_pages which it actually did start I/O against, so the readahead
 * logic can adjust its state.
 *
 * So, if/when this method will be added - the below is reiser4's
 * implementation
 * @start_page - number of page to start readahead from
 * @intrafile_readahead_amount - number of pages to issue i/o for
 * return value: number of pages for which i/o is started
 */
int
reiser4_do_page_cache_readahead(struct file *file, unsigned long start_page, unsigned long intrafile_readahead_amount)
{
	int result = 0;
	struct inode *inode;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	file_plugin *fplug;
	item_plugin *iplug;
	unsigned long last_page, cur_page;

	assert("vs-754", file && file->f_dentry && file->f_dentry->d_inode);
	inode = file->f_dentry->d_inode;
	if (inode->i_size == 0)
		return 0;

	coord_init_zero(&coord);
	init_lh(&lh);

	/* next page after last one */
	last_page = ((inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT);
	if (start_page + intrafile_readahead_amount > last_page)
		/* do not read past current file size */
		intrafile_readahead_amount = last_page - start_page;

	/* make sure that we can calculate a key by inode and offset we want to
	 * read from */
	fplug = inode_file_plugin(inode);
	assert("vs-755", fplug && fplug->key_by_inode);

	cur_page = start_page;
	while (intrafile_readahead_amount) {
		/* calc key of next page to readahead */
		fplug->key_by_inode(inode, (loff_t) cur_page << PAGE_CACHE_SHIFT, &key);

		/*
		 * FIXME-ME: seal might be used to find_next_item
		 */
		result = find_next_item(0, &key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
		if (result != CBK_COORD_FOUND) {
			break;
		}

		result = zload(coord.node);
		if (result) {
			break;
		}

		iplug = item_plugin_by_coord(&coord);
		if (!iplug->s.file.page_cache_readahead) {
			result = -EINVAL;
			zrelse(coord.node);
			break;
		}
		/* item's readahead returns number of pages for which readahead
		 * is started */
		result = iplug->s.file.page_cache_readahead(file, &coord, &lh, cur_page, intrafile_readahead_amount);
		zrelse(coord.node);
		if (result <= 0) {
			break;
		}
		assert("vs-794", (unsigned long) result <= intrafile_readahead_amount);
		intrafile_readahead_amount -= result;
		cur_page += result;
	}
	done_lh(&lh);
	return result <= 0 ? result : (int) (cur_page - start_page);
}

/**
 * ->link() VFS method in reiser4 inode_operations
 *
 * entry point for ->link() method.
 *
 * This is installed as ->link inode operation for reiser4
 * inodes. Delegates all work to object plugin
 */
/* Audited by: umka (2002.06.12) */
static int
reiser4_link(struct dentry *existing	/* dentry of existing
					 * object */ ,
	     struct inode *parent /* parent directory */ ,
	     struct dentry *where /* new name for @existing */ )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY(parent->i_sb);

	assert("umka-080", existing != NULL);
	assert("nikita-1031", parent != NULL);

	dplug = inode_dir_plugin(parent);
	assert("nikita-1430", dplug != NULL);
	if (dplug->link != NULL) {
		result = dplug->link(parent, existing, where);
		if (result == 0) {
			d_instantiate(where, existing->d_inode);
		}
	} else {
		result = -EPERM;
	}
	REISER4_EXIT(result);
}

static loff_t
reiser4_llseek(struct file *file, loff_t off, int origin)
{
	loff_t result;
	file_plugin *fplug;
	struct inode *inode = file->f_dentry->d_inode;
	loff_t(*seek_fn) (struct file *, loff_t, int);
	REISER4_ENTRY(inode->i_sb);

	trace_on(TRACE_VFS_OPS,
		 "llseek: (i_ino %li, size %lld): off %lli, origin %d\n", inode->i_ino, inode->i_size, off, origin);

	fplug = inode_file_plugin(inode);
	assert("nikita-2291", fplug != NULL);
	seek_fn = fplug->seek ? : default_llseek;
	result = seek_fn(file, off, origin);
	REISER4_EXIT(result);
}

typedef struct readdir_actor_args {
	void *dirent;
	filldir_t filldir;
	struct file *dir;
	__u64 skip;
	__u64 skipped;
	reiser4_key key;
} readdir_actor_args;

/**
 * reiser4_readdir() - our readdir() method.
 *
 * readdir(2)/getdents(2) interface is based on implicit assumption that
 * readdir can be restarted from any particular point by supplying file
 * system with off_t-full of data. That is, file system fill ->d_off
 * field in struct dirent and later user passes ->d_off to the
 * seekdir(3), which is, actually, implemented by glibc as lseek(2) on
 * directory.
 *
 * Reiser4 cannot restart readdir from 64 bits of data, because two last
 * components of the key of directory entry are unknown, which given 128
 * bits: locality and type fields in the key of directory entry are
 * always known, to start readdir() from given point objectid and offset
 * fields have to be filled.
 *
 */
static int
reiser4_readdir(struct file *f /* directory file being read */ ,
		void *dirent /* opaque data passed to us by VFS */ ,
		filldir_t filldir	/* filler function passed to us
					 * by VFS */ )
{
	dir_plugin *dplug;
	int result;
	struct inode *inode = f->f_dentry->d_inode;

	REISER4_ENTRY(inode->i_sb);

	dplug = inode_dir_plugin(inode);
	if ((dplug != NULL) && (dplug->readdir != NULL))
		result = dplug->readdir(f, dirent, filldir);
	else
		result = -ENOTDIR;

	UPDATE_ATIME(inode);
	REISER4_EXIT(result);

}

/** ->mmap() VFS method in reiser4 file_operations */
static int
reiser4_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode;
	int result;
	REISER4_ENTRY(file->f_dentry->d_inode->i_sb);

	trace_on(TRACE_VFS_OPS, "MMAP: (i_ino %li, size %lld)\n",
		 file->f_dentry->d_inode->i_ino, file->f_dentry->d_inode->i_size);

	inode = file->f_dentry->d_inode;
	if (inode_file_plugin(inode)->mmap == NULL)
		result = -ENOSYS;
	else
		result = inode_file_plugin(inode)->mmap(file, vma);
	REISER4_EXIT(result);
}

static int
unlink_file(struct inode *parent /* parent directory */ ,
	    struct dentry *victim	/* name of object being
					 * unlinked */ )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY(parent->i_sb);

	assert("nikita-1435", parent != NULL);
	assert("nikita-1436", victim != NULL);

	trace_on(TRACE_DIR | TRACE_VFS_OPS, "unlink: %li/%s\n", (long) parent->i_ino, victim->d_name.name);

	dplug = inode_dir_plugin(parent);
	assert("nikita-1429", dplug != NULL);
	if (dplug->unlink != NULL)
		result = dplug->unlink(parent, victim);
	else
		result = -EPERM;
	/* 
	 * @victim can be already removed from the disk by this time. Inode is
	 * then marked so that iput() wouldn't try to remove stat data. But
	 * inode itself is still there.
	 */
	REISER4_EXIT(result);
}

/** 
 * ->unlink() VFS method in reiser4 inode_operations
 *
 * remove link from @parent directory to @victim object: delegate work
 * to object plugin
 */
/* Audited by: umka (2002.06.12) */
static int
reiser4_unlink(struct inode *parent /* parent directory */ ,
	       struct dentry *victim	/* name of object being
					 * unlinked */ )
{
	assert("nikita-2011", parent != NULL);
	assert("nikita-2012", victim != NULL);
	assert("nikita-2013", victim->d_inode != NULL);
	if (inode_dir_plugin(victim->d_inode) == NULL)
		return unlink_file(parent, victim);
	else
		return -EISDIR;
}

/** 
 * ->rmdir() VFS method in reiser4 inode_operations
 *
 * The same as unlink, but only for directories.
 *
 */
/* Audited by: umka (2002.06.12) */
static int
reiser4_rmdir(struct inode *parent /* parent directory */ ,
	      struct dentry *victim	/* name of directory being
					 * unlinked */ )
{
	assert("nikita-2014", parent != NULL);
	assert("nikita-2015", victim != NULL);
	assert("nikita-2016", victim->d_inode != NULL);

	if (inode_dir_plugin(victim->d_inode) != NULL)
		/* there is no difference between unlink and rmdir for
		 * reiser4 */
		return unlink_file(parent, victim);
	else
		return -ENOTDIR;
}

/** ->permission() method in reiser4_inode_operations. */
static int
reiser4_permission(struct inode *inode /* object */ ,
		   int mask	/* mode bits to check permissions
				 * for */ )
{
	int result;
	/*
	 * reiser4_context creation/destruction removed from here,
	 * because permission checks currently don't require this.
	 *
	 * Permission plugin have to create context itself if necessary.
	 */
	/* REISER4_ENTRY( inode -> i_sb ); */
	assert("nikita-1687", inode != NULL);

	result = perm_chk(inode, mask, inode, mask) ? -EACCES : 0;
	/* REISER4_EXIT( result ); */
	return result;
}

/**
 * update inode stat-data by calling plugin
 */
int
reiser4_write_sd(struct inode *object)
{
	file_plugin *fplug;

	assert("nikita-2338", object != NULL);

	if (IS_RDONLY(object))
		return 0;

	fplug = inode_file_plugin(object);
	assert("nikita-2339", fplug != NULL);
	return fplug->write_sd_by_inode(object);
}

/**
 * helper function: increase inode nlink count and call plugin method to save
 * updated stat-data.
 *
 * Used by link/create and during creation of dot and dotdot in mkdir
 */
int
reiser4_add_nlink(struct inode *object /* object to which link is added */ ,
		  struct inode *parent /* parent where new entry will be */ ,
		  int write_sd_p	/* true is stat-data has to be
					 * updated */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1351", object != NULL);

	fplug = inode_file_plugin(object);
	assert("nikita-1445", fplug != NULL);

	/* ask plugin whether it can add yet another link to this
	   object */
	if (!fplug->can_add_link(object)) {
		return -EMLINK;
	}

	assert("nikita-2211", fplug->add_link != NULL);
	/* call plugin to do actual addition of link */
	result = fplug->add_link(object, parent);
	if ((result == 0) && write_sd_p)
		result = fplug->write_sd_by_inode(object);
	return result;
}

/**
 * helper function: decrease inode nlink count and call plugin method to save
 * updated stat-data.
 *
 * Used by unlink/create
 */
int
reiser4_del_nlink(struct inode *object	/* object from which link is
					 * removed */ ,
		  struct inode *parent /* parent where entry was */ ,
		  int write_sd_p	/* true is stat-data has to be
					 * updated */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1349", object != NULL);

	fplug = inode_file_plugin(object);
	assert("nikita-1350", fplug != NULL);
	assert("nikita-1446", object->i_nlink > 0);
	assert("nikita-2210", fplug->rem_link != NULL);

	/* call plugin to do actual deletion of link */
	result = fplug->rem_link(object, parent);
	if ((result == 0) && write_sd_p)
		result = fplug->write_sd_by_inode(object);
	return result;
}

/** call ->create() directory plugin method. */
static int
invoke_create_method(struct inode *parent /* parent directory */ ,
		     struct dentry *dentry	/* dentry of new
						 * object */ ,
		     reiser4_object_create_data * data	/* information
							 * necessary
							 * to create
							 * new
							 * object */ )
{
	int result;
	dir_plugin *dplug;
	REISER4_ENTRY(parent->i_sb);

	assert("nikita-426", parent != NULL);
	assert("nikita-427", dentry != NULL);
	assert("nikita-428", data != NULL);

	dplug = inode_dir_plugin(parent);
	if (dplug == NULL)
		result = -ENOTDIR;
	else if (dplug->create_child != NULL) {
		struct inode *child;
		
		result = dplug->create_child(parent, dentry, data);
		child = dentry->d_inode;
		if (unlikely(result != 0)) {
			if (child != NULL) {
				dentry->d_inode = NULL;
				reiser4_make_bad_inode(child);
				iput(child);
			}
		} else {
			d_instantiate(dentry, child);
			trace_on(TRACE_VFS_OPS, "create: %s (%o) %llu\n",
				 dentry->d_name.name, data->mode, get_inode_oid(child));
		}
		
	} else
		result = -EPERM;

	REISER4_EXIT(result);
}

/** helper function: call object plugin to truncate file to @size */
int
truncate_object(struct inode *inode /* object to truncate */ ,
		loff_t size /* size to truncate object to */ )
{
	file_plugin *fplug;

	assert("nikita-1026", inode != NULL);
	assert("nikita-1027", is_reiser4_inode(inode));
	assert("nikita-1028", inode->i_sb != NULL);

	fplug = inode_file_plugin(inode);
	assert("vs-142", fplug != NULL);

	if (fplug->truncate != NULL) {
		int result;
		result = fplug->truncate(inode, size);
		if (result != 0) {
			warning("nikita-1602", "Truncate error: %i for %lli", result, get_inode_oid(inode));
		}
		return result;
	} else {
		return -EPERM;
	}
}

/** initial prefix of names of pseudo-files like ..plugin, ..acl,
    ..whatnot, ..and, ..his, ..dog 

    Reminder: This is an optional style convention, not a requirement.
    If anyone builds in any dependence in the parser or elsewhere on a
    prefix existing for all pseudo files, and thereby hampers creating
    pseudo-files without this prefix, I will be pissed.  -Hans */
static const char PSEUDO_FILES_PREFIX[] = "..";

/**
 * Return and lazily allocate if necessary per-dentry data that we
 * attach to each dentry.
 */
/* Audited by: umka (2002.06.12) */
reiser4_dentry_fsdata *
reiser4_get_dentry_fsdata(struct dentry *dentry	/* dentry
						 * queried */ )
{
	assert("nikita-1365", dentry != NULL);

	if (dentry->d_fsdata == NULL) {
		reiser4_stat_file_add(fsdata_alloc);
		/* FIXME-NIKITA use slab in stead */
		dentry->d_fsdata = reiser4_kmalloc(sizeof (reiser4_dentry_fsdata), GFP_KERNEL);
		if (dentry->d_fsdata == NULL)
			return ERR_PTR(-ENOMEM);
		xmemset(dentry->d_fsdata, 0, sizeof (reiser4_dentry_fsdata));
	}
	return dentry->d_fsdata;
}

void
reiser4_free_dentry_fsdata(struct dentry *dentry /* dentry released */ )
{
	if (dentry->d_fsdata != NULL)
		reiser4_kfree(dentry->d_fsdata, sizeof (reiser4_dentry_fsdata));
}

/** Release reiser4 dentry. This is d_op->d_delease() method. */
/* Audited by: umka (2002.06.12) */
static void
reiser4_d_release(struct dentry *dentry /* dentry released */ )
{
	__REISER4_ENTRY(dentry->d_sb,);
	reiser4_free_dentry_fsdata(dentry);
	__REISER4_EXIT(&__context);
}

/**
 * Return and lazily allocate if necessary per-file data that we attach
 * to each struct file.
 */
reiser4_file_fsdata *
reiser4_get_file_fsdata(struct file *f	/* file
					 * queried */ )
{
	assert("nikita-1603", f != NULL);

	if (f->private_data == NULL) {
		reiser4_file_fsdata *fsdata;
		reiser4_inode *info;

		reiser4_stat_file_add(private_data_alloc);
		/* FIXME-NIKITA use slab in stead */
		fsdata = reiser4_kmalloc(sizeof *fsdata, GFP_KERNEL);
		if (fsdata == NULL)
			return ERR_PTR(-ENOMEM);
		xmemset(fsdata, 0, sizeof *fsdata);

		info = reiser4_inode_data(f->f_dentry->d_inode);
		spin_lock(&info->guard);
		if (f->private_data == NULL) {
			fsdata->back = f;
			readdir_list_clean(fsdata);
			f->private_data = fsdata;
			fsdata = NULL;
		}
		spin_unlock(&info->guard);
		if (fsdata != NULL)
			/*
			 * other thread initialised ->fsdata
			 */
			reiser4_kfree(fsdata, sizeof *fsdata);
	}
	assert("nikita-2665", f->private_data != NULL);
	return f->private_data;
}

static const char *
tail_status(const struct inode *inode)
{
	if (!inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN))
		return "unknown";
	if (inode_get_flag(inode, REISER4_HAS_TAIL))
		return "tail";
	return "notail";
}

/** Release reiser4 file. This is f_op->release() method. Called when last
 * holder closes a file */
static int
reiser4_release(struct inode *i /* inode released */ ,
		struct file *f /* file released */ )
{
	file_plugin *fplug;
	int result;
	REISER4_ENTRY(i->i_sb);

	assert("umka-081", i != NULL);
	assert("nikita-1447", f != NULL);

	fplug = inode_file_plugin(i);
	assert("umka-082", fplug != NULL);

	trace_on(TRACE_VFS_OPS,
		 "RELEASE: (i_ino %li, size %lld, tail status: %s)\n", i->i_ino, i->i_size, tail_status(i));

	if (fplug->release)
		result = fplug->release(f);
	else
		result = 0;

	if (f->private_data != NULL)
		reiser4_kfree(f->private_data, sizeof (reiser4_file_fsdata));

	REISER4_EXIT(result);
}

/** our ->read_inode() is no-op. Reiser4 inodes should be loaded
    through fs/reiser4/inode.c:reiser4_iget() */
static void
noop_read_inode(struct inode *inode UNUSED_ARG)
{
}

/***************************************************
 * initialisation and shutdown
 ***************************************************/

/** slab cache for inodes */
static kmem_cache_t *inode_cache;

/**
 * initalisation function passed to the kmem_cache_create() to init new pages
 * grabbed by our inodecache.
 */
static void
init_once(void *obj /* pointer to new inode */ ,
	  kmem_cache_t * cache UNUSED_ARG /* slab cache */ ,
	  unsigned long flags /* cache flags */ )
{
	reiser4_inode_object *info;

	info = obj;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) == SLAB_CTOR_CONSTRUCTOR) {
		/*
		 * FIXME-NIKITA add here initialisations for locks, list
		 * heads, etc. that will be added to our private inode part.
		 */
		/*
		 * FIXME-NIKITA where inode is zeroed?
		 */
		inode_init_once(&info->vfs_inode);
		spin_lock_init(&info->p.guard);
		init_rwsem(&info->p.sem);
		readdir_list_init(&info->p.readdir_list);
	}
}

/** initialise slab cache where reiser4 inodes will live */
int
init_inodecache(void)
{
	inode_cache = kmem_cache_create("reiser4_inode_cache",
					sizeof (reiser4_inode_object), 0, SLAB_HWCACHE_ALIGN, init_once, NULL);
	return (inode_cache != NULL) ? 0 : -ENOMEM;
}

/** initialise slab cache where reiser4 inodes lived */
/* Audited by: umka (2002.06.12) */
static void
destroy_inodecache(void)
{
	if (kmem_cache_destroy(inode_cache) != 0)
		warning("nikita-1695", "not all inodes were freed");
}

/** ->alloc_inode() super operation: allocate new inode */
static struct inode *
reiser4_alloc_inode(struct super_block *super UNUSED_ARG	/* super block new
								 * inode is
								 * allocated for */ )
{
	reiser4_inode_object *obj;

	assert("nikita-1696", super != NULL);
	obj = kmem_cache_alloc(inode_cache, SLAB_KERNEL);
	if (obj != NULL) {
		reiser4_inode *info;

		info = &obj->p;

		info->flags = 0;
		info->file = NULL;
		info->dir = NULL;
		info->perm = NULL;
		info->tail = NULL;
		info->hash = NULL;
		info->sd = NULL;
		info->dir_item = NULL;
		info->extmask = 0ull;
		info->sd_len = 0;
		info->locality_id = 0ull;
		info->parent = NULL;
		info->plugin_mask = 0;
#if !REISER4_INO_IS_OID
		info->oid_hi = 0;
#endif
		seal_init(&info->sd_seal, NULL, NULL);
		coord_init_invalid(&info->sd_coord, NULL);
		xmemset(&info->ra, 0, sizeof info->ra);
		return &obj->vfs_inode;
	} else
		return NULL;
}

/** ->destroy_inode() super operation: recycle inode */
static void
reiser4_destroy_inode(struct inode *inode	/* inode being
						 * destroyed */ )
{
	if (!is_bad_inode(inode) && inode_get_flag(inode, REISER4_LOADED)) {
		if (inode_get_flag(inode, REISER4_GENERIC_VP_USED)) {
			assert("vs-839", S_ISLNK(inode->i_mode));
			reiser4_kfree_in_sb(inode->u.generic_ip, (size_t) inode->i_size + 1, inode->i_sb);
			inode->u.generic_ip = 0;
			inode_clr_flag(inode, REISER4_GENERIC_VP_USED);
		}
	}
	kmem_cache_free(inode_cache, reiser4_inode_data(inode));
}

/** ->dirty_inode() super operation */
static void
reiser4_dirty_inode(struct inode *inode)
{
	int result;
	__REISER4_ENTRY(inode->i_sb,);

	/*
	 * FIXME-NIKITA: VFS expects ->dirty_inode() to be relatively
	 * cheap. For example, each quota call invokes it. Our dirty inode
	 * updates stat-data in the tree. Per-inode seals probably alleviate
	 * this significantly, but still.
	 *
	 * One possible solution is to attach inodes to the transaction atoms
	 * and only update them just before transaction commit. This, as a
	 * byproduct will amortize multiple inode updates per transaction.
	 */

	assert("nikita-2523", inode != NULL);
	if (reiser4_grab_space_exact(1, 1))
		goto no_space;
	
	trace_on(TRACE_RESERVE, "ditry inode grabs 1 block.\n");

	result = reiser4_write_sd(inode);
	if (result != 0)
		warning("nikita-2524", "Failed to write sd of %llu: %i", get_inode_oid(inode), result);
no_space:
	__REISER4_EXIT(&__context);
}

extern void generic_drop_inode(struct inode *object);

static void
reiser4_drop_inode(struct inode *object)
{
	file_plugin *fplug;

	assert("nikita-2643", object != NULL);

	/*
	 * -not- creating context in this method, because it is frequently
	 * called and all existing ->not_linked() methods are one liners.
	 */

	fplug = inode_file_plugin(object);
	if ((fplug != NULL) && fplug->not_linked(object)) {
		/*
		 * create context here.
		 *
		 * removal of inode from the hash table (done at the very
		 * beginning of generic_delete_inode(), truncate of pages, and
		 * removal of file's extents has to be performed in the same
		 * atom. Otherwise, it may so happen, that twig node with
		 * allocated extent will be flushed to the disk.
		 */
		__REISER4_ENTRY(object->i_sb,);

		object->i_nlink = 0;
		generic_delete_inode(object);
		__REISER4_EXIT(&__context);
	} else
		generic_forget_inode(object);
}

/** ->delete_inode() super operation */
static void
reiser4_delete_inode(struct inode *object)
{
	__REISER4_ENTRY(object->i_sb,);

	if (inode_get_flag(object, REISER4_LOADED)) {
		file_plugin *fplug;
		dir_plugin *dplug;
		reiser4_block_nr reserved = 0;

		fplug = inode_file_plugin(object);
		dplug = inode_dir_plugin(object);
		
		if (dplug != NULL)
			reserved += dplug->estimate.done(reiser4_inode_data(object)->parent, object);

		if (fplug != NULL)
			reserved += 
				fplug->estimate.truncate ? fplug->estimate.truncate(object, 0) : 0 + 
				fplug->estimate.delete(object); 
		
		if (reiser4_grab_space_exact(reserved, 1))
			goto no_space;
		
		trace_on(TRACE_RESERVE, "delete inode grabs %llu blocks.\n", reserved);
		
		get_exclusive_access(object);
		truncate_object(object, (loff_t) 0);
		drop_exclusive_access(object);

		if (dplug != NULL)
			dplug->done(object);

		if ((fplug != NULL) && (fplug->delete != NULL))
			fplug->delete(object);
	}
no_space:
	object->i_blocks = 0;
	clear_inode(object);
	__REISER4_EXIT(&__context);
}

/* Audited by: umka (2002.06.12) */
const char *REISER4_SUPER_MAGIC_STRING = "R4Sb";
const int REISER4_MAGIC_OFFSET = 16 * 4096;	/* offset to magic string from the
						 * beginning of device */

/** type of option parseable by parse_option() */
typedef enum {
	/** value of option is arbitrary string */
	OPT_STRING,
	/** option specifies bit in a bitmask */
	OPT_BIT,
	/** value of option should conform to sprintf() format */
	OPT_FORMAT,
	/** option can take one of predefined values */
	OPT_ONEOF,
	/** option specifies reiser4 plugin */
	OPT_PLUGIN
} opt_type_t;

typedef struct opt_bitmask_bit {
	const char *bit_name;
	int bit_nr;
} opt_bitmask_bit;

/** description of option parseable by parse_option() */
typedef struct opt_desc {
	/** 
	 * option name.
	 * 
	 * parsed portion of string has a form "name=value".
	 */
	const char *name;
	/** type of option */
	opt_type_t type;
	union {
		/** where to store value of string option (type == OPT_STRING) */
		char **string;
		/** description of bits for bit option (type == OPT_BIT) */
		struct {
			int nr;
			void *addr;
		} bit;
		/** 
		 * description of format and targets for format option (type
		 * == OPT_FORMAT)
		 */
		struct {
			const char *format;
			int nr_args;
			void *arg1;
			void *arg2;
			void *arg3;
			void *arg4;
		} f;
		struct {
			/* NOT YET */
		} oneof;
		/** description of plugin option */
		struct {
			reiser4_plugin **addr;
			const char *type_label;
		} plugin;
		struct {
			void *addr;
			int nr_bits;
			opt_bitmask_bit *bits;
		} bitmask;
	} u;
} opt_desc_t;

/**
 * parse one option
 */
static int
parse_option(char *opt_string /* starting point of parsing */ ,
	     opt_desc_t * opt /* option description */ )
{
	/* 
	 * foo=bar, 
	 * ^   ^  ^
	 * |   |  +-- replaced to '\0'
	 * |   +-- val_start
	 * +-- opt_string
	 */
	char *val_start;
	int result;
	const char *err_msg;

	/*
	 * FIXME-NIKITA think about using lib/cmdline.c functions here.
	 */

	val_start = strchr(opt_string, '=');
	if (val_start != NULL) {
		*val_start = '\0';
		++val_start;
	}

	err_msg = NULL;
	result = 0;
	switch (opt->type) {
	case OPT_STRING:
		if (val_start == NULL) {
			err_msg = "String arg missing";
			result = -EINVAL;
		} else
			*opt->u.string = val_start;
		break;
	case OPT_BIT:
		if (val_start != NULL)
			err_msg = "Value ignored";
		else
			set_bit(opt->u.bit.nr, opt->u.bit.addr);
		break;
	case OPT_FORMAT:
		if (val_start == NULL) {
			err_msg = "Formatted arg missing";
			result = -EINVAL;
			break;
		}
		if (sscanf(val_start, opt->u.f.format,
			   opt->u.f.arg1, opt->u.f.arg2, opt->u.f.arg3, opt->u.f.arg4) != opt->u.f.nr_args) {
			err_msg = "Wrong conversion";
			result = -EINVAL;
		}
		break;
	case OPT_ONEOF:
		not_implemented("nikita-2099", "Oneof");
		break;
	case OPT_PLUGIN:{
			reiser4_plugin *plug;

			if (*opt->u.plugin.addr != NULL) {
				err_msg = "Plugin already set";
				result = -EINVAL;
				break;
			}

			plug = lookup_plugin(opt->u.plugin.type_label, val_start);
			if (plug != NULL)
				*opt->u.plugin.addr = plug;
			else {
				err_msg = "Wrong plugin";
				result = -EINVAL;
			}
			break;
		}
	default:
		wrong_return_value("nikita-2100", "opt -> type");
		break;
	}
	if (err_msg != NULL) {
		warning("nikita-2496", "%s when parsing option \"%s%s%s\"",
			err_msg, opt->name, val_start ? "=" : "", val_start ? : "");
	}
	return result;
}

/** parse options */
static int
parse_options(char *opt_string /* starting point */ ,
	      opt_desc_t * opts /* array with option description */ ,
	      int nr_opts /* number of elements in @opts */ )
{
	int result;

	result = 0;
	while ((result == 0) && opt_string && *opt_string) {
		int j;
		char *next;

		next = strchr(opt_string, ',');
		if (next != NULL) {
			*next = '\0';
			++next;
		}
		for (j = 0; j < nr_opts; ++j) {
			if (!strncmp(opt_string, opts[j].name, strlen(opts[j].name))) {
				result = parse_option(opt_string, &opts[j]);
				break;
			}
		}
		if (j == nr_opts) {
			warning("nikita-2307", "Unrecognized option: \"%s\"", opt_string);
			/* 
			 * traditionally, -EINVAL is returned on wrong mount
			 * option
			 */
			result = -EINVAL;
		}
		opt_string = next;
	}
	return result;
}

#define NUM_OPT( label, fmt, addr )				\
		{						\
			.name = ( label ),			\
			.type = OPT_FORMAT,			\
			.u = {					\
				.f = {				\
					.format  = ( fmt ),	\
					.nr_args = 1,		\
					.arg1 = ( addr ),	\
					.arg2 = NULL,		\
					.arg3 = NULL,		\
					.arg4 = NULL		\
				}				\
			}					\
		}

#define SB_FIELD_OPT( field, fmt ) NUM_OPT( #field, fmt, &info -> field )

#define PLUG_OPT( label, ptype, plug )					\
	{								\
		.name = ( label ),					\
		.type = OPT_PLUGIN,					\
		.u = {							\
			.plugin = {					\
				.type_label = #ptype,			\
				.addr = ( reiser4_plugin ** )( plug )	\
			}						\
		}							\
	}

static int
reiser4_parse_options(struct super_block *s, char *opt_string)
{
	int result;
	reiser4_super_info_data *info = get_super_private(s);
	char *trace_file_name;

	opt_desc_t opts[] = {
		/*
		 * trace_flags=N
		 *
		 * set trace flags to be N for this mount. N can be C numeric
		 * literal recognized by %i scanf specifier.  It is treated as
		 * bitfield filled by values of debug.h:reiser4_trace_flags
		 * enum
		 */
		SB_FIELD_OPT(trace_flags, "%i"),
		/*
		 * debug_flags=N
		 *
		 * set debug flags to be N for this mount. N can be C numeric
		 * literal recognized by %i scanf specifier.  It is treated as
		 * bitfield filled by values of debug.h:reiser4_debug_flags
		 * enum
		 */
		SB_FIELD_OPT(debug_flags, "%i"),
		/*
		 * txnmgr.atom_max_size=N
		 *
		 * Atoms containing more than N blocks will be forced to
		 * commit. N is decimal.
		 */
		SB_FIELD_OPT(txnmgr.atom_max_size, "%u"),
		/*
		 * txnmgr.atom_max_age=N
		 *
		 * Atoms older than N seconds will be forced to commit. N is
		 * decimal.
		 */
		SB_FIELD_OPT(txnmgr.atom_max_age, "%u"),
		/*
		 * txnmgr.low_memory=N
		 *
		 * After percentage of free memory falls below this,
		 * preventive flushing is started.
		 */
		SB_FIELD_OPT(txnmgr.low_memory, "%u"),
		/*
		 * tree.cbk_cache_slots=N
		 *
		 * Number of slots in the cbk cache.
		 */
		SB_FIELD_OPT(tree.cbk_cache.nr_slots, "%u"),

		/* If flush finds more than FLUSH_RELOCATE_THRESHOLD adjacent
		 * dirty leaf-level blocks it will force them to be
		 * relocated. */
		SB_FIELD_OPT(flush.relocate_threshold, "%u"),
		/* If flush finds can find a block allocation closer than at
		 * most FLUSH_RELOCATE_DISTANCE from the preceder it will
		 * relocate to that position. */
		SB_FIELD_OPT(flush.relocate_distance, "%u"),
		/* Flush defers actualy BIO submission until it gathers
		 * FLUSH_QUEUE_SIZE blocks. */
		SB_FIELD_OPT(flush.queue_size, "%u"),
		/* If we have written this much or more blocks before
		   encountering busy jnode in flush list - abort flushing
		   hoping that next time we get called this jnode will be
		   clean already, and we will save some seeks. */
		SB_FIELD_OPT(flush.written_threshold, "%u"),
		/*
		 * The maximum number of nodes to scan left on a level during
		 * flush.
		 */
		SB_FIELD_OPT(flush.scan_maxnodes, "%u"),

		/*
		 * preferred IO size
		 */
		SB_FIELD_OPT(optimal_io_size, "%u"),

		/* carry flags used for insertion of new nodes */
		SB_FIELD_OPT(tree.carry.new_node_flags, "%u"),
		/* carry flags used for insertion of new extents */
		SB_FIELD_OPT(tree.carry.new_extent_flags, "%u"),
		/* carry flags used for paste operations */
		SB_FIELD_OPT(tree.carry.paste_flags, "%u"),
		/* carry flags used for insert operations */
		SB_FIELD_OPT(tree.carry.insert_flags, "%u"),

		PLUG_OPT("plugin.tail", tail, &info->plug.t),
		PLUG_OPT("plugin.sd", item, &info->plug.sd),
		PLUG_OPT("plugin.dir_item", item, &info->plug.dir_item),
		PLUG_OPT("plugin.perm", perm, &info->plug.p),
		PLUG_OPT("plugin.file", file, &info->plug.f),
		PLUG_OPT("plugin.dir", dir, &info->plug.d),
		PLUG_OPT("plugin.hash", hash, &info->plug.h),

#if REISER4_BSD_PORT
		{
		 /*
		  * turn on BSD-style gid assignment
		  */
		 .name = "bsdgroups",
		 .type = OPT_BIT,
		 .u = {
		       .bit = {
			       .nr = REISER4_BSD_GID,
			       .addr = &info->fs_flags}
		       }
		 },
#endif

#if REISER4_TRACE_TREE
		{
		 .name = "trace_file",
		 .type = OPT_STRING,
		 .u = {
		       .string = &trace_file_name}
		 }
#endif
	};

	info->txnmgr.atom_max_size = REISER4_ATOM_MAX_SIZE;
	info->txnmgr.atom_max_age = REISER4_ATOM_MAX_AGE / HZ;
	info->txnmgr.low_memory = REISER4_LOW_MEMORY;

	info->tree.cbk_cache.nr_slots = CBK_CACHE_SLOTS;

	info->flush.relocate_threshold = FLUSH_RELOCATE_THRESHOLD;
	info->flush.relocate_distance = FLUSH_RELOCATE_DISTANCE;
	info->flush.queue_size = FLUSH_QUEUE_SIZE;
	info->flush.written_threshold = FLUSH_WRITTEN_THRESHOLD;
	info->flush.scan_maxnodes = FLUSH_SCAN_MAXNODES;

	if (info->plug.t == NULL)
		info->plug.t = tail_plugin_by_id(REISER4_TAIL_PLUGIN);
	if (info->plug.sd == NULL)
		info->plug.sd = item_plugin_by_id(REISER4_SD_PLUGIN);
	if (info->plug.dir_item == NULL)
		info->plug.dir_item = item_plugin_by_id(REISER4_DIR_ITEM_PLUGIN);
	if (info->plug.p == NULL)
		info->plug.p = perm_plugin_by_id(REISER4_PERM_PLUGIN);
	if (info->plug.f == NULL)
		info->plug.f = file_plugin_by_id(REISER4_FILE_PLUGIN);
	if (info->plug.d == NULL)
		info->plug.d = dir_plugin_by_id(REISER4_DIR_PLUGIN);
	if (info->plug.h == NULL)
		info->plug.h = hash_plugin_by_id(REISER4_HASH_PLUGIN);

	info->optimal_io_size = REISER4_OPTIMAL_IO_SIZE;

	info->tree.carry.new_node_flags = REISER4_NEW_NODE_FLAGS;
	info->tree.carry.new_extent_flags = REISER4_NEW_EXTENT_FLAGS;
	info->tree.carry.paste_flags = REISER4_PASTE_FLAGS;
	info->tree.carry.insert_flags = REISER4_INSERT_FLAGS;

	trace_file_name = NULL;

	result = parse_options(opt_string, opts, sizeof_array(opts));
	if (result != 0)
		return result;

	info->txnmgr.atom_max_age *= HZ;
	if (info->txnmgr.atom_max_age <= 0)
		/* overflow */
		info->txnmgr.atom_max_age = REISER4_ATOM_MAX_AGE;

	if (info->txnmgr.low_memory > 100)
		info->txnmgr.low_memory = 100;

	/* round optimal io size up to 512 bytes */
	info->optimal_io_size >>= VFS_BLKSIZE_BITS;
	info->optimal_io_size <<= VFS_BLKSIZE_BITS;
	if (info->optimal_io_size == 0) {
		warning("nikita-2497", "optimal_io_size is too small");
		return -EINVAL;
	}
#if REISER4_TRACE_TREE
	if (trace_file_name != NULL)
		result = open_trace_file(s, trace_file_name, REISER4_TRACE_BUF_SIZE, &info->trace_file);
	else
		info->trace_file.type = log_to_bucket;
#endif
	return result;
}

static int
reiser4_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct super_block *super;
	reiser4_super_info_data *info;

	super = mnt->mnt_sb;
	info = get_super_private(super);

	seq_printf(m, ",trace=0x%x", info->trace_flags);
	seq_printf(m, ",debug=0x%x", info->debug_flags);
	seq_printf(m, ",atom_max_size=0x%x", info->txnmgr.atom_max_size);

	seq_printf(m, ",default plugins: file=\"%s\"", default_file_plugin(super)->h.label);
	seq_printf(m, ",dir=\"%s\"", default_dir_plugin(super)->h.label);
	seq_printf(m, ",hash=\"%s\"", default_hash_plugin(super)->h.label);
	seq_printf(m, ",tail=\"%s\"", default_tail_plugin(super)->h.label);
	seq_printf(m, ",perm=\"%s\"", default_perm_plugin(super)->h.label);
	seq_printf(m, ",dir_item=\"%s\"", default_dir_item_plugin(super)->h.label);
	seq_printf(m, ",sd=\"%s\"", default_sd_plugin(super)->h.label);
	return 0;
}

extern ktxnmgrd_context kdaemon;

/* There are some 'committed' versions of reiser4 super block counters, which
 * correspond to reiser4 on-disk state. These counters are initialized here */
static void
init_committed_sb_counters(const struct super_block *s)
{
	reiser4_super_info_data *private = get_super_private(s);

	private->blocks_free_committed = private->blocks_free;
	private->nr_files_committed = oid_used();
}

/**
 * read super block from device and fill remaining fields in @s.
 *
 * This is read_super() of the past.  
 */
static int
reiser4_fill_super(struct super_block *s, void *data, int silent UNUSED_ARG)
{
	struct buffer_head *super_bh;
	struct reiser4_master_sb *master_sb;
	reiser4_super_info_data *info;
	int plugin_id;
	disk_format_plugin *df_plug;
	struct inode *inode;
	int result;
	unsigned long blocksize;
	reiser4_context __context;

	assert("umka-085", s != NULL);

	if ((REISER4_DEBUG || REISER4_DEBUG_MODIFY || REISER4_TRACE ||
	     REISER4_STATS || REISER4_DEBUG_MEMCPY || REISER4_ZERO_NEW_NODE || REISER4_TRACE_TREE) && !silent)
		warning("nikita-2372", "Debugging is on. Benchmarking is invalid.");

	/* this is common for every disk layout. It has a pointer where layout
	 * specific part of info can be attached to, though */
	info = kmalloc(sizeof (reiser4_super_info_data), GFP_KERNEL);

	if (!info)
		return -ENOMEM;

	s->s_fs_info = info;
	memset(info, 0, sizeof (*info));
	ON_DEBUG(INIT_LIST_HEAD(&info->all_jnodes));

	result = init_context(&__context, s);
	if (result) {
		kfree(info);
		s->s_fs_info = NULL;
		return result;
	}

read_super_block:
	/* look for reiser4 magic at hardcoded place */
	super_bh = sb_bread(s, (int) (REISER4_MAGIC_OFFSET / s->s_blocksize));
	if (!super_bh) {
		result = -EIO;
		goto error1;
	}

	master_sb = (struct reiser4_master_sb *) super_bh->b_data;
	/* check reiser4 magic string */
	result = -EINVAL;
	if (!strncmp(master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4)) {
		/* reset block size if it is not a right one FIXME-VS: better comment is needed */
		blocksize = d16tocpu(&master_sb->blocksize);

		if (blocksize != PAGE_CACHE_SIZE) {
			if (!silent)
				warning("nikita-2609", "%s: wrong block size %ld\n", s->s_id, blocksize);
			brelse(super_bh);
			goto error1;
		}
		if (blocksize != s->s_blocksize) {
			brelse(super_bh);
			if (!sb_set_blocksize(s, (int) blocksize)) {
				goto error1;
			}
			goto read_super_block;
		}

		plugin_id = d16tocpu(&master_sb->disk_plugin_id);
		/* only two plugins are available for now */
		assert("vs-476", (plugin_id == FORMAT40_ID || plugin_id == TEST_FORMAT_ID));
		df_plug = disk_format_plugin_by_id(plugin_id);
		brelse(super_bh);
	} else {
		if (!silent)
			warning("nikita-2608", "Wrong magic: %x != %x",
				*(__u32 *) master_sb->magic, *(__u32 *) REISER4_SUPER_MAGIC_STRING);
		/* no standard reiser4 super block found */
		brelse(super_bh);
		/* FIXME-VS: call guess method for all available layout
		 * plugins */
		/* 
		 * umka (2002.06.12) Is it possible when format-specific super
		 * block exists but there no master super block?
		 */
		goto error1;
	}

	s->s_op = &reiser4_super_operations;

	spin_lock_init(&info->guard);

	/* init layout plugin */
	info->df_plug = df_plug;

	txnmgr_init(&info->tmgr);

	result = ktxnmgrd_attach(&kdaemon, &info->tmgr);
	if (result) {
		goto error2;
	}

	/* initialize fake inode, formatted nodes will be read/written through
	 * it */
	result = init_formatted_fake(s);
	if (result) {
		goto error2;
	}

	/* call disk format plugin method to do all the preparations like
	 * journal replay, reiser4_super_info_data initialization, read oid
	 * allocator, etc */
	result = df_plug->get_ready(s, data);
	if (result) {
		goto error3;
	}

	init_committed_sb_counters(s);

	assert("nikita-2687", check_block_counters(s));

	/*
	 * FIXME-NIKITA actually, options should be parsed by plugins also.
	 */
	result = reiser4_parse_options(s, data);
	if (result) {
		goto error4;
	}

	result = cbk_cache_init(&info->tree.cbk_cache);
	if (result) {
		goto error4;
	}

	inode = reiser4_iget(s, df_plug->root_dir_key(s));
	if (IS_ERR(inode)) {
		result = PTR_ERR(inode);
		goto error4;
	}
	/* allocate dentry for root inode, It works with inode == 0 */
	s->s_root = d_alloc_root(inode);
	if (!s->s_root) {
		result = -ENOMEM;
		goto error4;
	}
	s->s_root->d_op = &reiser4_dentry_operation;

	if (inode->i_state & I_NEW) {
		reiser4_inode *info;

		info = reiser4_inode_data(inode);

		if (info->file == NULL)
			info->file = default_file_plugin(s);
		if (info->dir == NULL)
			info->dir = default_dir_plugin(s);
		if (info->sd == NULL)
			info->sd = default_sd_plugin(s);
		if (info->hash == NULL)
			info->hash = default_hash_plugin(s);
		if (info->tail == NULL)
			info->tail = default_tail_plugin(s);
		if (info->perm == NULL)
			info->perm = default_perm_plugin(s);
		if (info->dir_item == NULL)
			info->dir_item = default_dir_item_plugin(s);
		assert("nikita-1951", info->file != NULL);
		assert("nikita-1814", info->dir != NULL);
		assert("nikita-1815", info->sd != NULL);
		assert("nikita-1816", info->hash != NULL);
		assert("nikita-1817", info->tail != NULL);
		assert("nikita-1818", info->perm != NULL);
		assert("vs-545", info->dir_item != NULL);
		unlock_new_inode(inode);
	}

	if (!silent)
		print_fs_info("mount ok", s);
	REISER4_EXIT(0);

error4:
	get_super_private(s)->df_plug->release(s);
error3:
	done_formatted_fake(s);
	/* shutdown daemon */
	ktxnmgrd_detach(&info->tmgr);
error2:
	txnmgr_done(&info->tmgr);
error1:
	kfree(info);
	s->s_fs_info = NULL;

	__context.trans = NULL;
	done_context(&__context);
	return result;
}

static void
reiser4_kill_super(struct super_block *s)
{
	reiser4_super_info_data *info;
	reiser4_context context;

	info = (reiser4_super_info_data *) s->s_fs_info;
	if (!info) {
		/* mount failed */
		s->s_op = 0;
		kill_block_super(s);
		return;
	}

	if (init_context(&context, s)) {
		warning("nikita-2728", "Cannot initialize context.");
		return;
	}
	trace_on(TRACE_VFS_OPS, "kill_super\n");

	/*
	 * FIXME-VS: the problem is that there still might be dirty pages which
	 * became dirty via mapping. Have them to go through writepage
	 */
	fsync_super(s);

	if (reiser4_is_debugged(s, REISER4_VERBOSE_UMOUNT)) {
		get_current_context()->trace_flags |= (TRACE_PCACHE |
						       TRACE_TXN |
						       TRACE_FLUSH | TRACE_ZNODES | TRACE_IO_R | TRACE_IO_W);
	}

	/* flushes transactions, etc. */
	if (get_super_private(s)->df_plug->release(s) != 0)
		goto out;

	/* shutdown daemon if last mount is removed */
	ktxnmgrd_detach(&info->tmgr);

	check_block_counters(s);
	done_formatted_fake(s);

	close_trace_file(&info->trace_file);

	/*
	 * we don't want ->write_super to be called any more.
	 */
	s->s_op->write_super = NULL;
	kill_block_super(s);

#if REISER4_DEBUG
	{
		struct list_head *scan;

		/*
		 * print jnodes that survived umount.
		 */
		list_for_each(scan, &info->all_jnodes) {
			jnode *busy;

			busy = list_entry(scan, jnode, jnodes);
			info_jnode("\nafter umount", busy);
		}
	}
	if (info->kmalloc_allocated > 0)
		warning("nikita-2622", "%i bytes still allocated", info->kmalloc_allocated);
#endif

	if (reiser4_is_debugged(s, REISER4_STATS_ON_UMOUNT))
		reiser4_print_stats();

out:
	/* no assertions below this line */
	__REISER4_EXIT(&context);

	kfree(info);
	s->s_fs_info = NULL;
}

static void
reiser4_write_super(struct super_block *s)
{
	int ret;
	__REISER4_ENTRY(s,);

	/* FIXME: JMACD->NIKITA: Are we sure this is right?  I don't remember putting this
	 * here. */
	if ((ret = txnmgr_force_commit_all(s))) {
		warning("jmacd-77113", "txn_force failed in write_super: %d", ret);
	}

	/* Oleg says do this: */
	s->s_dirt = 0;

	__REISER4_EXIT(&__context);
}

/** ->get_sb() method of file_system operations. */
/* Audited by: umka (2002.06.12) */
static struct super_block *
reiser4_get_sb(struct file_system_type *fs_type	/* file
						 * system
						 * type */ ,
	       int flags /* flags */ ,
	       char *dev_name /* device name */ ,
	       void *data /* mount options */ )
{
	return get_sb_bdev(fs_type, flags, dev_name, data, reiser4_fill_super);
}

/** ->invalidatepage method for reiser4 */
int
reiser4_invalidatepage(struct page *page, unsigned long offset)
{
	int ret = 0;
	REISER4_ENTRY(page->mapping->host->i_sb);

	if (offset == 0) {
		jnode *node;

		/*
		 * ->private pointer is protected by page lock that we are
		 * holding right now.
		 */
again:
		node = jnode_by_page(page);
		if (node != NULL) {
			jref(node);
try_to_lock:
			spin_lock_jnode(node);
			reiser4_unlock_page(page);

			ret = try_capture(node, ZNODE_WRITE_LOCK, 0);

			if (!ret) {
				/* If node is flush queued (i.e. prepard to
				 * write to disk) we are trying to submit flush
				 * queues to disk one by one and wait i/o
				 * completion. We repeat it until our node
				 * becomes not queued. */
				if (JF_ISSET(node, JNODE_FLUSH_QUEUED)) {
					txn_atom *atom;
					int nr_io_errors;

					atom = atom_get_locked_by_jnode(node);
					spin_unlock_jnode(node);

					assert("zam-x1070", atom != NULL);

					/*
					 * FIXME-VS: we call finish_all_fq
					 * because currently it is not possible
					 * to find which flush queue the node
					 * is on
					 */
					nr_io_errors = 0;
					ret = finish_all_fq(atom, &nr_io_errors);

					if (ret) {
						if (ret == -EBUSY) {
							/* All flush queues are
							 * busy we have to wait
							 * an atom event and
							 * rescan atom's list
							 * for not busy flush
							 * queues. */
							atom_wait_event(atom);

							jput(node);
							reiser4_lock_page(page);
							goto again;
						}

						if (ret != -EAGAIN)
							reiser4_panic
							    ("zam-x1071: cannot flush queued node (ret = %d)\n", ret);
						/* -EAGAIN means we have to
						 * repeat. Repeating of
						 * finish_all_fq() may be not
						 * needed because the node we
						 * wanted to write is already
						 * written to disk and removed
						 * from a flush queue */

					} else {
						spin_unlock_atom(atom);
					}

					jput(node);
					reiser4_lock_page(page);
					goto again;
				}
			}

			spin_unlock_jnode(node);
			/*
			 * return with page still
			 * locked. truncate_list_pages() expects this.
			 */
			reiser4_lock_page(page);
			assert("nikita-2676", jprivate(page) == node);
			if (ret) {
				warning("green-20", "try_capture returned %d", ret);
				if ((ret == -EAGAIN) || (ret == -EDEADLK) || (ret == -EINTR)) {
					preempt_point();
					goto try_to_lock;
				}
			} else {
				uncapture_page(page);

				UNDER_SPIN_VOID(jnode, node, page_clear_jnode(page, node));
				JF_SET(node, JNODE_HEARD_BANSHEE);
			}
			/*
			 * can safely call jput() under page lock, because
			 * page was just detached from jnode.
			 */
			jput(node);
		}
	}
	REISER4_EXIT(ret);
}

/** ->releasepage method for reiser4 */
int
reiser4_releasepage(struct page *page, int gfp UNUSED_ARG)
{
	jnode *node;

	assert("nikita-2257", PagePrivate(page));
	assert("nikita-2259", PageLocked(page));
	ON_DEBUG_CONTEXT(assert("nikita-2586", lock_counters()->spin_locked == 0));

	/*
	 * FIXME-NIKITA: this can be called in the context of reiser4 call. It
	 * is not clear what to do in this case. A lot of deadlocks seems be
	 * possible.
	 */

	node = jnode_by_page(page);
	assert("nikita-2258", node != NULL);

#if REISER4_STATS
	++get_super_private(page->mapping->host->i_sb)->stats.level[jnode_get_level(node) -
								    LEAF_LEVEL].page_try_release;
#endif

	/*
	 * is_page_cache_freeable() check 
	 *
	 * (mapping + private + page_cache_get() by shrink_cache())
	 */
	if (page_count(page) > 3)
		return 0;
	if (PageDirty(page))
		return 0;

	spin_lock_jnode(node);
	/*
	 * can only release page if real block number is assigned to
	 * it. Simple check for ->atom wouldn't do, because it is possible for
	 * node to be clean, not it atom yet, and still having fake block
	 * number. For example, node just created in jinit_new().
	 */
	if (atomic_read(&node->d_count) || jnode_is_loaded(node) ||
	    blocknr_is_fake(jnode_get_block(node)) || jnode_is_dirty(node) || JF_ISSET(node, JNODE_OVRWR)
	    || JF_ISSET(node, JNODE_WRITEBACK)) {
		spin_unlock_jnode(node);
		return 0;
	}

	{
		reiser4_tree *tree = tree_by_page(page);
		REISER4_ENTRY(tree->super);

		/* account for spin_lock_jnode() above */
		if (REISER4_DEBUG && get_current_context() == &__context) {
			ON_DEBUG_CONTEXT(++lock_counters()->spin_locked_jnode);
			ON_DEBUG_CONTEXT(++lock_counters()->spin_locked);
		}

		jref(node);
		page_clear_jnode(page, node);
		spin_unlock_jnode(node);

		reiser4_stat_add_at_level(jnode_get_level(node), page_released);
		/*
		 * we are under memory pressure so release jnode also.
		 */

		jput(node);
		/*
		 * return with page still locked. shrink_cache() expects this.
		 */
		REISER4_EXIT(1);
	}
}

/* reiser4 writepages() address space operation */
int
reiser4_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int ret = 0;
	struct super_block *s = mapping->host->i_sb;
	struct backing_dev_info *bdi = mapping->backing_dev_info;

	REISER4_ENTRY(s);

	assert("zam-760", lock_stack_isclean(get_current_lock_stack()));
	/* Here we can call synchronously. We can be called from
	 * balance_dirty_pages() Reiser4 code is supposed to call
	 * balance_dirty_pages at paces where no locks are hold it means we can
	 * call begin jnode_flush right from there having no deadlocks between the
	 * caller of balance_dirty_pages() and jnode_flush(). */

	while (wbc->nr_to_write > 0) {
		long nr_submitted = 0;

		/* do not put more requests to overload write queue */
		if (wbc->nonblocking && bdi_write_congested(bdi)) {
			blk_run_queues();
			wbc->encountered_congestion = 1;
			break;
		}

		ret = flush_some_atom(&nr_submitted, JNODE_FLUSH_WRITE_BLOCKS);

		if (!nr_submitted)
			break;

		wbc->nr_to_write -= nr_submitted;
	}

	REISER4_EXIT(ret);
}

typedef enum {
	INIT_NONE,
	INIT_INODECACHE,
	INIT_CONTEXT_MGR,
	INIT_ZNODES,
	INIT_PLUGINS,
	INIT_TXN,
	INIT_FAKES,
	INIT_JNODES,
	INIT_FS_REGISTERED
} reiser4_init_stage;

static reiser4_init_stage init_stage;

/**
 * finish with reiser4: this is called either at shutdown or at module unload.
 */
static void __exit
done_reiser4(void)
{
#define DONE_IF( stage, exp )			\
	if( init_stage == ( stage ) ) {		\
		exp;				\
		-- init_stage;			\
	}

	DONE_IF(INIT_FS_REGISTERED, unregister_filesystem(&reiser4_fs_type));
	DONE_IF(INIT_JNODES, jnode_done_static());
	DONE_IF(INIT_FAKES,;);
	DONE_IF(INIT_TXN, txnmgr_done_static());
	DONE_IF(INIT_PLUGINS,;);
	DONE_IF(INIT_ZNODES, znodes_done());
	DONE_IF(INIT_CONTEXT_MGR,;);
	DONE_IF(INIT_INODECACHE, destroy_inodecache());
	assert("nikita-2516", init_stage == INIT_NONE);

#undef DONE_IF
}

#if 0
static int
test(struct notifier_block *self, unsigned long event, void *arg)
{
	struct low_mem_info *info;

	info = arg;

	info("low_mem: %lu, zone: %s, prio: %i, scanned: %i\n", event, info->zone->name, info->priority, info->scanned);
	return NOTIFY_DONE;
}

static struct notifier_block low_mem_test = {
	.notifier_call = test,
	.next = NULL,
	.priority = 1
};
#endif

/**
 * initialise reiser4: this is called either at bootup or at module load.
 */
static int __init
init_reiser4(void)
{
#define CHECK_INIT_RESULT( exp )		\
({						\
	result = exp;				\
	if( result == 0 )			\
		++ init_stage;			\
	else {					\
		done_reiser4();			\
		return result;			\
	}					\
})

	int result;

	info(KERN_INFO "Loading Reiser4. " "See www.namesys.com for a description of Reiser4.\n");
	init_stage = INIT_NONE;

	CHECK_INIT_RESULT(init_inodecache());
	CHECK_INIT_RESULT(init_context_mgr());
	CHECK_INIT_RESULT(znodes_init());
	CHECK_INIT_RESULT(init_plugins());
	CHECK_INIT_RESULT(txnmgr_init_static());
	CHECK_INIT_RESULT(init_fakes());
	CHECK_INIT_RESULT(jnode_init_static());
	CHECK_INIT_RESULT(register_filesystem(&reiser4_fs_type));

	assert("nikita-2515", init_stage == INIT_FS_REGISTERED);
	// notifier_chain_register( &low_mem_chain, &low_mem_test );
	return 0;
#undef CHECK_INIT_RESULT
}

module_init(init_reiser4);
module_exit(done_reiser4);

MODULE_DESCRIPTION("Reiser4 filesystem");
MODULE_AUTHOR("Hans Reiser <Reiser@Namesys.COM>");

/*
 * FIXME-NIKITA is this correct?
 */
MODULE_LICENSE("GPL");

/**
 * description of the reiser4 file system type in the VFS eyes.
 */
static struct file_system_type reiser4_fs_type = {
	.owner = THIS_MODULE,
	.name = "reiser4",
	.get_sb = reiser4_get_sb,
	.kill_sb = reiser4_kill_super,

	/*
	 * FIXME-NIKITA something more?
	 */
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL
};

struct inode_operations reiser4_inode_operations = {
	.create = reiser4_create,	/* d */
	.lookup = reiser4_lookup,	/* d */
	.link = reiser4_link,	/* d */
	.unlink = reiser4_unlink,	/* d */
	.symlink = reiser4_symlink,	/* d */
	.mkdir = reiser4_mkdir,	/* d */
	.rmdir = reiser4_rmdir,	/* d */
	.mknod = reiser4_mknod,	/* d */
	.rename = reiser4_rename,	/* d */
	.readlink = NULL,
	.follow_link = NULL,
	.truncate = reiser4_truncate,	/* d */
	.permission = reiser4_permission,	/* d */
	.setattr = reiser4_setattr,	/* d */
	.getattr = reiser4_getattr,	/* d */
/*	.setxattr    = reiser4_setxattr, */
/* 	.getxattr    = reiser4_getxattr, */
/* 	.listxattr   = reiser4_listxattr, */
/* 	.removexattr = reiser4_removexattr */
};

struct file_operations reiser4_file_operations = {
	.llseek = reiser4_llseek,	/* d */
	.read = reiser4_read,	/* d */
	.write = reiser4_write,	/* d */
	.readdir = reiser4_readdir,	/* d */
/* 	.poll              = reiser4_poll, */
	.ioctl = reiser4_ioctl,
	.mmap = reiser4_mmap,	/* d */
/* 	.open              = reiser4_open, */
/* 	.flush             = reiser4_flush, */
	.release = reiser4_release,	/* d */
/* 	.fsync             = reiser4_fsync, */
/* 	.fasync            = reiser4_fasync, */
/* 	.lock              = reiser4_lock, */
/* 	.readv             = reiser4_readv, */
/* 	.writev            = reiser4_writev, */
/* 	.sendpage          = reiser4_sendpage, */
/* 	.get_unmapped_area = reiser4_get_unmapped_area */
};

struct inode_operations reiser4_symlink_inode_operations = {
	.readlink = reiser4_readlink,
	.follow_link = reiser4_follow_link
};

define_never_ever_op(prepare_write_vfs)
    define_never_ever_op(commit_write_vfs)
    define_never_ever_op(direct_IO_vfs)
#define V( func ) ( ( void * ) ( func ) )
struct address_space_operations reiser4_as_operations = {
	/** called from write_one_page(). Not sure how this is to be used. */
	.writepage = reiser4_writepage,
	/** 
	 * called to read page from the storage when page is added into page
	 * cache 
	 */
	.readpage = reiser4_readpage,
	/**
	 * This is most annoyingly misnomered method. Actually it is called
	 * from wait_on_page_bit() and lock_page() and its purpose is to
	 * actually start io by jabbing device drivers.
	 */
	.sync_page = block_sync_page,
	/** called during sync (pdflush) */
	.writepages = reiser4_writepages,
	/** called during memory pressure by kswapd */
	.vm_writeback = reiser4_vm_writeback,
	.set_page_dirty = __set_page_dirty_nobuffers,
	/** called during read-ahead */
	.readpages = NULL,
	.prepare_write = V(never_ever_prepare_write_vfs),
	.commit_write = V(never_ever_commit_write_vfs),
	.bmap = reiser4_bmap,
	/**
	 * called just before page is taken out from address space (on
	 * truncate, umount, or similar). 
	 */
	.invalidatepage = reiser4_invalidatepage,
	/**
	 * called when VM is about to take page from address space (due to
	 * memory pressure).
	 */
	.releasepage = reiser4_releasepage,
	.direct_IO = V(never_ever_direct_IO_vfs)
};

struct super_operations reiser4_super_operations = {
	.alloc_inode = reiser4_alloc_inode,	/* d */
	.destroy_inode = reiser4_destroy_inode,	/* d */
	.read_inode = noop_read_inode,	/* d */
	.dirty_inode = reiser4_dirty_inode,	/* d */
/* 	.write_inode        = reiser4_write_inode, */
/* 	.put_inode          = reiser4_put_inode, */
	.drop_inode = reiser4_drop_inode,	/* d */
	.delete_inode = reiser4_delete_inode,	/* d */
	.put_super = NULL /* d */ ,
	.write_super = reiser4_write_super,
/* 	.write_super_lockfs = reiser4_write_super_lockfs, */
/* 	.unlockfs           = reiser4_unlockfs, */
	.statfs = reiser4_statfs,	/* d */
/* 	.remount_fs         = reiser4_remount_fs, */
/* 	.clear_inode        = reiser4_clear_inode, */
/* 	.umount_begin       = reiser4_umount_begin,*/
/* 	.fh_to_dentry       = reiser4_fh_to_dentry, */
/* 	.dentry_to_fh       = reiser4_dentry_to_fh */
	.show_options = reiser4_show_options	/* d */
};

struct dentry_operations reiser4_dentry_operation = {
	.d_revalidate = NULL,
	.d_hash = NULL,
	.d_compare = NULL,
	.d_delete = NULL,
	.d_release = reiser4_d_release,
	.d_iput = NULL,
};

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
