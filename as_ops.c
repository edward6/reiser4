/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to VFS. Reiser4 address_space_operations are defined here. */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/file/file.h"
#include "plugin/security/perm.h"
#include "plugin/disk_format/disk_format.h"
#include "plugin/plugin.h"
#include "plugin/plugin_set.h"
#include "plugin/plugin_hash.h"
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
#include "kattr.h"
#include "entd.h"
#include "emergency_flush.h"

#include <linux/profile.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
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
#include <linux/quotaops.h>
#include <linux/security.h>

/* address space operations */

static int reiser4_readpage(struct file *, struct page *);
/* static int reiser4_prepare_write(struct file *,
				 struct page *, unsigned, unsigned);
static int reiser4_commit_write(struct file *,
				struct page *, unsigned, unsigned);
*/
static int reiser4_set_page_dirty (struct page *);
sector_t reiser4_bmap(struct address_space *, sector_t);
/* static int reiser4_direct_IO(int, struct inode *,
			     struct kiobuf *, unsigned long, int); */

/* address space operations */

/* as_ops->set_page_dirty() VFS method in reiser4_address_space_operations.

   It is used by others (except reiser4) to set reiser4 pages dirty. Reiser4
   itself uses set_page_dirty_internal().

   The difference is that reiser4_set_page_dirty puts dirty page on
   reiser4_inode->moved_pages.  That list is processed by reiser4_writepages()
   to do reiser4 specific work over dirty pages (allocation jnode, capturing,
   atom creation) which cannot be done in the contexts where set_page_dirty is
   called.

   Mostly this function is __set_page_dirty_nobuffers() but target page list
   differs.
*/
static int reiser4_set_page_dirty (struct page * page /* page to mark dirty */)
{
	int ret = 0;

	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			spin_lock(&mapping->page_lock);
			/* check for race with truncate */
			if (page->mapping) {
				if (!mapping->backing_dev_info->memory_backed)
					inc_page_state(nr_dirty);
				list_del(&page->list);
				list_add(&page->list, get_moved_pages(mapping));
			}			
			spin_unlock(&mapping->page_lock);
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
	}
	return ret;
}

/* ->readpage() VFS method in reiser4 address_space_operations
   method serving file mmapping
*/
static int
reiser4_readpage(struct file *f /* file to read from */ ,
		 struct page *page	/* page where to read data
					 * into */ )
{
	struct inode *inode;
	file_plugin *fplug;
	int result;
	reiser4_context ctx;

	/*
	 * basically calls ->readpage method of object plugin and handles
	 * errors.
	 */

	assert("umka-078", f != NULL);
	assert("umka-079", page != NULL);
	assert("nikita-2280", PageLocked(page));
	assert("vs-976", !PageUptodate(page));

	assert("vs-318", page->mapping && page->mapping->host);
	assert("nikita-1352", (f == NULL) || (f->f_dentry->d_inode == page->mapping->host));

	/* ->readpage can be called from page fault service routine */
	assert("nikita-3174", schedulable());

	inode = page->mapping->host;
	init_context(&ctx, inode->i_sb);
	fplug = inode_file_plugin(inode);
	if (fplug->readpage != NULL)
		result = fplug->readpage(f, page);
	else
		result = RETERR(-EINVAL);
	if (result != 0) {
		SetPageError(page);
		unlock_page(page);
	}
	reiser4_exit_context(&ctx);
	return 0;
}

/* ->readpages() VFS method in reiser4 address_space_operations
   method serving page cache readahead

   reiser4_readpages works in the following way: on input it has coord which is set on extent that addresses first of
   pages for which read requests are to be issued. So, reiser4_readpages just walks forward through extent unit, finds
   which blocks are to be read and start read for them.

reiser4_readpages can be called from two places: from 
sys_read->reiser4_read->read_unix_file->read_extent->page_cache_readahead and
from
handling page fault: 
handle_mm_fault->do_no_page->filemap_nopage->page_cache_readaround

In first case coord is set by reiser4 read code. This case is detected by  if 
(is_in_reiser4_context()).

In second case, coord is not set and currently, reiser4_readpages does not do 
anything. 
*/
static int
reiser4_readpages(struct file *file, struct address_space *mapping,
		  struct list_head *pages, unsigned nr_pages)
{
	file_plugin *fplug;

