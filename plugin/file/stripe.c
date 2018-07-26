/*
  Copyright (c) 2018 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * Bodies of striped files are composed of distribution units (stripes).
 * Every stripe, in turn, is a set of extents, and every extent is a set
 * of allocation units (file system blocks) with contiguous disk addresses.
 * Extents are represented in the storage tree by extent pointers (items)
 * of EXTENT41_POINTER_ID. Extent pointer's key is calculated like for
 * classic unix files except the ordering component, which contains ID
 * of a brick (subvolume), where that block should be stored (see
 * build_body_key_stripe()). Holes in a striped file are not represented
 * by any items.
 *
 * In the initial implementation any modifying operation takes an
 * exclusive access to the whole file. It is for simplicity:
 * further we'll lock only a part of the file covered by a respective
 * node on the twig level (longterm lock).
 */

#include "../../inode.h"
#include "../../super.h"
#include "../../tree_walk.h"
#include "../../carry.h"
#include "../../page_cache.h"
#include "../object.h"
#include "../cluster.h"
#include "../../safe_link.h"

#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/syscalls.h>

int readpages_filler_generic(void *data, struct page *page, int striped);

reiser4_subvol *calc_data_subvol_stripe(const struct inode *inode,
					loff_t offset)
{
	volume_plugin *vplug = current_vol_plug();

	return current_origin(vplug->data_subvol_id_calc(get_inode_oid(inode),
							 offset));
}

int build_body_key_stripe(struct inode *inode, loff_t off, reiser4_key *key)
{
	reiser4_key_init(key);
	set_key_locality(key, reiser4_inode_data(inode)->locality_id);
	set_key_objectid(key, get_inode_oid(inode));
	set_key_ordering(key, calc_data_subvol_stripe(inode, off)->id);
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) off);
	return 0;
}

int flow_by_inode_stripe(struct inode *inode,
			 const char __user *buf, int user,
			 loff_t size, loff_t off,
			 rw_op op, flow_t *flow)
{
	flow->length = size;
	memcpy(&flow->data, &buf, sizeof(buf));
	flow->user = user;
	flow->op = op;
	assert("edward-2088",
	       inode_file_plugin(inode) ==
	       file_plugin_by_id(STRIPED_FILE_PLUGIN_ID));
	assert("edward-2089",
	       inode_file_plugin(inode)->build_body_key ==
	       build_body_key_stripe);
	/*
	 * calculate key of write position and insert it into flow->key
	 */
	return build_body_key_stripe(inode, off, &flow->key);
}

ssize_t read_stripe(struct file *file, char __user *buf,
		    size_t read_amount, loff_t *off)
{
	ssize_t result;
	struct inode *inode;
	reiser4_context *ctx;
	struct unix_file_info *uf_info;

	if (unlikely(read_amount == 0))
		return 0;

	inode = file_inode(file);
	assert("edward-2029", !reiser4_inode_get_flag(inode, REISER4_NO_SD));

	ctx = reiser4_init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	result = reserve_update_sd_common(inode);
	if (unlikely(result != 0))
		goto out;
	uf_info = unix_file_inode_data(inode);

	get_nonexclusive_access(uf_info);
	result = new_sync_read(file, buf, read_amount, off);
	drop_nonexclusive_access(uf_info);
 out:
	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);
	return result;
}

ssize_t write_stripe(struct file *file,
		     const char __user *buf,
		     size_t count, loff_t *pos,
		     struct dispatch_context *cont)
{
	int result;
	reiser4_context *ctx;
	struct inode *inode = file_inode(file);
	struct unix_file_info *uf_info;
	ssize_t written;
	int to_write = PAGE_SIZE * reiser4_write_granularity();
	size_t left;
	int enospc = 0; /* item plugin ->write() returned ENOSPC */

	ctx = get_current_context();

	assert("edward-2030", !reiser4_inode_get_flag(inode, REISER4_NO_SD));

	result = file_remove_privs(file);
	if (result) {
		context_set_commit_async(ctx);
		return result;
	}
	/* remove_suid might create a transaction */
	reiser4_txn_restart(ctx);
	uf_info = unix_file_inode_data(inode);

	written = 0;
	left = count;

