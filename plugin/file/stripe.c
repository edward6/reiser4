/*
  Copyright (c) 2018 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * Stripe is a distribution logical unit in a file.
 * Every stripe, which got physical addresses, is composed of extents
 * (IO units), and every extent is a set of allocation units (file system
 * blocks) with contiguous disk addresses.
 * Neighboring extents of any two adjacent (in the logical order) stripes,
 * which got to the same device, get merged at the stripes boundary if
 * their physical addresses are adjusent.
 * In the storage tree extents are represented by extent pointers (items)
 * of EXTENT41_POINTER_ID. Extent pointer's key is calculated like for
 * classic unix files except the ordering component, which contains ID
 * of a brick (subvolume), where that extent should be stored. Holes in a
 * striped file are not represented by any items.
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

int reserve_stripe_meta(int count, int truncate);
int reserve_stripe_data(int count, reiser4_subvol *dsubv, int truncate);
int readpages_filler_generic(void *data, struct page *page, int striped);

/**
 * Return a pointer to data subvolume where
 * file's data at @offset should be stored
 */
reiser4_subvol *calc_data_subvol(const struct inode *inode, loff_t offset)
{
	reiser4_subvol *ret;
	lv_conf *conf;
	reiser4_volume *vol = current_volume();

	rcu_read_lock();
	conf = rcu_dereference(vol->conf);
	ret = conf_origin(conf, vol->vol_plug->data_subvol_id_calc(conf,
								   inode,
								   offset));
	rcu_read_unlock();
	return ret;
}