	if (is_in_reiser4_context()) {
		/* we are called from reiser4 context, typically from method
		   which implements read into page cache. From read_extent,
		   for example */
		fplug = inode_file_plugin(mapping->host);
		if (fplug->readpages)
			fplug->readpages(file, mapping, pages);
	} else {
		/* we are called from page fault. Currently, we do not
		 * readahead in this case. */;
	}

	/* __do_page_cache_readahead expects filesystem's readpages method to
	 * process every page on this list */
	while (!list_empty(pages)) {
		struct page *page = list_entry(pages->prev, struct page, list);
		list_del(&page->list);
		page_cache_release(page);
	}
	return 0;
}

/* ->writepages()
   ->vm_writeback()
   ->set_page_dirty()
   ->prepare_write()
   ->commit_write()
*/

/* ->bmap() VFS method in reiser4 address_space_operations */
sector_t
reiser4_bmap(struct address_space *mapping, sector_t block)
{
	file_plugin *fplug;
	sector_t result;
	reiser4_context ctx;

	init_context(&ctx, mapping->host->i_sb);
	reiser4_stat_inc(vfs_calls.bmap);

	fplug = inode_file_plugin(mapping->host);
	if (fplug || fplug->get_block) {
		result = generic_block_bmap(mapping, block, fplug->get_block);
	} else
		result = RETERR(-EINVAL);
	reiser4_exit_context(&ctx);
	return result;
}

/* ->invalidatepage method for reiser4 */

/*
 * this is called for each truncated page from
 * truncate_inode_pages()->truncate_{complete,partial}_page().
 *
 * At the moment of call, page is under lock, and outstanding io (if any) has
 * completed.
 */

int
reiser4_invalidatepage(struct page *page /* page to invalidate */,
		       unsigned long offset /* starting offset for partial
					     * invalidation */)
{
	int ret = 0;
	reiser4_context ctx;
	struct inode *inode;

	/*
	 * This is called to truncate file's page.
	 *
	 * Originally, reiser4 implemented truncate in a standard way
	 * (vmtruncate() calls ->invalidatepage() on all truncated pages
	 * first, then file system ->truncate() call-back is invoked).
	 *
	 * This lead to the problem when ->invalidatepage() was called on a
	 * page with jnode that was captured into atom in ASTAGE_PRE_COMMIT
	 * process. That is, truncate was bypassing transactions. To avoid
	 * this, try_capture_page_to_invalidate() call was added here.
	 *
	 * After many troubles with vmtruncate() based truncate (including
	 * races with flush, tail conversion, etc.) it was re-written in the
	 * top-to-bottom style: items are killed in cut_tree_object() and
	 * pages belonging to extent are invalidated in kill_hook_extent(). So
	 * probably now additional call to capture is not needed here.
	 *
	 */

	assert("nikita-3137", PageLocked(page));
	assert("nikita-3138", !PageWriteback(page));
	inode = page->mapping->host;
	assert("vs-1426", ergo(PagePrivate(page) && get_super_fake(inode->i_sb) != inode, ((inode->i_state & I_JNODES) &&
											   (reiser4_inode_data(inode)->jnodes > 0))));
	assert("vs-1427", ergo(PagePrivate(page), page->mapping == jnode_get_mapping(jnode_by_page(page))));
	assert("vs-1449", !test_bit(PG_arch_1, &page->flags));

	init_context(&ctx, inode->i_sb);
	/* capture page being truncated. */
	ret = try_capture_page_to_invalidate(page);
	if (ret != 0) {
		warning("nikita-3141", "Cannot capture: %i", ret);
		print_page("page", page);
	} else
		assert("vs-1425", ((inode->i_state & I_JNODES) &&
				   (reiser4_inode_data(inode)->jnodes > 0)));


	if (offset == 0) {
		jnode *node;

		/* remove jnode from transaction and detach it from page. */
		node = jnode_by_page(page);
		if (node != NULL) {
			assert("vs-1435", !JF_ISSET(node, JNODE_CC));
			jref(node);
 			JF_SET(node, JNODE_HEARD_BANSHEE);
			/* page cannot be detached from jnode concurrently,
			 * because it is locked */
			uncapture_page(page);

			/* this detaches page from jnode, so that jdelete will not try to lock page which is already locked */
			UNDER_SPIN_VOID(jnode,
					node,
					page_clear_jnode(page, node));
			unhash_unformatted_jnode(node);

			jput(node);
		}
	}
	reiser4_exit_context(&ctx);
	return ret;
}

