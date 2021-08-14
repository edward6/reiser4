/*
  Copyright (c) 2018-2020 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * Implementation of regular files with distributed bodies.
 *
 * Logical unit of distribution in such file is called "stripe".
 * Every stripe, which got physical addresses, is composed of extents
 * (IO units), and every extent is a set of filesystem blocks (allocation
 * units) with contiguous disk addresses.
 * Neighboring extents of any two adjacent (in the logical order) stripes,
 * which got to the same device, get merged at the stripe boundary if
 * their physical addresses are adjusent.
 * In the storage tree extents are represented by extent pointers (items)
 * of EXTENT41_POINTER_ID. Extent pointer's key is calculated like for
 * classic unix files (UNIX_FILE_PLUGIN_ID) except the ordering component,
 * which in our case contains ID of a brick (subvolume), where that extent
 * should be stored in.
 * Holes in a striped file are not represented by any items.
 */

#include "../../inode.h"
#include "../../super.h"
#include "../../tree_walk.h"
#include "../../carry.h"
#include "../../page_cache.h"
#include "../object.h"
#include "../cluster.h"
#include "../../safe_link.h"
#include "../volume/volume.h"

#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/syscalls.h>

reiser4_block_nr estimate_migration_iter(void);
reiser4_block_nr estimate_write_stripe_meta(int count);
int readpages_filler_generic(void *data, struct page *page, int striped);