	while (left) {
		int update_sd = 0;
		if (left < to_write)
			to_write = left;

		get_exclusive_access(uf_info);

		written = write_extent_stripe(file, inode, buf,
					      to_write, pos);
		if (written == -ENOSPC && !enospc) {
			drop_exclusive_access(uf_info);
			txnmgr_force_commit_all(inode->i_sb, 0);
			enospc = 1;
			continue;
		}
		if (written < 0) {
			/*
			 * If this is -ENOSPC, then it happened
			 * second time, so don't try to free space
			 * once again.
			 */
			result = written;
			drop_exclusive_access(uf_info);
			break;
		}
		enospc = 0;
		/*
		 * something is written
		 */
		if (*pos + written > inode->i_size) {
			INODE_SET_FIELD(inode, i_size, *pos + written);
			update_sd = 1;
		}
		if (!IS_NOCMTIME(inode)) {
			inode->i_ctime = inode->i_mtime = current_time(inode);
			update_sd = 1;
		}
		if (update_sd) {
			/*
			 * space for update_sd was reserved
			 * in write_extent()
			 */
			result = reiser4_update_sd(inode);
			if (result) {
				warning("edward-1574",
					"Can not update stat-data: %i. FSCK?",
					result);
				context_set_commit_async(ctx);
				drop_exclusive_access(uf_info);
				break;
			}
		}
		drop_exclusive_access(uf_info);
		/*
		 * tell VM how many pages were dirtied. Maybe number of pages
		 * which were dirty already should not be counted
		 */
		reiser4_throttle_write(inode);
		left -= written;
		buf += written;
		*pos += written;
	}
	if (result == 0 && ((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
		reiser4_txn_restart_current();
		grab_space_enable();
		result = reiser4_sync_file_common(file, 0, LONG_MAX,
						  0 /* data and stat data */);
		if (result)
			warning("reiser4-7", "failed to sync file %llu",
				(unsigned long long)get_inode_oid(inode));
	}
	/*
	 * return number of written bytes or error code if nothing is
	 * written. Note, that it does not work correctly in case when
	 * sync_unix_file returns error
	 */
	return (count - left) ? (count - left) : result;
}

static inline int readpages_filler_stripe(void *data, struct page *page)
{
	return reiser4_readpages_filler_generic(data, page, 1);
}

int readpages_stripe(struct file *file, struct address_space *mapping,
		     struct list_head *pages, unsigned nr_pages)
{
	return reiser4_readpages_generic(file, mapping, pages, nr_pages,
					 readpages_filler_stripe);
}

void validate_extended_coord(uf_coord_t *uf_coord, loff_t offset);
/**
 * ->readpage() method of address space operations for striped-file plugin
 */
int readpage_stripe(struct file *file, struct page *page)
{
	reiser4_context *ctx;
	int result;
	struct inode *inode;
	reiser4_key key;
	hint_t *hint;
	lock_handle *lh;
	coord_t *coord;

	assert("vs-1062", PageLocked(page));
	assert("vs-976", !PageUptodate(page));
	assert("vs-1061", page->mapping && page->mapping->host);

	inode = page->mapping->host;

	if (inode->i_size <= page_offset(page)) {
		/* page is out of file */
		zero_user(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
		unlock_page(page);
		return 0;
	}
	ctx = reiser4_init_context(inode->i_sb);
	if (IS_ERR(ctx)) {
		unlock_page(page);
		return PTR_ERR(ctx);
	}
	hint = kmalloc(sizeof(*hint), reiser4_ctx_gfp_mask_get());
	if (hint == NULL) {
		unlock_page(page);
		reiser4_exit_context(ctx);
		return RETERR(-ENOMEM);
	}

	result = load_file_hint(file, hint);
	if (result) {
		kfree(hint);
		unlock_page(page);
		reiser4_exit_context(ctx);
		return result;
	}
	lh = &hint->lh;
	/*
	 * construct key of the page's first byte
	 */
	build_body_key_stripe(inode, page_offset(page), &key);
	/*
	 * look for file metadata corresponding to the page's first byte
	 */
	get_page(page);
	unlock_page(page);
	result = find_file_item_nohint(&hint->ext_coord.coord,
				       hint->ext_coord.lh, &key,
				       ZNODE_READ_LOCK, inode);
	lock_page(page);
	put_page(page);

	if (page->mapping == NULL) {
		/*
		 * readpage allows truncate to run concurrently.
		 * Page was truncated while it was not locked
		 */
		done_lh(lh);
		kfree(hint);
		unlock_page(page);
		reiser4_txn_restart(ctx);
		reiser4_exit_context(ctx);
		return -EINVAL;
	}
	if (IS_CBKERR(result)) {
		done_lh(lh);
		kfree(hint);
		unlock_page(page);
		reiser4_txn_restart(ctx);
		reiser4_exit_context(ctx);
		return result;
	}
	if (PageUptodate(page)) {
		done_lh(lh);
		kfree(hint);
		unlock_page(page);
		reiser4_txn_restart(ctx);
		reiser4_exit_context(ctx);
		return 0;
	}
	coord = &hint->ext_coord.coord;
	result = zload(coord->node);
	if (result) {
		done_lh(lh);
		kfree(hint);
		unlock_page(page);
		reiser4_txn_restart(ctx);
		reiser4_exit_context(ctx);
		return result;
	}
	validate_extended_coord(&hint->ext_coord, page_offset(page));

	if (coord_is_existing_unit(coord)) {
		result = reiser4_readpage_extent(coord, page);
		assert("edward-2032", result == 0);
	} else {
		/* hole in the file */
		result = __reiser4_readpage_extent(NULL, 0, page);
		assert("edward-2033", result == 0);
	}
	if (result) {
		unlock_page(page);
		reiser4_unset_hint(hint);
	} else {
		build_body_key_stripe(inode,
				      (loff_t)(page->index + 1) << PAGE_SHIFT,
				      &key);
		/* FIXME should call reiser4_set_hint() */
		reiser4_unset_hint(hint);
	}
	assert("edward-2034",
	       ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
	assert("edward-2035",
	       ergo(result != 0, !PageLocked(page)));
	zrelse(coord->node);
	done_lh(lh);

	save_file_hint(file, hint);
	kfree(hint);

	/*
	 * FIXME: explain why it is needed. HINT: page allocation in write can
	 * not be done when atom is not NULL because reiser4_writepage can not
	 * kick entd and have to eflush
	 */
	reiser4_txn_restart(ctx);
	reiser4_exit_context(ctx);
	return result;
}

/**
 * Cut body of a striped file. Items of such file are grouped in
 * the storage tree by subvolume IDs. In each group they are ordered
 * by offsets.
 *
 * We cut off file's items one by one in a loop until @new_size
 * of the file is reached. At every iteration we reserve space,
 * find the last file's item in the tree, cut it off, and finally
 * update file's stat data.
 */
static int cut_file_items_stripe(struct inode *inode, loff_t new_size,
				 int update_sd, loff_t cur_size,
				 int (*update_size_fn) (struct inode *,
							loff_t, int))
{
	reiser4_tree *tree;
	reiser4_key from_key, to_key;
	reiser4_key smallest_removed;
	int ret = 0;
	int progress = 0;
	/*
	 * for now we can scatter only files managed by
	 * unix-file plugin and built on extent items
	 */
	assert("edward-2021", inode_file_plugin(inode) ==
	       file_plugin_by_id(STRIPED_FILE_PLUGIN_ID));

	tree = meta_subvol_tree();

	while (i_size_read(inode) != new_size) {
		loff_t from_off;
		/*
		 * Cut the last stripe of the file's body.
		 */
		assert("edward-2020", inode->i_size != 0);
		/*
		 * when constructing @to_key, round up file size to
		 * block size boundary for parse_cut_by_key_range()
		 * to evaluate cut mode properly
		 */
		build_body_key_stripe(inode,
		      round_up(i_size_read(inode), current_blocksize) - 1,
				      &to_key);
		from_key = to_key;
		/*
		 * cut not more than last stripe, as stripes are located
		 * in the tree not in the logical order.
		 *
		 * FIXME: It is suboptimal: in the case of large holes
		 * it will lead to large number of iterations.
		 * Better solution is to find the last file item in the
		 * tree and calculate to_off and from_off by that item.
		 */
		if (current_stripe_bits) {
			from_off = round_down(i_size_read(inode) - 1,
					      current_stripe_size);
			if (from_off < new_size)
				from_off = new_size;
		} else
			from_off = new_size;

		set_key_offset(&from_key, from_off);

		/* the below takes sbinfo->delete_mutex */
		ret = reserve_cut_iteration(inode);
		if (ret)
			return ret;
		ret = reiser4_cut_tree_object(tree,
					      &from_key, &to_key,
					      &smallest_removed, inode, 1,
					      &progress);
		if (ret == -E_REPEAT) {
			/*
			 * -E_REPEAT is a signal to interrupt a long
			 * file truncation process
			 */
			if (progress) {
				ret = update_size_fn(inode,
					      get_key_offset(&smallest_removed),
					      update_sd);
				if (ret)
					break;
			} else {
				/*
				 * reiser4_cut_tree_object() was interrupted
				 * probably because current atom requires
				 * commit, we have to release transaction
				 * handle to allow atom commit.
				 */
				all_grabbed2free();
				/* release sbinfo->delete_mutex */
				reiser4_release_reserved(inode->i_sb);
				reiser4_txn_restart_current();
				continue;
			}
		} else if (ret == CBK_COORD_NOTFOUND || !progress) {
			ret = update_size_fn(inode, from_off, update_sd);
		} else if (ret == -E_NO_NEIGHBOR) {
			ret = update_size_fn(inode, new_size, update_sd);
			break;
		} else if (ret == 0) {
			ret = update_size_fn(inode,
					     get_key_offset(&smallest_removed),
					     update_sd);
			if (ret)
				break;
		} else
			break;
		all_grabbed2free();
		/* release sbinfo->delete_mutex) */
		reiser4_release_reserved(inode->i_sb);
	}
	all_grabbed2free();
	/* release sbinfo->delete_mutex */
	reiser4_release_reserved(inode->i_sb);
	return ret;
}

int find_or_create_extent_stripe(struct page *page)
{
	return find_or_create_extent_generic(page, update_extent_stripe);
}

static int shorten_stripe(struct inode *inode, loff_t new_size)
{
	int result;
	struct page *page;
	int padd_from;
	unsigned long index;
	/*
	 * cut file body
	 */
	result = cut_file_items_stripe(inode, new_size,
				       1, /* update_sd */
				       get_key_offset(reiser4_max_key()),
				       reiser4_update_file_size);
	if (result)
		return result;
	assert("vs-1105", new_size == inode->i_size);
	/*
	 * drop all the pages that don't have jnodes (i.e. pages
	 * which can not be truncated by cut_file_items() because
	 * of holes, which are not represented by any items, so
	 * that we can't call kill hooks to truncate them like in
	 * the case of calassic unix-files
	 */
	truncate_inode_pages(inode->i_mapping, round_up(new_size, PAGE_SIZE));

	padd_from = inode->i_size & (PAGE_SIZE - 1);
	if (!padd_from)
		/* file is truncated to page boundary */
		return 0;
	/*
	 * last page is partially truncated - zero its content
	 */
	index = (inode->i_size >> PAGE_SHIFT);
	result = reserve_partial_page(inode, index);
	if (result) {
		reiser4_release_reserved(inode->i_sb);
		return result;
	}
	page = read_mapping_page(inode->i_mapping, index, NULL);
	if (IS_ERR(page)) {
		/*
		 * the below does up(sbinfo->delete_mutex).
		 * Do not get confused
		 */
		reiser4_release_reserved(inode->i_sb);
		return PTR_ERR(page);
	}
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		put_page(page);
		/*
		 * the below does up(sbinfo->delete_mutex).
		 * Do not get confused
		 */
		reiser4_release_reserved(inode->i_sb);
		return RETERR(-EIO);
	}
	lock_page(page);
	assert("edward-2036", PageLocked(page));
	zero_user_segment(page, padd_from, PAGE_SIZE);
	unlock_page(page);
	result = find_or_create_extent_stripe(page);
	put_page(page);
	/*
	 * the below does up(sbinfo->delete_mutex).
	 * Do not get confused
	 */
	reiser4_release_reserved(inode->i_sb);
	return result;
}

static int truncate_body_stripe(struct inode *inode, struct iattr *attr)
{
	int ret;
	loff_t new_size = attr->ia_size;

	if (inode->i_size < new_size) {
		/* expand */
		ret = write_extent_stripe(NULL, inode, NULL, 0, &new_size);
		if (ret)
			return ret;
		return reiser4_update_file_size(inode, new_size, 1);
	} else
		/* shrink */
		return shorten_stripe(inode, new_size);
}

int setattr_stripe(struct dentry *dentry, struct iattr *attr)
{
	return reiser4_setattr_generic(dentry, attr, truncate_body_stripe);
}

int delete_object_stripe(struct inode *inode)
{
	struct unix_file_info *uf_info;
	int result;

	if (reiser4_inode_get_flag(inode, REISER4_NO_SD))
		return 0;

	/* truncate file body first */
	uf_info = unix_file_inode_data(inode);
	get_exclusive_access(uf_info);
	result = shorten_stripe(inode, 0 /* new size */);
	drop_exclusive_access(uf_info);

	if (unlikely(result != 0))
		warning("edward-2037",
			"failed to truncate striped file (%llu) on removal: %d",
			get_inode_oid(inode), result);

	/* remove stat data and safe link */
	return reiser4_delete_object_common(inode);
}

int create_object_stripe(struct inode *object, struct inode *parent,
			 reiser4_object_create_data *data, oid_t *oid)
{
	reiser4_inode *info;

	assert("edward-2038", object != NULL);
	assert("edward-2039", parent != NULL);
	assert("edward-2040", data != NULL);
	assert("edward-2041", reiser4_inode_get_flag(object, REISER4_NO_SD));
	assert("edward-2042", data->id == STRIPED_FILE_PLUGIN_ID);

	info = reiser4_inode_data(object);

	assert("edward-2043", info != NULL);
	/*
	 * Since striped file plugin is not default, we
	 * need to store its id in stat-data extention
	 */
	info->plugin_mask |= (1 << PSET_FILE);

	return write_sd_by_inode_common(object, oid);
}

int open_stripe(struct inode *inode, struct file *file)
{
	/*
	 * nothing to do at open time
	 */
	return 0;
}

int release_stripe(struct inode *inode, struct file *file)
{
	reiser4_free_file_fsdata(file);
	return 0;
}

/**
 * Capture one anonymous page
 */
static int capture_anon_page(struct page *page)
{
	int ret;
	struct inode *inode;

	if (PageWriteback(page))
		/*
		 * FIXME: do nothing?
		 */
		return 0;
	assert("edward-2044", page->mapping && page->mapping->host);

	inode = page->mapping->host;

	assert("edward-2045", inode->i_size > page_offset(page));

	ret = reserve_capture_anon_page(page);
	if (ret)
		return ret;
	ret = find_or_create_extent_stripe(page);
	if (ret) {
		SetPageError(page);
		warning("edward-2046",
			"Failed to capture anon page of striped file: %i", ret);
	} else
		ret = 1;
	return ret;
}

static int commit_file_atoms(struct inode *inode)
{
	int ret;

	reiser4_txn_restart_current();
	ret =
		/*
		 * when we are called by
		 * filemap_fdatawrite->
		 *    do_writepages()->
		 *       reiser4_writepages_dispatch()
		 *
		 * inode->i_mapping->dirty_pages are spices into
		 * ->io_pages, leaving ->dirty_pages dirty.
		 *
		 * When we are called from
		 * reiser4_fsync()->sync_unix_file(), we have to
		 * commit atoms of all pages on the ->dirty_list.
		 *
		 * So for simplicity we just commit ->io_pages and
		 * ->dirty_pages.
		 */
		reiser4_sync_page_list(inode);
	/*
	 * commit current transaction: there can be captured nodes from
	 * find_file_state() and finish_conversion().
	 */
	reiser4_txn_restart_current();
	return ret;
}

int writepages_stripe(struct address_space *mapping,
		      struct writeback_control *wbc)
{
	return reiser4_writepages_generic(mapping, wbc,
					  capture_anon_page,
					  commit_file_atoms);
}

int ioctl_stripe(struct file *filp, unsigned int cmd,
		 unsigned long arg)
{
	return 0;
}

/**
 * Estimate and reserve disk space needed for write_end_stripe():
 * one block for page itself, and one item insertion which may
 * happen if page corresponds to hole extent and unallocated one
 * will have to be created.
 * @inode: object that the page belongs to;
 * @index: index of the page.
 */
static int reserve_write_begin_stripe(const struct inode *inode,
				      pgoff_t index)
{
	int ret;
	reiser4_subvol *subv_m = get_meta_subvol();
	reiser4_subvol *subv_d = calc_data_subvol(inode,
						  index << PAGE_SHIFT);
	grab_space_enable();
	ret = reiser4_grab_space(1, BA_CAN_COMMIT, subv_d);
	if (ret)
		return ret;
	grab_space_enable();
	ret = reiser4_grab_space(estimate_one_insert_into_item(&subv_m->tree),
				 BA_CAN_COMMIT, subv_m);
	return ret;
}

/**
 * implementation of ->write_begin() address space operation
 * for striped-file plugin
 */
int write_begin_stripe(struct file *file, struct page *page,
		       loff_t pos, unsigned len, void **fsdata)
{
	int ret;
	struct inode * inode;
	struct unix_file_info *info;

	inode = file_inode(file);
	info = unix_file_inode_data(inode);

	ret = reserve_write_begin_stripe(inode, page->index);
	if (ret)
		return ret;
	get_exclusive_access(info);
	ret = do_write_begin_generic(file, page, pos, len,
				     readpage_stripe);
	if (unlikely(ret != 0))
		drop_exclusive_access(info);
	/*
	 * on success the acquired exclusive access
	 * will be dropped by ->write_end()
	 */
	return ret;
}

/**
 * implementation of ->write_end() address space operatio
 * for striped-file plugin
 */
int write_end_stripe(struct file *file, struct page *page,
		     loff_t pos, unsigned copied, void *fsdata)
{
	/*
	 * this will drop exclusive access
	 * acquired by write_begin_stripe()
	 */
	return reiser4_write_end_generic(file, page, pos, copied, fsdata,
					 find_or_create_extent_stripe);
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 80
 * scroll-step: 1
 * End:
 */
