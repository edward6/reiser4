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
static int reiser4_set_page_dirty (struct page * page)
{
	int ret = 0;

	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			spin_lock(&mapping->page_lock);
			if (page->mapping) {	/* Race with truncate? */
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


/* ->readpage() VFS method in reiser4 address_space_operations */
static int
reiser4_readpage(struct file *f /* file to read from */ ,
		 struct page *page	/* page where to read data
					 * into */ )
{
	struct inode *inode;
	file_plugin *fplug;
	int result;
	reiser4_context ctx;

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
		reiser4_unlock_page(page);
	}
	reiser4_exit_context(&ctx);
	return 0;
}
/* VS-FIXME-HANS: shouldn't there be more assertions in this file, and the function below? */
static int
reiser4_readpages(struct file *file, struct address_space *mapping,
		  struct list_head *pages, unsigned nr_pages)
{
	file_plugin *fplug;

	if (!is_in_reiser4_context()) {
		/* no readahead in the path: handle_mm_fault->do_no_page->filemap_nopage->page_cache_readaround */
/* VS-FIXME-HANS: this comment above means what? how about putting some better comments in this function? */
		unsigned i;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = list_entry(pages->prev, struct page, list);
			list_del(&page->list);
			page_cache_release(page);
		}
		return 0;
	}

	fplug = inode_file_plugin(mapping->host);
	if (fplug->readpages)
		fplug->readpages(file, mapping, pages);

	/* FIXME-VS: until it is not centralized */
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

/* ->invalidatepage()
   ->releasepage() */

/* ->invalidatepage method for reiser4 */
	/*
	 * this is called for each truncated page from
	 * truncate_inode_pages()->truncate_{complete,partial}_page().
	 *
	 * At the moment of call, page is under lock, and outstanding io (if
	 * any) has completed.
	 */

int
reiser4_invalidatepage(struct page *page, unsigned long offset)
{
	int ret = 0;
	reiser4_context ctx;
	struct inode *inode;

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
			UNDER_SPIN_VOID(jnode,
					node, page_clear_jnode(page, node));
			unhash_unformatted_jnode(node);
			jput(node);
		}
	}
	reiser4_exit_context(&ctx);
	return ret;
}

#if !REISER4_DEBUG
static
#endif
int
releasable(const jnode *node)
{
	assert("nikita-2781", node != NULL);
	assert("nikita-2783", spin_jnode_is_locked(node));

	if (atomic_read(&node->d_count) != 0) {
		return 0;
	}
	assert("vs-1214", !jnode_is_loaded(node));

	if (JF_ISSET(node, JNODE_EFLUSH))
		return 1; /* yeah! */

	/* can only release page if real block number is assigned to
	   it. Simple check for ->atom wouldn't do, because it is possible for
	   node to be clean, not it atom yet, and still having fake block
	   number. For example, node just created in jinit_new(). */
	if (blocknr_is_fake(jnode_get_block(node)))
		return 0;
	if (jnode_is_dirty(node))
		return 0;
	if (JF_ISSET(node, JNODE_OVRWR))
		return 0;
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

#define INC_STAT(page, node, counter)						\
	reiser4_stat_inc_at(page->mapping->host->i_sb, 				\
			    level[jnode_get_level(node) - LEAF_LEVEL].counter);

/* ->releasepage method for reiser4 */
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
	if (page_count(page) > 3) {
		return 0;
	}

	if (PageDirty(page)) {
		return 0;
	}

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

/* reiser4 writepages() address space operation */

	/* Here we can call synchronously. 
VS-FIXME-HANS: call what?
We can be called from balance_dirty_pages()
	   Reiser4 code is supposed to call balance_dirty_pages at places where no locks
	   are held, which means we can call begin jnode_flush right from there  (VS-FIXME-HANS where? ) having no
	   deadlocks between the caller of balance_dirty_pages() and jnode_flush(). */
int
reiser4_writepages(struct address_space *mapping,
		   struct writeback_control *wbc)
{
	int ret = 0;
	struct inode *inode;

	reiser4_context ctx;

	init_context(&ctx, mapping->host->i_sb);
	/* avoid recursive calls to ->sync_inodes */
	ctx.nobalance = 1;
	assert("zam-760", ergo(is_in_reiser4_context(),
			       lock_stack_isclean(get_current_lock_stack())));

	inode = mapping->host;
	if (inode_file_plugin(inode) != NULL &&
	    inode_file_plugin(inode)->capture != NULL)
		ret = inode_file_plugin(inode)->capture(inode, wbc);

	/* work around infinite loop in pdflush->sync_sb_inodes. */
	/* Problem: ->writepages() is supposed to submit io for the pages from
	 * ->io_pages list and to clean this list. */
	mapping->dirtied_when = jiffies|1;
	spin_lock(&inode_lock);
	list_move(&mapping->host->i_list, &mapping->host->i_sb->s_dirty);
	spin_unlock(&inode_lock);
	reiser4_exit_context(&ctx);
	return ret;
}

int reiser4_sync_page(struct page *page)
{
	if (REISER4_TRACE_TREE) {
		jnode *j;

		assert("vs-1111", page->mapping && page->mapping->host);

		j = jprivate(page);
		if (j != NULL) {
			reiser4_block_nr block;
			struct super_block *s;
			char jbuf[100];

			block = *jnode_get_block(j);
			s = page->mapping->host->i_sb;
			(void)jnode_short_info(j, jbuf);
			write_tracef(&get_super_private(s)->trace_file, s,
				     "wait_on_page: %llu %s\n", block, jbuf);
			(void)jbuf; /* ohoho */
		}
	}
	block_sync_page(page);
	return 0;
}

struct address_space_operations reiser4_as_operations = {
	/* called during memory pressure by kswapd */
	.writepage = reiser4_writepage,
	/* called to read page from the storage when page is added into page
	   cache  */
	.readpage = reiser4_readpage,
	/* This is most annoyingly misnomered method. 

VS-FIXME-HANS: then rename it.

Actually it is called
	   from wait_on_page_bit() and lock_page() and its purpose is to
	   actually start io by jabbing device drivers.
	*/
	.sync_page = reiser4_sync_page,
	/* called during sync (pdflush) */
	.writepages = reiser4_writepages,
	.set_page_dirty = reiser4_set_page_dirty,
	/* called during read-ahead */
	.readpages = reiser4_readpages,
	.prepare_write = NULL,
	.commit_write = NULL,
	.bmap = reiser4_bmap,
	/* called just before page is taken out from address space (on
	   truncate, umount, or similar).  */
	.invalidatepage = reiser4_invalidatepage,
	/* called when VM is about to take page from address space (due to
	   memory pressure). */
	.releasepage = reiser4_releasepage,
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
