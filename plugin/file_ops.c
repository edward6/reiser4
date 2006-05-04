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

/**
 * release_dir_common - release of struct file_operations
 * @inode: inode of released file
 * @file: file to release
 *
 * Implementation of release method of struct file_operations for typical
 * directory. All it does is freeing of reiser4 specific file data.
*/
int release_dir_common(struct inode *inode, struct file *file)
{
	reiser4_context *ctx;

	ctx = init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	reiser4_free_file_fsdata(file);
	reiser4_exit_context(ctx);
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
#if 0
ssize_t
sendfile_common(struct file *file, loff_t *ppos, size_t count,
		read_actor_t actor, void *target)
{
	reiser4_context *ctx;
	ssize_t result;

	ctx = init_context(file->f_dentry->d_inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	result = generic_file_sendfile(file, ppos, count, actor, target);
	reiser4_exit_context(ctx);
	return result;
}
#endif  /*  0  */

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

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