/* help function called from reiser4_releasepage(). It returns true if jnode
 * can be detached from its page and page released. */
static int
releasable(const jnode *node /* node to check */)
{
	assert("nikita-2781", node != NULL);
	assert("nikita-2783", spin_jnode_is_locked(node));

	/* is some thread is currently using jnode page, later cannot be
	 * detached */
	if (atomic_read(&node->d_count) != 0)
		return 0;

	assert("vs-1214", !jnode_is_loaded(node));

	/* emergency flushed page can be released. This is what emergency
	 * flush is all about after all. */
	if (JF_ISSET(node, JNODE_EFLUSH))
		return 1; /* yeah! */

	/* can only release page if real block number is assigned to
	   it. Simple check for ->atom wouldn't do, because it is possible for
	   node to be clean, not it atom yet, and still having fake block
	   number. For example, node just created in jinit_new(). */
	if (blocknr_is_fake(jnode_get_block(node)))
		return 0;
	/* dirty jnode cannot be released. It can however be submitted to disk
	 * as part of early flushing, but only after getting flush-prepped. */
	if (jnode_is_dirty(node))
		return 0;
	/* overwrite is only written by log writer. */
	if (JF_ISSET(node, JNODE_OVRWR))
		return 0;
	/* jnode is already under writeback */
	if (JF_ISSET(node, JNODE_WRITEBACK))
		return 0;
	/* page was modified through mmap, but its jnode is not yet
	 * captured. Don't discard modified data. */
	if (jnode_is_unformatted(node) && JF_ISSET(node, JNODE_KEEPME))
		return 0;
	/* don't flush bitmaps or journal records */
	if (!jnode_is_znode(node) && !jnode_is_unformatted(node))
		return 0;
	return 1;
}

#if REISER4_DEBUG
int jnode_is_releasable(const jnode *node)
{
	return releasable(node);
}
#endif

#define INC_STAT(page, node, counter)						\
	reiser4_stat_inc_at(page->mapping->host->i_sb, 				\
			    level[jnode_get_level(node) - LEAF_LEVEL].counter);

/*
 * ->releasepage method for reiser4
 *
 * This is called by VM scanner when it comes across clean page.  What we have
 * to do here is to check whether page can really be released (freed that is)
 * and if so, detach jnode from it and remove page from the page cache.
 *
 * Check for releasability is done by releasable() function.
 */
