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

void inode_set_new_dist(struct inode *inode);
void inode_set_old_dist(struct inode *inode);

/**
 * Scan file body from right to left, read all data blocks which get
 * new location, and make respective pages dirty. In flush time those
 * pages will get location on new bricks.
 *
 * IMPORTANT: This implementation assumes that all items of the same
 * file are next to each other in the storage tree. Currently, it is
 * achievable only with PLANB_KEY_ALLOCATION scheme. If you need to
 * balance in other key allocation modes, then write another
 * implementation, put it here and ifdef it.
 */
int balance_stripe(struct inode *inode)
{
	int ret;
	reiser4_key key;
	struct unix_file_info *uf;
	reiser4_volume *vol;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

	/* see the comment above */
	assert("edward-2144", REISER4_PLANB_KEY_ALLOCATION);

	if (inode->i_size == 0)
		/*
		 * empty file, nothing to migrate
		 */
		return 0;

	vol = current_volume();
	uf = unix_file_inode_data(inode);

	get_exclusive_access(uf);
	reiser4_inode_set_flag(inode, REISER4_FILE_UNBALANCED);

	build_body_key_common(inode, &key);
	set_key_ordering(&key, KEY_ORDERING_MASK /* max value */);
	set_key_offset(&key, get_key_offset(reiser4_max_key()));

	while (1) {
		znode *loaded;

		init_lh(&lh);
		ret = coord_by_key(meta_subvol_tree(), &key,
				   &coord, &lh, ZNODE_WRITE_LOCK,
				   FIND_MAX_NOT_MORE_THAN,
				   TWIG_LEVEL, TWIG_LEVEL,
				   0, NULL);
		if (IS_CBKERR(ret)) {
			done_lh(&lh);
			drop_exclusive_access(uf);
			return ret;
		}
		ret = zload(coord.node);
		if (ret) {
			done_lh(&lh);
			drop_exclusive_access(uf);
			return ret;
		}
		loaded = coord.node;

		coord_set_to_left(&coord);
		assert("edward-2103", coord_is_existing_item(&coord));
		/*
		 * check that found item belongs to the file
		 */
		if (!inode_file_plugin(inode)->owns_item(inode, &coord)) {
			zrelse(loaded);
			goto done;
		}
		/* make key precise */
		item_key_by_coord(&coord, &key);

		iplug = item_plugin_by_coord(&coord);
		if (iplug->v.migrate) {
			/*
			 * relocate data blocks pointed by found item,
			 * return amount of processed bytes on success
			 */
			ret = iplug->v.migrate(&coord, &lh, inode);
			zrelse(loaded);
			done_lh(&lh);
			drop_exclusive_access(uf);
			if (ret && ret != -E_REPEAT)
				break;
			/* commit atom */
			all_grabbed2free();
			get_exclusive_access(uf);

			if (ret == -E_REPEAT)
				/* continue with the same key */
				continue;
		} else {
			/*
			 * item is not migrateable, simply skip it
			 */
			zrelse(loaded);
			done_lh(&lh);
		}
		if (get_key_offset(&key) == 0)
			/*
			 * nothing to migrate any more
			 */
			break;
		/*
		 * look for the next item at the left
		 */
		set_key_offset(&key, get_key_offset(&key) - 1);
	}
 done:
	assert("edward-2104", reiser4_lock_counters()->d_refs == 0);
	reiser4_inode_clr_flag(inode, REISER4_FILE_UNBALANCED);
	inode_set_new_dist(inode);
	/* FIXME-EDWARD: update stat-data with new volinfo ID */
	drop_exclusive_access(uf);
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
