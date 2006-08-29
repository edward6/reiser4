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
#include "plugin/object.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "entd.h"

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
#include <linux/backing-dev.h>
#include <linux/quotaops.h>
#include <linux/security.h>

/* address space operations */

/**
 * reiser4_set_page_dirty - set dirty bit, tag in page tree, dirty accounting
 * @page: page to be dirtied
 *
 * Operation of struct address_space_operations. This implementation is used by
 * unix and cryptcompress file plugins.
 *
 * This is called when reiser4 page gets dirtied outside of reiser4, for
 * example, when dirty bit is moved from pte to physical page.
 *
 * Tags page in the mapping's page tree with special tag so that it is possible
 * to do all the reiser4 specific work wrt dirty pages (jnode creation,
 * capturing by an atom) later because it can not be done in the contexts where
 * set_page_dirty is called.
 */
int reiser4_set_page_dirty(struct page *page)
{
	/* this page can be unformatted only */
	assert("vs-1734", (page->mapping &&
			   page->mapping->host &&
			   reiser4_get_super_fake(page->mapping->host->i_sb) !=
			   page->mapping->host
			   && reiser4_get_cc_fake(page->mapping->host->i_sb) !=
			   page->mapping->host
			   && reiser4_get_bitmap_fake(page->mapping->host->i_sb) !=
			   page->mapping->host));

	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			write_lock_irq(&mapping->tree_lock);

			/* check for race with truncate */
			if (page->mapping) {
				assert("vs-1652", page->mapping == mapping);
				if (mapping_cap_account_dirty(mapping))
					inc_zone_page_state(page,
							NR_FILE_DIRTY);
				radix_tree_tag_set(&mapping->page_tree,
						   page->index,
						   PAGECACHE_TAG_REISER4_MOVED);
			}
			write_unlock_irq(&mapping->tree_lock);
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
	}
	return 0;
}

static int filler(void *vp, struct page *page)
{
	return page->mapping->a_ops->readpage(vp, page);
}

/**
 * reiser4_readpages - submit read for a set of pages
 * @file: file to read
 * @mapping: address space
 * @pages: list of pages to submit read for
 * @nr_pages: number of pages no the list
 *
 * Operation of struct address_space_operations. This implementation is used by
 * unix and cryptcompress file plugins.
 *
 * Calls read_cache_pages or readpages hook if it is set.
 */
int
reiser4_readpages(struct file *file, struct address_space *mapping,
		  struct list_head *pages, unsigned nr_pages)
{
	reiser4_context *ctx;
	reiser4_file_fsdata *fsdata;

