/*
  Copyright (c) 2017 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "../../inode.h"
#include "../../super.h"
#include "../../page_cache.h"

/*
 * Per-iteration migration context
 */
struct iter_migrate_ctx {
	item_plugin *iplug;
	struct inode *inode;
	loff_t stripe_off;
	u64 new_subvol_id;
};

/**
 * check an item specified by @coord,
 * return:
 * 1, iff the item should be migrated;
 * 0, if iteration should be continued;
 * -E_OUTSTEP, if iteration should be terminated.
 */
static int check_stripe_item(reiser4_tree *tree, coord_t *coord,
			     lock_handle *lh, void *arg)
{
	reiser4_key key;
	struct iter_migrate_ctx *mctx = arg;

	if (mctx->iplug != item_plugin_by_coord(coord)) {
		assert("edward-1892", item_is_internal(coord));
		return 0;
	}
	item_key_by_coord(coord, &key);
	assert("edward-1893", get_key_offset(&key) == mctx->stripe_off);

	if (subvol_by_coord(coord)->id != mctx->new_subvol_id)
		/*
		 * item should be migrated
		 */
		return 1;
	/*
	 * migration is not needed
	 */
	mctx->iplug->s.file.append_key(coord, &key);

	if (get_key_offset(&key) != mctx->stripe_off)
		/*
		 * @coord is the last item in the stripe,
		 * terminate iteration
		 */
		return -E_OUTSTEP;
	/*
	 * next item also belongs to this stripe, continue iteration
	 */
	return 0;
}

/**
 * @stripe_idx: index of the stripe to be migrated
 */
static int migrate_data_stripe(struct inode *inode, pgoff_t stripe_off)
{
	int ret;
	int done = 0;
	reiser4_key key;
	struct unix_file_info *uf;

	uf = unix_file_inode_data(inode);
	key_by_inode_and_offset(inode, stripe_off, &key);
	set_key_ordering(&key, KEY_ORDERING_MASK);
	/*
	 * FIXME: Implement read-ahead per stripe base
	 */
	while (!done) {
		coord_t coord;
		lock_handle lh;
		item_plugin *iplug;
		reiser4_key append_key;
		struct iter_migrate_ctx mctx;

		init_lh(&lh);
		get_exclusive_access(uf);
		reiser4_inode_set_flag(inode, REISER4_FILE_BALANCE_IN_PROGRESS);
		/*
		 * find first non-migrated item of the stripe
		 */
		ret = find_file_item_nohint(&coord, &lh, &key,
					    ZNODE_WRITE_LOCK, inode);
		if (ret != CBK_COORD_FOUND) {
			/*
			 * no items in this stripe
			 */
			done_lh(&lh);
			drop_exclusive_access(uf);
			return ret;
		}
		assert("edward-1894", item_is_extent(&coord));

		mctx.inode = inode;
		mctx.stripe_off = stripe_off;
		mctx.iplug = item_plugin_by_coord(&coord);
		mctx.new_subvol_id =
			current_vol_plug()->data_subvol_id(get_inode_oid(inode),
							   stripe_off);

		ret = reiser4_iterate_tree(meta_subvol_tree(), &coord,
					   &lh, check_stripe_item,
					   &mctx, ZNODE_WRITE_LOCK, 0);

		if (ret == -E_NO_NEIGHBOR || ret == -E_OUTSTEP) {
			/*
			 * nothing to migrate in this stripe
			 */
			ret = 0;
			done_lh(&lh);
			drop_exclusive_access(uf);
			break;
		}
		else if (ret < 0) {
			done_lh(&lh);
			drop_exclusive_access(uf);
			break;
		}
		iplug = item_plugin_by_coord(&coord);

		iplug->s.file.append_key(&coord, &append_key);
		if (get_key_offset(&append_key) != stripe_off)
			/*
			 * last item in the stripe
			 */
			done = 1;
		/*
		 * migrate data pages in asynchronos manner
		 */
		if (iplug->s.vol.migrate)
			ret = iplug->s.vol.migrate(&coord, &lh,
						   inode, mctx.new_subvol_id);
		done_lh(&lh);
		reiser4_inode_clr_flag(inode, REISER4_FILE_BALANCE_IN_PROGRESS);
		drop_exclusive_access(uf);
		if (ret)
			break;
		reiser4_throttle_write(inode);
	}
	return ret;
}

/*
 * Migrate one-by-one all data stripes of the file
 */
int balance_unix_file(struct inode *inode)
{
	int ret;
	pgoff_t stripe_idx;

	for (stripe_idx = 0;; stripe_idx ++) {
		/*
		 * migrate data stripe in asynchronos manner
		 */
		ret = migrate_data_stripe(inode,
					  stripe_idx >> current_stripe_bits);
		if (ret)
			break;
	}
	if (ret == CBK_COORD_NOTFOUND)
		ret = 0;
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