int build_body_key_stripe(struct inode *inode, loff_t off, reiser4_key *key)
{
	build_body_key_common(inode, key);
	set_key_ordering(key, KEY_ORDERING_MASK /* max value */);
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

static inline size_t write_granularity(void)
{
	if (current_stripe_bits) {
		int ret = 1 << (current_stripe_bits - current_blocksize_bits);
		if (ret > DEFAULT_WRITE_GRANULARITY)
			ret = DEFAULT_WRITE_GRANULARITY;
		return ret;
	} else
		return DEFAULT_WRITE_GRANULARITY;
}

ssize_t write_stripe(struct file *file,
		     const char __user *buf,
		     size_t count, loff_t *pos,
		     struct dispatch_context *cont)
{
	int ret;
	reiser4_context *ctx = get_current_context();
	struct inode *inode = file_inode(file);
	struct unix_file_info *uf_info;
	ssize_t written = 0;
	int to_write;
	int chunk_size = PAGE_SIZE * write_granularity();
	size_t left = count;
	int enospc = 0;

	assert("edward-2030", !reiser4_inode_get_flag(inode, REISER4_NO_SD));

	ret = file_remove_privs(file);
	if (ret) {
		context_set_commit_async(ctx);
		return ret;
	}
	/* remove_suid might create a transaction */
	reiser4_txn_restart(ctx);
	uf_info = unix_file_inode_data(inode);

	while (left) {
		int update_sd = 0;
		/*
		 * write not more then one logical chunk per iteration
		 */
		to_write = chunk_size - (*pos & (chunk_size - 1));
		if (left < to_write)
			to_write = left;

		get_nonexclusive_access(uf_info);
		written = write_extent_stripe(file, inode, buf, to_write,
					      pos);
		drop_nonexclusive_access(uf_info);

		if (written == -ENOSPC && !enospc) {
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
			ret = written;
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
			ret = reiser4_update_sd(inode);
			if (ret) {
				warning("edward-1574",
					"Can not update stat-data: %i. FSCK?",
					ret);
				context_set_commit_async(ctx);
				break;
			}
		}
		/*
		 * tell VM how many pages were dirtied. Maybe number of pages
		 * which were dirty already should not be counted
		 */
		reiser4_throttle_write(inode);
		left -= written;
		buf += written;
		*pos += written;
	}
	if (ret == 0 && ((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
		reiser4_txn_restart_current();
		grab_space_enable();
		ret = reiser4_sync_file_common(file, 0, LONG_MAX,
						  0 /* data and stat data */);
		if (ret)
			warning("edward-2367", "failed to sync file %llu",
				(unsigned long long)get_inode_oid(inode));
	}
	/*
	 * return number of written bytes or error code if nothing is
	 * written. Note, that it does not work correctly in case when
	 * sync_unix_file returns error
	 */
	return (count - left) ? (count - left) : ret;
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
		result = __reiser4_readpage_extent(NULL, NULL, 0, page);
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

#define CUT_TREE_MIN_ITERATIONS 64

int cut_tree_worker_stripe(tap_t *tap, const reiser4_key *from_key,
			   const reiser4_key *to_key,
			   reiser4_key *smallest_removed, struct inode *object,
			   int truncate, int *progress)
{
	int ret;
	coord_t left_coord;
	reiser4_key left_key;
	reiser4_key right_key;
	lock_handle next_node_lock;

	assert("edward-2287", tap->coord->node != NULL);
	assert("edward-2288", znode_is_write_locked(tap->coord->node));

	*progress = 0;
	init_lh(&next_node_lock);

	while (1) {
		znode *node = tap->coord->node;

		ret = reiser4_get_left_neighbor(&next_node_lock, node,
						ZNODE_WRITE_LOCK,
						GN_CAN_USE_UPPER_LEVELS);
		if (ret != 0 && ret != -E_NO_NEIGHBOR)
			break;
		ret = reiser4_tap_load(tap);
		if (ret)
			break;
		if (*progress)
			/* prepare right point */
			coord_init_last_unit(tap->coord, node);
		/* prepare left point */
		ret = node_plugin_by_node(node)->lookup(node, from_key,
							FIND_MAX_NOT_MORE_THAN,
							&left_coord);
		if (IS_CBKERR(ret))
			break;
		/*
		 * adjust coordinates so that they are set to existing units
		 */
		if (coord_set_to_right(&left_coord) ||
		    coord_set_to_left(tap->coord)) {
			ret = CBK_COORD_NOTFOUND;
			break;
		}
		if (coord_compare(&left_coord, tap->coord) ==
		    COORD_CMP_ON_RIGHT) {
			/* no keys of [from_key, @to_key] in the tree */
			ret = CBK_COORD_NOTFOUND;
			break;
		}
		/*
		 * Make keys precise.
		 * Set right_key to last byte of the item at tap->coord
		 */
		item_key_by_coord(tap->coord, &right_key);
		set_key_offset(&right_key,
			       get_key_offset(&right_key) +
			       reiser4_extent_size(tap->coord) - 1);
		assert("edward-2289",
		       get_key_offset(&right_key) <= get_key_offset(to_key));
		assert("edward-2290",
		       get_key_offset(from_key) <= get_key_offset(&right_key));
		/*
		 * @from_key may not exist in the tree
		 */
		unit_key_by_coord(&left_coord, &left_key);

		if (get_key_offset(&left_key) < get_key_offset(from_key))
			set_key_offset(&left_key, get_key_offset(from_key));

		/* cut data from one node */
		ret = kill_node_content(&left_coord, tap->coord,
					&left_key, &right_key,
					smallest_removed,
					next_node_lock.node /* left neighbor */,
					object, truncate);
		reiser4_tap_relse(tap);
		if (ret)
			break;
		(*progress)++;
		if (keyle(smallest_removed, from_key))
			break;
		if (next_node_lock.node == NULL)
			break;
		ret = reiser4_tap_move(tap, &next_node_lock);
		done_lh(&next_node_lock);
		if (ret)
			break;
		/* break long truncate if atom requires commit */

		if (*progress > CUT_TREE_MIN_ITERATIONS &&
		    current_atom_should_commit()) {
			ret = -E_REPEAT;
			break;
		}
	}
	done_lh(&next_node_lock);
	return ret;
}

/**
 * Cut body of a striped file
 */
static int cut_file_items_stripe(struct inode *inode, loff_t new_size,
				 int update_sd, loff_t cur_size,
				 reiser4_key *smallest_removed,
				 int (*update_size_fn) (struct inode *,
							loff_t, int))
{
	int ret = 0;
	reiser4_key from_key, to_key;
	reiser4_tree *tree = meta_subvol_tree();

	assert("edward-2021", inode_file_plugin(inode) ==
	       file_plugin_by_id(STRIPED_FILE_PLUGIN_ID));

	build_body_key_stripe(inode, cur_size - 1, &to_key);
	from_key = to_key;
	set_key_offset(&from_key, new_size);

	while (1) {
		int progress = 0;
		/*
		 * this takes sbinfo->delete_mutex
		 */
		ret = reserve_cut_iteration(inode);
		if (ret)
			return ret;

		ret = reiser4_cut_tree_object(tree,
					      &from_key, &to_key,
					      smallest_removed, inode,
					      1 /* truncate */, &progress);
		assert("edward-2291", ret != -E_NO_NEIGHBOR);

		if (ret == -E_REPEAT) {
			if (progress) {
				ret = update_size_fn(inode,
					 get_key_offset(smallest_removed),
						   update_sd);
				if (ret)
					break;
			}
			/* this releases sbinfo->delete_mutex */

			reiser4_release_reserved(inode->i_sb);
			reiser4_txn_restart_current();
			continue;
		} else if (ret == 0 || ret == CBK_COORD_NOTFOUND)
			ret = update_size_fn(inode, new_size, update_sd);
		break;
	}
	/* this releases sbinfo->delete_mutex */

	reiser4_release_reserved(inode->i_sb);
	return ret;
}

#if 0
static void check_partial_page_truncate(struct inode *inode,
					reiser4_key *smallest_removed)
{
	int ret;
	reiser4_key key;
	lock_handle lh;
	coord_t coord;

	init_lh(&lh);
	memcpy(&key, smallest_removed, sizeof(key));
	set_key_offset(&key, round_down(inode->i_size, PAGE_SIZE));

	ret = find_file_item_nohint(&coord, &lh, &key,
				    ZNODE_READ_LOCK, inode);
	done_lh(&lh);
	assert("edward-2369", ret == 0);
	assert("edward-2370", coord.between == AT_UNIT);
}
#endif

/**
 * Exclusive access to the file must be acquired
 */
static int shorten_stripe(struct inode *inode, loff_t new_size)
{
	int result;
	struct page *page;
	int padd_from;
	unsigned long index;
	reiser4_key smallest_removed;

	memcpy(&smallest_removed,
	       reiser4_max_key(), sizeof(smallest_removed));
	/*
	 * cut file body
	 */
	result = cut_file_items_stripe(inode, new_size,
				       1, /* update_sd */
				       get_key_offset(reiser4_max_key()),
				       &smallest_removed,
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
	if (get_key_offset(&smallest_removed) != new_size)
		/*
		 * the cut offset is in the logical block, which is
		 * not represented by a block pointer in the tree -
		 * there is no need to handle partial page truncate
		 */
		return 0;
	/*
	 * Handle partial page truncate
	 */
	//check_partial_page_truncate(inode, &smallest_removed);
	/*
	 * reserve space on meta-data brick
	 */
	result = reserve_stripe_meta(1, 1);
	if (result) {
		assert("edward-2295",
		       get_current_super_private()->delete_mutex_owner == NULL);
		return result;
	}
	/*
	 * reserve space on data brick
	 */
	result = reserve_stripe_data(1,
			current_origin(get_key_ordering(&smallest_removed)), 1);
	if (result)
		return result;
	/*
	 * zero content of partially truncated page
	 */
	index = (inode->i_size >> PAGE_SHIFT);

	page = read_mapping_page(inode->i_mapping, index, NULL);
	if (IS_ERR(page)) {
		reiser4_release_reserved(inode->i_sb);
		return PTR_ERR(page);
	}
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		put_page(page);
		reiser4_release_reserved(inode->i_sb);
		return RETERR(-EIO);
	}
	lock_page(page);
	assert("edward-2036", PageLocked(page));
	zero_user_segment(page, padd_from, PAGE_SIZE);
	unlock_page(page);

	READ_DIST_LOCK(inode);
	result = find_or_create_extent_stripe(page, 1 /* truncate */);
	READ_DIST_UNLOCK(inode);

	reiser4_release_reserved(inode->i_sb);
	put_page(page);
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
 * Capture one anonymous page.
 * Exclusive, or non-exclusive access to the file must be acquired.
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
	/*
	 * reserve space on meta-data brick
	 */
	ret = reserve_stripe_meta(1, 0);
	if (ret)
		return ret;
	READ_DIST_LOCK(inode);
	ret = find_or_create_extent_stripe(page, 0);
	READ_DIST_UNLOCK(inode);
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
 * implementation of ->write_begin() address space operation
 * for striped-file plugin
 */
int write_begin_stripe(struct file *file, struct page *page,
		       loff_t pos, unsigned len, void **fsdata)
{
	int ret;
	/*
	 * Reserve space on meta-data brick.
	 * In particular, it is needed to "drill" the leaf level
	 * by search procedure.
	 */
	if (reserve_stripe_meta(1, 0))
		return -ENOMEM;

	get_nonexclusive_access(unix_file_inode_data(file_inode(file)));
	ret = do_write_begin_generic(file, page, pos, len,
				     readpage_stripe);
	if (unlikely(ret != 0))
		drop_nonexclusive_access(unix_file_inode_data(file_inode(file)));
	return ret;
}

/**
 * implementation of ->write_end() address space operatio
 * for striped-file plugin
 */
int write_end_stripe(struct file *file, struct page *page,
		     loff_t pos, unsigned copied, void *fsdata)
{
	int ret;
	ret = reiser4_write_end_generic(file, page, pos, copied, fsdata,
					find_or_create_extent_stripe);
	drop_nonexclusive_access(unix_file_inode_data(file_inode(file)));
	return ret;
}

/**
 * Scan file body from right to left, read all data blocks which get
 * new location, and make respective pages dirty. In flush time those
 * pages will get location on new bricks.
 *
 * Exclusive access to the file should be acquired by caller.
 *
 * IMPORTANT: This implementation assumes that physical order of file
 * data bytes coincides with their logical order.
 */
int balance_stripe(struct inode *inode)
{
	int ret;
	reiser4_key key;
	struct unix_file_info *uf;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

	uf = unix_file_inode_data(inode);

	reiser4_inode_set_flag(inode, REISER4_FILE_UNBALANCED);

	build_body_key_stripe(inode, get_key_offset(reiser4_max_key()),
			      &key);
	while (1) {
		znode *loaded;
		loff_t done_off;

		init_lh(&lh);
		ret = coord_by_key(meta_subvol_tree(), &key,
				   &coord, &lh, ZNODE_WRITE_LOCK,
				   FIND_MAX_NOT_MORE_THAN,
				   TWIG_LEVEL, TWIG_LEVEL,
				   CBK_UNIQUE, NULL);
		if (IS_CBKERR(ret)) {
			done_lh(&lh);
			return ret;
		}
		ret = zload(coord.node);
		if (ret) {
			done_lh(&lh);
			return ret;
		}
		loaded = coord.node;

		coord_set_to_left(&coord);
		if (!coord_is_existing_item(&coord)) {
			/*
			 * nothing to migrate
			 */
			zrelse(loaded);
			goto done;
		}
		/*
		 * check that found item belongs to the file
		 */
		if (!inode_file_plugin(inode)->owns_item(inode, &coord)) {
			zrelse(loaded);
			goto done;
		}
		iplug = item_plugin_by_coord(&coord);
		assert("edward-2349", iplug->v.migrate != NULL);
		/*
		 * Migrate data blocks (from right to left) pointed
		 * out by the found extent item.
		 * On success (ret == 0 || ret == -E_REPEAT) at least
		 * one of the mentioned blocks has to be migrated. In
		 * this case @done_off contains offset of the leftmost
		 * migrated byte
		 */
		ret = iplug->v.migrate(&coord, &lh, inode, &done_off);
		zrelse(loaded);
		done_lh(&lh);
		if (ret && ret != -E_REPEAT)
			return ret;
		/*
		 * FIXME: commit atom not after each ->migrate() call,
		 * but after reaching some limit of processed blocks
		 */
		all_grabbed2free();

		if (done_off == 0)
			/* nothing to migrate any more */
			break;
		/*
		 * look for the item, which points out to the
		 * rightmost not processed block
		 */
		set_key_offset(&key, done_off - 1);
	}
 done:
	/*
	 * The whole file has been successfully migrated.
	 * Clean up unbalanced status
	 */
	assert("edward-2104", reiser4_lock_counters()->d_refs == 0);
	done_lh(&lh);
	reiser4_inode_clr_flag(inode, REISER4_FILE_UNBALANCED);
	return 0;
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