	ctx = reiser4_init_context(mapping->host->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	fsdata = reiser4_get_file_fsdata(file);
	if (IS_ERR(fsdata)) {
		reiser4_exit_context(ctx);
		return PTR_ERR(fsdata);
	}

	if (fsdata->ra2.readpages)
		fsdata->ra2.readpages(mapping, pages, fsdata->ra2.data);
	else {
		/*
		 * filler (reiser4 readpage method) may involve tree search
		 * which is not allowed when lock stack is not clean. If lock
		 * stack is not clean - do nothing.
		 */
		if (lock_stack_isclean(get_current_lock_stack()))
			read_cache_pages(mapping, pages, filler, file);
		else {
			while (!list_empty(pages)) {
				struct page *victim;

				victim = list_entry(pages->prev, struct page, lru);
				list_del(&victim->lru);
				page_cache_release(victim);
			}
		}
	}
	reiser4_exit_context(ctx);
	return 0;
}

/* ->invalidatepage method for reiser4 */

/*
 * this is called for each truncated page from
 * truncate_inode_pages()->truncate_{complete,partial}_page().
 *
 * At the moment of call, page is under lock, and outstanding io (if any) has
 * completed.
 */

/**
 * reiser4_invalidatepage
 * @page: page to invalidate
 * @offset: starting offset for partial invalidation
 *
 */
void reiser4_invalidatepage(struct page *page, unsigned long offset)
{
	int ret = 0;
	reiser4_context *ctx;
	struct inode *inode;
	jnode *node;

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
	 * top-to-bottom style: items are killed in reiser4_cut_tree_object()
	 * and pages belonging to extent are invalidated in kill_hook_extent().
	 * So probably now additional call to capture is not needed here.
	 */

	assert("nikita-3137", PageLocked(page));
	assert("nikita-3138", !PageWriteback(page));
	inode = page->mapping->host;

	/*
	 * ->invalidatepage() should only be called for the unformatted
	 * jnodes. Destruction of all other types of jnodes is performed
	 * separately. But, during some corner cases (like handling errors
	 * during mount) it is simpler to let ->invalidatepage to be called on
	 * them. Check for this, and do nothing.
	 */
	if (reiser4_get_super_fake(inode->i_sb) == inode)
		return;
	if (reiser4_get_cc_fake(inode->i_sb) == inode)
		return;
	if (reiser4_get_bitmap_fake(inode->i_sb) == inode)
		return;
	assert("vs-1426", PagePrivate(page));
	assert("vs-1427",
	       page->mapping == jnode_get_mapping(jnode_by_page(page)));
	assert("", jprivate(page) != NULL);
	assert("", ergo(inode_file_plugin(inode) !=
			file_plugin_by_id(CRC_FILE_PLUGIN_ID), offset == 0));

	ctx = reiser4_init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return;

	node = jprivate(page);
	spin_lock_jnode(node);
	if (!(node->state & ((1 << JNODE_DIRTY) | (1<< JNODE_FLUSH_QUEUED) |
			  (1 << JNODE_WRITEBACK) | (1 << JNODE_OVRWR)))) {
		/* there is not need to capture */
		jref(node);
		JF_SET(node, JNODE_HEARD_BANSHEE);
		page_clear_jnode(page, node);
		reiser4_uncapture_jnode(node);
		unhash_unformatted_jnode(node);
		jput(node);
		reiser4_exit_context(ctx);
		return;
	}
	spin_unlock_jnode(node);

	/* capture page being truncated. */
	ret = try_capture_page_to_invalidate(page);
	if (ret != 0)
		warning("nikita-3141", "Cannot capture: %i", ret);

	if (offset == 0) {
		/* remove jnode from transaction and detach it from page. */
		jref(node);
		JF_SET(node, JNODE_HEARD_BANSHEE);
		/* page cannot be detached from jnode concurrently, because it
		 * is locked */
		reiser4_uncapture_page(page);

		/* this detaches page from jnode, so that jdelete will not try
		 * to lock page which is already locked */
		spin_lock_jnode(node);
		page_clear_jnode(page, node);
		spin_unlock_jnode(node);
		unhash_unformatted_jnode(node);

		jput(node);
	}

	reiser4_exit_context(ctx);
}

/* help function called from reiser4_releasepage(). It returns true if jnode
 * can be detached from its page and page released. */
int jnode_is_releasable(jnode * node /* node to check */ )
{
	assert("nikita-2781", node != NULL);
	assert_spin_locked(&(node->guard));
	assert_spin_locked(&(node->load));

	/* is some thread is currently using jnode page, later cannot be
	 * detached */
	if (atomic_read(&node->d_count) != 0) {
		return 0;
	}

	assert("vs-1214", !jnode_is_loaded(node));

	/*
	 * can only release page if real block number is assigned to it. Simple
	 * check for ->atom wouldn't do, because it is possible for node to be
	 * clean, not it atom yet, and still having fake block number. For
	 * example, node just created in jinit_new().
	 */
	if (reiser4_blocknr_is_fake(jnode_get_block(node)))
		return 0;

	/*
	 * pages prepared for write can not be released anyway, so avoid
	 * detaching jnode from the page
	 */
	if (JF_ISSET(node, JNODE_WRITE_PREPARED))
		return 0;

	/*
	 * dirty jnode cannot be released. It can however be submitted to disk
	 * as part of early flushing, but only after getting flush-prepped.
	 */
	if (JF_ISSET(node, JNODE_DIRTY))
		return 0;

	/* overwrite set is only written by log writer. */
	if (JF_ISSET(node, JNODE_OVRWR))
		return 0;

	/* jnode is already under writeback */
	if (JF_ISSET(node, JNODE_WRITEBACK))
		return 0;

	/* don't flush bitmaps or journal records */
	if (!jnode_is_znode(node) && !jnode_is_unformatted(node))
		return 0;

	return 1;
}

/*
 * ->releasepage method for reiser4
 *
 * This is called by VM scanner when it comes across clean page.  What we have
 * to do here is to check whether page can really be released (freed that is)
 * and if so, detach jnode from it and remove page from the page cache.
 *
 * Check for releasability is done by releasable() function.
 */
int reiser4_releasepage(struct page *page, gfp_t gfp UNUSED_ARG)
{
	jnode *node;

	assert("nikita-2257", PagePrivate(page));
	assert("nikita-2259", PageLocked(page));
	assert("nikita-2892", !PageWriteback(page));
	assert("nikita-3019", reiser4_schedulable());

	/* NOTE-NIKITA: this can be called in the context of reiser4 call. It
	   is not clear what to do in this case. A lot of deadlocks seems be
	   possible. */

	node = jnode_by_page(page);
	assert("nikita-2258", node != NULL);
	assert("reiser4-4", page->mapping != NULL);
	assert("reiser4-5", page->mapping->host != NULL);

	if (PageDirty(page))
		return 0;

	/* releasable() needs jnode lock, because it looks at the jnode fields
	 * and we need jload_lock here to avoid races with jload(). */
	spin_lock_jnode(node);
	spin_lock(&(node->load));
	if (jnode_is_releasable(node)) {
		struct address_space *mapping;

		mapping = page->mapping;
		jref(node);
		/* there is no need to synchronize against
		 * jnode_extent_write() here, because pages seen by
		 * jnode_extent_write() are !releasable(). */
		page_clear_jnode(page, node);
		spin_unlock(&(node->load));
		spin_unlock_jnode(node);

		/* we are under memory pressure so release jnode also. */
		jput(node);

		return 1;
	} else {
		spin_unlock(&(node->load));
		spin_unlock_jnode(node);
		assert("nikita-3020", reiser4_schedulable());
		return 0;
	}
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