int
reiser4_releasepage(struct page *page, int gfp UNUSED_ARG)
{
	jnode *node;

	assert("nikita-2257", PagePrivate(page));
	assert("nikita-2259", PageLocked(page));
	assert("nikita-2892", !PageWriteback(page));
	assert("nikita-3019", schedulable());

	/* NOTE-NIKITA: this can be called in the context of reiser4 call. It
	   is not clear what to do in this case. A lot of deadlocks seems be
	   possible. */

	node = jnode_by_page(page);
	assert("nikita-2258", node != NULL);

	INC_STAT(page, node, page_try_release);

	/* is_page_cache_freeable() check

	   (mapping + private + page_cache_get() by shrink_cache()) */
	if (page_count(page) > 3)
		return 0;

	if (PageDirty(page))
		return 0;

	/* releasable() needs jnode lock, because it looks at the jnode fields
	 * and we need jload_lock here to avoid races with jload(). */
	LOCK_JNODE(node);
	LOCK_JLOAD(node);
	if (releasable(node)) {
		struct address_space *mapping;

		mapping = page->mapping;
		INC_STAT(page, node, page_released);
		jref(node);
		/* there is no need to synchronize against
		 * jnode_extent_write() here, because pages seen by
		 * jnode_extent_write() are !releasable(). */
		page_clear_jnode(page, node);
		UNLOCK_JLOAD(node);
		UNLOCK_JNODE(node);

		/* we are under memory pressure so release jnode also. */
		jput(node);
		spin_lock(&mapping->page_lock);
		/* shrink_list() + radix-tree */
		if (page_count(page) == 2) {
			__remove_from_page_cache(page);
			__put_page(page);
		}
		spin_unlock(&mapping->page_lock);
		return 1;
	} else {
		UNLOCK_JLOAD(node);
		UNLOCK_JNODE(node);
		assert("nikita-3020", schedulable());
		return 0;
	}
}

/* reiser4 writepages() address space operation this captures anonymous pages
   and anonymous jnodes. Anonymous pages are pages which are dirtied via
   mmapping. Anonymous jnodes are ones which were created by reiser4_writepage
 */
int
reiser4_writepages(struct address_space *mapping,
		   struct writeback_control *wbc)
{
	int ret = 0;
	struct inode *inode;
	file_plugin *fplug;

	inode = mapping->host;
	fplug = inode_file_plugin(inode);
	if (fplug != NULL && fplug->capture != NULL)
		/* call file plugin method to capture anonymous pages and
		 * anonymous jnodes */
		ret = fplug->capture(inode, wbc);

	/* work around infinite loop in pdflush->sync_sb_inodes. */
	/* Problem: ->writepages() is supposed to submit io for the pages from
	 * ->io_pages list and to clean this list. */
	mapping->dirtied_when = jiffies|1;
	spin_lock(&inode_lock);
	list_move(&mapping->host->i_list, &mapping->host->i_sb->s_dirty);
	spin_unlock(&inode_lock);
	return ret;
}

/* start actual IO on @page */
int reiser4_start_up_io(struct page *page)
{
	block_sync_page(page);
	return 0;
}

/*
 * reiser4 methods for VM
 */
struct address_space_operations reiser4_as_operations = {
	/* called during memory pressure by kswapd */
	.writepage = reiser4_writepage,
	/* called to read page from the storage when page is added into page
	   cache. This is done by page-fault handler. */
	.readpage = reiser4_readpage,
	/* Start IO on page. This is called from wait_on_page_bit() and
	   lock_page() and its purpose is to actually start io by jabbing
	   device drivers. */
	.sync_page = reiser4_start_up_io,
	/* called from
	 * reiser4_sync_inodes()->generic_sync_sb_inodes()->...->do_writepages()
	 *
	 * captures anonymous pages for given inode
	 */
	.writepages = reiser4_writepages,
	/* marks page dirty. Note that this is never called by reiser4
	 * directly. Reiser4 uses set_page_dirty_internal(). Reiser4 set page
	 * dirty is called for pages dirtied though mmap and moves dirty page
	 * to the special ->moved_list in its mapping. */
	.set_page_dirty = reiser4_set_page_dirty,
	/* called during read-ahead */
	.readpages = reiser4_readpages,
	.prepare_write = NULL, /* generic_file_write() call-back */
	.commit_write = NULL,  /* generic_file_write() call-back */
	/* map logical block number to disk block number. Used by FIBMAP ioctl
	 * and ..bmap pseudo file. */
	.bmap = reiser4_bmap,
	/* called just before page is taken out from address space (on
	   truncate, umount, or similar).  */
	.invalidatepage = reiser4_invalidatepage,
	/* called when VM is about to take page from address space (due to
	   memory pressure). */
	.releasepage = reiser4_releasepage,
	/* not yet implemented */
	.direct_IO = NULL
};


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