static inline reiser4_extent *ext_by_offset(const znode *node, int offset)
{
	reiser4_extent *ext;

	ext = (reiser4_extent *) (zdata(node) + offset);
	return ext;
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

/*
 * Tree search with a sealing technique for striped files
 *
 * To save CPU resources, every time before releasing a longterm lock
 * we "seal" a position in the tree, which represents an existing object.
 * Next time when we want to lock a positon in the tree, we check the seal.
 * If it is unbroken, and it was created for a suitable object, we don't
 * perform an expensive tree traversal. Instead, we lock the "sealed" node
 * and perform fast lookup from the "sealed" position.
 */

#if REISER4_DEBUG

static inline int equal_to_ldk_nonprec(znode *node, const reiser4_key *key)
{
	int ret;

	read_lock_dk(znode_get_tree(node));
	ret = all_but_ordering_keyeq(key, znode_get_ld_key(node));
	read_unlock_dk(znode_get_tree(node));
	return ret;
}
#endif

static inline int equal_to_rdk_nonprec(znode *node, const reiser4_key *key)
{
	int ret;

	read_lock_dk(znode_get_tree(node));
	ret = all_but_ordering_keyeq(key, znode_get_rd_key(node));
	read_unlock_dk(znode_get_tree(node));
	return ret;
}

/**
 * Check if the seal was created against the previous block pointer.
 * And if so, then validate it.
 */
static int hint_validate(hint_t *hint, reiser4_tree *tree,
			 const reiser4_key *key, znode_lock_mode lock_mode)
{
	reiser4_key vkey;

	if (!hint || !hint_is_set(hint) || hint->mode != lock_mode ||
	    get_key_offset(key) != hint->offset + PAGE_SIZE)
		return RETERR(-E_REPEAT);

	assert("edward-2377", hint->ext_coord.lh == &hint->lh);

	memcpy(&vkey, key, sizeof(vkey));
	set_key_offset(&vkey, hint->offset);

	return reiser4_seal_validate(&hint->seal, tree,
				     &hint->ext_coord.coord, &vkey,
				     hint->ext_coord.lh, lock_mode,
				     ZNODE_LOCK_LOPRI);
}

/**
 * Search-by-key procedure optimized for sequential operations
 * by using "sealing" technique. Subtle!
 *
 * @key: key of a block pointer we are looking for. That key is
 * not precise, that is we don't know its "ordering" component.
 */
int find_stripe_item(hint_t *hint, const reiser4_key *key,
		     znode_lock_mode lock_mode, struct inode *inode)
{
	int ret;
	coord_t *coord;
	coord_t rcoord;
	lock_handle *lh;
	struct extent_coord_extension *ext_coord;

	assert("edward-2378", hint != NULL);
	assert("edward-2379", inode != NULL);
	assert("edward-2380", reiser4_schedulable());
	assert("edward-2381", (get_key_offset(key) & (PAGE_SIZE - 1)) == 0);
	assert("edward-2382", inode_file_plugin(inode) ==
	       file_plugin_by_id(STRIPED_FILE_PLUGIN_ID));

	coord = &hint->ext_coord.coord;
	lh = hint->ext_coord.lh;
	init_lh(lh);

	ret = hint_validate(hint, meta_subvol_tree(), key, lock_mode);
	if (ret)
		/*  improper, or broken seal */
		goto nohint;
	assert("edward-2385",
	       WITH_DATA(coord->node, coord_is_existing_unit(coord)));
	/*
	 * We sealed valid coord extension, which represents
	 * existing block pointer. Respectively, if the seal
	 * is unbroken, then it remains to be valid
	 */
	hint->ext_coord.valid = 1;
	ext_coord = &hint->ext_coord.extension.extent;

	/* fast lookup against the sealed coord extension */

	ext_coord->pos_in_unit ++;
	if (ext_coord->pos_in_unit < ext_coord->width)
		/*
		 * found within the unit specified by @coord
		 */
		return CBK_COORD_FOUND;
	/*
	 * end of unit is reached. Try to move to next unit
	 */
	ext_coord->pos_in_unit = 0;
	coord->unit_pos ++;
	if (coord->unit_pos < ext_coord->nr_units) {
		/*
		 * found within next unit. Update coord extension
		 */
		ext_coord->ext_offset += sizeof(reiser4_extent);
		ext_coord->width =
			extent_get_width(ext_by_offset(coord->node,
						       ext_coord->ext_offset));
		ON_DEBUG(ext_coord->extent =
			 *ext_by_offset(coord->node, ext_coord->ext_offset));
		return CBK_COORD_FOUND;
	}
	/*
	 * end of item reached. Try to find in the next item at the right
	 */
	coord->unit_pos --;
	coord->between = AFTER_UNIT;
	hint->ext_coord.valid = 0; /* moving to the next item invalidates
				      the coord extension */
	ret = zload(lh->node);
	if (ret) {
		done_lh(lh);
		return ret;
	}
	coord_dup(&rcoord, coord);
	if (!coord_next_item(&rcoord)) {
		/*
		 * rcoord is set to next item
		 */
		reiser4_key rkey;
		if (!item_is_extent(&rcoord) ||
		    !all_but_ordering_keyeq(key,
				item_key_by_coord(&rcoord, &rkey))) {
			zrelse(lh->node);
			assert("edward-2386", coord->between == AFTER_UNIT);
			return CBK_COORD_NOTFOUND;
		}
		coord_dup(coord, &rcoord);
		zrelse(lh->node);
		coord->between = AT_UNIT;
		return CBK_COORD_FOUND;
	}
	zrelse(lh->node);
	/*
	 * end of node reached. Try to find in the next node at the right
	 */
	if (equal_to_rdk_nonprec(coord->node, key)) {
		ret = goto_right_neighbor(coord, lh);
		if (unlikely(ret)) {
			done_lh(lh);
			assert("edward-2387", ret != CBK_COORD_NOTFOUND);
			return ret == -E_NO_NEIGHBOR ? RETERR(-EIO) : ret;
		}
		ret = zload(lh->node);
		if (unlikely(ret)) {
			done_lh(lh);
			return ret;
		}
		if (unlikely(node_is_empty(coord->node))) {
			/*
			 * for simplicity we don't go further to
			 * the right. Instead we call slow lookup.
			 */
			zrelse(lh->node);
			done_lh(lh);
			goto nohint;
		}
		/*
		 * it is guaranteed that the first item at the
		 * right neighbor has @key, because exclusive, or
		 * non-exclusive lock of the file is held by us.
		 */
		assert("edward-2384", equal_to_ldk_nonprec(coord->node, key));
		zrelse(lh->node);
		assert("edward-2388", coord->between == AT_UNIT);
		return CBK_COORD_FOUND;
	}
	assert("edward-2389", coord->between == AFTER_UNIT);
	return CBK_COORD_NOTFOUND;
 nohint:
	/* full-fledged lookup */
	coord_init_zero(coord);
	hint->ext_coord.valid = 0;
	return find_file_item_nohint(coord, lh, key, lock_mode, inode);
}

ssize_t read_iter_stripe(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t result;
	struct inode *inode;
	reiser4_context *ctx;
	struct unix_file_info *uf_info;

	if (unlikely(iov_iter_count(iter) == 0))
		return 0;

	inode = file_inode(iocb->ki_filp);
	assert("edward-2029", !reiser4_inode_get_flag(inode, REISER4_NO_SD));

	ctx = reiser4_init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	result = reserve_update_sd_common(inode);
	if (unlikely(result != 0))
		goto out;
	uf_info = unix_file_inode_data(inode);

	get_nonexclusive_access(uf_info);
	result = generic_file_read_iter(iocb, iter);
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

static inline ssize_t write_extent_stripe_handle_enospc(struct file *file,
							struct inode *inode,
							const char __user *buf,
							size_t count,
							loff_t *pos)
{
	int ret;
	struct unix_file_info *uf_info = unix_file_inode_data(inode);

	get_nonexclusive_access(uf_info);
	ret = write_extent_stripe(file, inode, buf, count, pos, 0);
	if (ret == -ENOSPC) {
		drop_nonexclusive_access(uf_info);
		txnmgr_force_commit_all(inode->i_sb, 0);
		get_nonexclusive_access(uf_info);
		ret = write_extent_stripe(file, inode, buf, count, pos, 0);
		if (ret == -ENOSPC &&
		    reiser4_is_set(reiser4_get_current_sb(),
				   REISER4_PROXY_IO)) {
			drop_nonexclusive_access(uf_info);
			reiser4_txn_restart_current();
			get_nonexclusive_access(uf_info);
			ret = write_extent_stripe(file, inode, buf, count, pos,
						  UPX_PROXY_FULL);
			if (0 && ret == -ENOSPC) {
				drop_nonexclusive_access(uf_info);
				txnmgr_force_commit_all(inode->i_sb, 0);
				get_nonexclusive_access(uf_info);
				ret = write_extent_stripe(file, inode, buf,
							  count, pos,
							  UPX_PROXY_FULL);
			}
		}
	}
	drop_nonexclusive_access(uf_info);
	return ret;
}

#if 0
ssize_t write_stripe(struct file *file,
		     const char __user *buf,
		     size_t count, loff_t *pos,
		     struct dispatch_context *cont)
{
	int ret;
	reiser4_context *ctx = get_current_context();
	struct inode *inode = file_inode(file);
	ssize_t written = 0;
	int to_write;
	int chunk_size = PAGE_SIZE * write_granularity();
	size_t left = count;

	assert("edward-2030", !reiser4_inode_get_flag(inode, REISER4_NO_SD));

	ret = file_remove_privs(file);
	if (ret) {
		context_set_commit_async(ctx);
		return ret;
	}
	/* remove_suid might create a transaction */
	reiser4_txn_restart(ctx);

	while (left) {
		int update_sd = 0;
		/*
		 * write not more then one logical chunk per iteration
		 */
		to_write = chunk_size - (*pos & (chunk_size - 1));
		if (left < to_write)
			to_write = left;

		written = write_extent_stripe_handle_enospc(file, inode, buf,
							    to_write, pos);
		if (written < 0)
			break;
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
#endif

ssize_t write_iter_stripe(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret;
	struct hint hint;
	struct inode *inode = file_inode(iocb->ki_filp);
	reiser4_context *ctx;

	ctx = reiser4_init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	/*
	 * reserve space on meta-data brick for stat-data update (mtime/ctime)
	 */
	ret = reiser4_grab_space_force(estimate_update_common(inode),
				       BA_CAN_COMMIT, get_meta_subvol());
	if (ret)
		goto out;

	ret = load_file_hint(iocb->ki_filp, &hint);
	if (ret)
		return ret;
	ctx->hint = &hint;

	ret = generic_file_write_iter(iocb, from);
	if (ret > 0)
		save_file_hint(iocb->ki_filp, &hint);
 out:
	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);
	return ret;
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
	hint_init_zero(hint);
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
	if (result)
		unlock_page(page);

	assert("edward-2034",
	       ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
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

#if REISER4_DEBUG
static void check_truncate_jnodes(struct inode *inode, pgoff_t start)
{
	int ret;
	jnode *node = NULL;

	read_lock_tree();
	ret = radix_tree_gang_lookup(jnode_tree_by_reiser4_inode(reiser4_inode_data(inode)),
				     (void **)node, start, 1);
	read_unlock_tree();
	if (ret)
		warning("edward-2467", "found jnode index=%lu, file_size=%llu",
			index_jnode(node), inode->i_size);
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
	struct hint hint;
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
	ON_DEBUG(check_truncate_jnodes(inode,
			        round_up(new_size, PAGE_SIZE) >> PAGE_SHIFT));

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
	 * Handle partial page truncate.
	 * Reserve space on meta-data brick
	 */
	grab_space_enable();
	result = reiser4_grab_reserved(reiser4_get_current_sb(),
				       estimate_write_stripe_meta(1),
				       BA_CAN_COMMIT,
				       get_meta_subvol());
	if (result) {
		assert("edward-2295",
		       get_current_super_private()->delete_mutex_owner == NULL);
		return result;
	}
	/*
	 * reserve space on data brick, where the partially
	 * trunated page should be stored
	 */
	grab_space_enable();
	result = reiser4_grab_reserved(reiser4_get_current_sb(),
				       1, /* count */
				       BA_CAN_COMMIT,
				       subvol_by_key(&smallest_removed));
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

	hint_init_zero(&hint);
	result = find_or_create_extent_stripe(page, &hint, UPX_TRUNCATE);

	reiser4_release_reserved(inode->i_sb);
	put_page(page);
	return result;
}

static int truncate_body_stripe(struct inode *inode, struct iattr *attr)
{
	loff_t new_size = attr->ia_size;

	if (inode->i_size < new_size) {
		/* expand */
		return reiser4_update_file_size(inode, new_size, 1);
	} else if (inode->i_size > new_size)
		/* shrink */
		return shorten_stripe(inode, new_size);
	return 0;
}

int setattr_stripe(struct user_namespace *mnt_userns,
		   struct dentry *dentry, struct iattr *attr)
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
	struct hint hint;

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
	grab_space_enable();
	ret = reiser4_grab_space(estimate_write_stripe_meta(1),
				 0, /* flags */
				 get_meta_subvol() /* where */);
	if (ret)
		return ret;

	hint_init_zero(&hint);
	ret = find_or_create_extent_stripe(page, &hint, 0);
	if (ret == -ENOSPC &&
	    reiser4_is_set(reiser4_get_current_sb(), REISER4_PROXY_IO))
		ret = find_or_create_extent_stripe(page, &hint, UPX_PROXY_FULL);
	if (ret) {
		SetPageError(page);
		warning("edward-2046",
			"Failed to capture anon page of striped file: %i", ret);
	} else
		ret = 1;
	return ret;
}

int sync_jnode(jnode *node)
{
	int result;

	assert("edward-2452", node != NULL);
	assert("edward-2453", get_current_context() != NULL);
	assert("edward-2454", get_current_context()->trans != NULL);

	do {
		txn_atom *atom;

		spin_lock_jnode(node);
		atom = jnode_get_atom(node);
		spin_unlock_jnode(node);
		result = reiser4_sync_atom(atom);

	} while (result == -E_REPEAT);

	assert("edward-2455",
	       ergo(result == 0,
		    get_current_context()->trans->atom == NULL));
	return result;
}

int sync_jnode_list(struct inode *inode)
{
	int result = 0;
	unsigned long from;	/* start index for radix_tree_gang_lookup */
	unsigned int found;	/* return value for radix_tree_gang_lookup */

	from = 0;
	read_lock_tree();
	while (result == 0) {
		jnode *node = NULL;

		found = radix_tree_gang_lookup(jnode_tree_by_inode(inode),
					       (void **)&node, from, 1);
		if (found == 0)
			break;
		assert("edward-2456", node != NULL);
		/**
		 * node may not leave radix tree because it is
		 * protected from truncating by exclusive lock
		 */
		jref(node);
		read_unlock_tree();

		from = node->key.j.index + 1;

		result = sync_jnode(node);

		jput(node);
		read_lock_tree();
	}
	read_unlock_tree();
	return result;
}

static int commit_stripe_atoms(struct inode *inode)
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
					  commit_stripe_atoms);
}

long int ioctl_stripe(struct file *filp, unsigned int cmd,  unsigned long arg)
{
	return reiser4_ioctl_volume(filp, cmd, arg, reiser4_volume_op_file);
}

/**
 * implementation of ->write_begin() address space operation
 * for striped-file plugin
 */
int write_begin_stripe(struct file *file,
		       struct address_space *mapping,
		       loff_t pos,
		       unsigned len,
		       unsigned flags,
		       struct page **pagep,
		       void **fsdata)
{
	int ret;

	assert("edward-2479", is_in_reiser4_context());

	*pagep = grab_cache_page_write_begin(mapping, pos >> PAGE_SHIFT,
					     flags & AOP_FLAG_NOFS);
	if (!*pagep)
		return -ENOMEM;
	/*
	 * Reserve space on meta-data brick for stat-data update (file size)
	 * and for "drilling" the leaf level by search procedure
	 *
	 * FIXME: striped file plugin doesn't drill the leaf level?
	 */
	grab_space_enable();
	ret = reiser4_grab_space(estimate_update_common(file_inode(file)) +
				 estimate_write_stripe_meta(1),
				 BA_CAN_COMMIT,
				 get_meta_subvol());
	if (ret)
		return RETERR(ret);

	ret = reiser4_write_begin_common(file, *pagep, pos, len,
					 readpage_stripe);
	if (unlikely(ret)) {
		unlock_page(*pagep);
		put_page(*pagep);
	}
	return ret;
}

/**
 * Implementation of ->write_end() address space operation
 * for striped-file plugin
 */
int write_end_stripe(struct file *file,
		     struct address_space *mapping,
		     loff_t pos,
		     unsigned len,
		     unsigned copied,
		     struct page *page,
		     void *fsdata)
{
	int ret;
	struct inode *inode = file_inode(file);
	struct unix_file_info *info = unix_file_inode_data(inode);
	reiser4_context *ctx = get_current_context();
	struct hint *hint = ctx->hint;

	SetPageUptodate(page);
	set_page_dirty_notag(page);
	unlock_page(page);

	reiser4_txn_restart(ctx);
	get_nonexclusive_access(info);
	ret = find_or_create_extent_stripe(page, hint, 0);
	drop_nonexclusive_access(info);
	if (ret == -ENOSPC) {
		txnmgr_force_commit_all(inode->i_sb, 0);
		get_nonexclusive_access(info);
		ret = find_or_create_extent_stripe(page, hint, 0);
		if (ret == -ENOSPC && reiser4_is_set(reiser4_get_current_sb(),
						     REISER4_PROXY_IO)) {
			/*
			 * proxy disk is full, write to permanent location
			 */
			ret = find_or_create_extent_stripe(page, hint,
							   UPX_PROXY_FULL);
			if (ret == -ENOSPC) {
				drop_nonexclusive_access(info);
				txnmgr_force_commit_all(inode->i_sb, 0);
				get_nonexclusive_access(info);
				ret = find_or_create_extent_stripe(page, hint,
							      UPX_PROXY_FULL);
			}
		}
		drop_nonexclusive_access(info);
	}
	if (ret) {
		SetPageError(page);
		goto out;
	}
	if (pos + copied > inode->i_size) {
		INODE_SET_FIELD(inode, i_size, pos + copied);
		ret = reiser4_update_sd(inode);
		if (unlikely(ret != 0))
			warning("edward-2431",
				"Can not update stat-data: %i. FSCK?",
				ret);
	}
 out:
	put_page(page);
	return ret == 0 ? copied : ret;
}

/**
 * Migrate data blocks of a regular file specified by @inode
 * Exclusive access to the file should be acquired by caller.
 *
 * Implementation details:
 * Scan file body from right to left, read all pages which should
 * get location on other bricks, and make them dirty. In flush time
 * those pages will get disk addresses on the new bricks.
 *
 * IMPORTANT: This implementation assumes that logical order on
 * the file coincides with the physical order.
 */
static int __migrate_stripe(struct inode *inode, u64 *dst_id)
{
	int ret;
	reiser4_key key; /* search key */
	reiser4_key ikey; /* item key */
	struct unix_file_info *uf;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

	/*
	 * commit all file atoms before migration!
	 */
	reiser4_txn_restart_current();
	ret = sync_jnode_list(inode);
	reiser4_txn_restart_current();
	if (ret)
		return ret;
	all_grabbed2free();
	/*
	 * Reserve space for the first iteration of the migration
	 * procedure. We grab from reserved area, as rebalancing can
	 * be launched on a volume with no free space.
	 */
	ret = reserve_migration_iter();
	if (ret)
		return ret;
	uf = unix_file_inode_data(inode);

	reiser4_inode_set_flag(inode, REISER4_FILE_IN_MIGRATION);

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
			reiser4_release_reserved(inode->i_sb);
			return ret;
		}
		ret = zload(coord.node);
		if (ret) {
			done_lh(&lh);
			reiser4_release_reserved(inode->i_sb);
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
		item_key_by_coord(&coord, &ikey);
		iplug = item_plugin_by_coord(&coord);
		assert("edward-2349", iplug->v.migrate != NULL);
		zrelse(loaded);
		/*
		 * Migrate data blocks (from right to left) pointed
		 * out by the found extent item at @coord.
		 * On success (ret == 0 || ret == -E_REPEAT) at least
		 * one of the mentioned blocks has to be migrated. In
		 * this case @done_off contains offset of the leftmost
		 * migrated byte
		 */
		ret = iplug->v.migrate(&coord, &ikey, &lh, inode, &done_off,
				       dst_id);
		done_lh(&lh);
		reiser4_release_reserved(inode->i_sb);
		if (ret && ret != -E_REPEAT)
			return ret;
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
	reiser4_inode_clr_flag(inode, REISER4_FILE_IN_MIGRATION);
	return 0;
}

int migrate_stripe(struct inode *inode, u64 *dst_id)
{
	int ret;

	get_exclusive_access(unix_file_inode_data(inode));
	ret = __migrate_stripe(inode, dst_id);
	drop_exclusive_access(unix_file_inode_data(inode));
	return ret;
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
