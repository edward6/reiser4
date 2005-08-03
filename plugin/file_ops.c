/* Copyright 2005 by Hans Reiser, licensing governed by
   reiser4/README */

/* this file contains typical implementations for some of methods of
   struct file_operations and of struct address_space_operations
*/

#include "../inode.h"
#include "object.h"

/* file operations */

/* implementation of vfs's llseek method of struct file_operations for
   typical directory can be found in readdir_common.c
*/
loff_t llseek_common_dir(struct file *, loff_t, int origin);

/* implementation of vfs's readdir method of struct file_operations for
   typical directory can be found in readdir_common.c
*/
int readdir_common(struct file *, void *dirent, filldir_t);

/* this is implementation of vfs's release method of struct file_operations for
   typical directory
*/
int release_dir_common(struct inode *inode, struct file *file)
{
	/* this is called when directory file descriptor is closed. */
	spin_lock_inode(inode);
	/* remove directory from readddir list. See comment before
	 * readdir_common() for details. */
	if (file->private_data != NULL)
		readdir_list_remove_clean(reiser4_get_file_fsdata(file));
	spin_unlock_inode(inode);
	return 0;
}

/* this is common implementation of vfs's fsync method of struct
   file_operations
*/
int sync_common(struct file *file, struct dentry *dentry, int datasync)
{
	reiser4_context *ctx;
	int result;

	ctx = init_context(dentry->d_inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	result = txnmgr_force_commit_all(dentry->d_inode->i_sb, 0);

	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);
	return result;
}

/* this is common implementation of vfs's sendfile method of struct
   file_operations

   Reads @count bytes from @file and calls @actor for every page read. This is
   needed for loop back devices support.
*/
ssize_t
sendfile_common(struct file *file, loff_t *ppos, size_t count,
		read_actor_t actor, void *target)
{
	reiser4_context *ctx;
	ssize_t result;

	ctx = init_context(file->f_dentry->d_inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	result = do_sendfile(file, ppos, count, actor, target);
	reiser4_exit_context(ctx);
	return result;
}

/* this is helper for sendfile_common and sendfile_unix_file
 */
ssize_t
do_sendfile(struct file *file, loff_t *ppos, size_t count,
	    read_actor_t actor, void *target)
{
	struct inode *inode;
	read_descriptor_t desc;
	struct page *page = NULL;
	int ret = 0;

	assert("umka-3108", file != NULL);

	inode = file->f_dentry->d_inode;

	desc.error = 0;
	desc.written = 0;
	desc.arg.data = target;
	desc.count = count;

	if (inode->i_mapping->a_ops->readpage)
		return RETERR(-EINVAL);

	while (desc.count != 0) {
		unsigned long read_request_size;
		unsigned long index;
		unsigned long offset;
		loff_t file_size = i_size_read(inode);

		if (*ppos >= file_size)
			break;

		index = *ppos >> PAGE_CACHE_SHIFT;
		offset = *ppos & ~PAGE_CACHE_MASK;

		page_cache_readahead(inode->i_mapping, &file->f_ra, file,
				     offset,
				     (file_size + PAGE_CACHE_SIZE -
				      1) >> PAGE_CACHE_SHIFT);

		/* determine valid read request size. */
		read_request_size = PAGE_CACHE_SIZE - offset;
		if (read_request_size > desc.count)
			read_request_size = desc.count;
		if (*ppos + read_request_size >= file_size) {
			read_request_size = file_size - *ppos;
			if (read_request_size == 0)
				break;
		}
		page = grab_cache_page(inode->i_mapping, index);
		if (unlikely(page == NULL)) {
			desc.error = RETERR(-ENOMEM);
			break;
		}

		if (PageUptodate(page))
			/* process locked, up-to-date  page by read actor */
			goto actor;

		ret = inode->i_mapping->a_ops->readpage(file, page);
		if (ret != 0) {
			SetPageError(page);
			ClearPageUptodate(page);
			desc.error = ret;
			goto fail_locked_page;
		}

		lock_page(page);
		if (!PageUptodate(page)) {
			desc.error = RETERR(-EIO);
			goto fail_locked_page;
		}

	      actor:
		ret = actor(&desc, page, offset, read_request_size);
		unlock_page(page);
		page_cache_release(page);

		(*ppos) += ret;

		if (ret != read_request_size)
			break;
	}

	if (0) {
	      fail_locked_page:
		unlock_page(page);
		page_cache_release(page);
	}

	update_atime(inode);

	if (desc.written)
		return desc.written;
	return desc.error;
}

/* address space operations */

/* this is common implementation of vfs's prepare_write method of struct
   address_space_operations
*/
int
prepare_write_common(struct file *file, struct page *page, unsigned from,
		     unsigned to)
{
	reiser4_context *ctx;
	int result;

	ctx = init_context(page->mapping->host->i_sb);
	result = do_prepare_write(file, page, from, to);

	/* don't commit transaction under inode semaphore */
	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);

	return result;
}

/* this is helper for prepare_write_common and prepare_write_unix_file
 */
int
do_prepare_write(struct file *file, struct page *page, unsigned from,
		 unsigned to)
{
	int result;
	file_plugin *fplug;
	struct inode *inode;

	assert("umka-3099", file != NULL);
	assert("umka-3100", page != NULL);
	assert("umka-3095", PageLocked(page));

	if (to - from == PAGE_CACHE_SIZE || PageUptodate(page))
		return 0;

	inode = page->mapping->host;
	fplug = inode_file_plugin(inode);

	if (page->mapping->a_ops->readpage == NULL)
		return RETERR(-EINVAL);

	result = page->mapping->a_ops->readpage(file, page);
	if (result != 0) {
		SetPageError(page);
		ClearPageUptodate(page);
		/* All reiser4 readpage() implementations should return the
		 * page locked in case of error. */
		assert("nikita-3472", PageLocked(page));
	} else {
		/*
		 * ->readpage() either:
		 *
		 *     1. starts IO against @page. @page is locked for IO in
		 *     this case.
		 *
		 *     2. doesn't start IO. @page is unlocked.
		 *
		 * In either case, page should be locked.
		 */
		lock_page(page);
		/*
		 * IO (if any) is completed at this point. Check for IO
		 * errors.
		 */
		if (!PageUptodate(page))
			result = RETERR(-EIO);
	}
	assert("umka-3098", PageLocked(page));
	return result;
}
