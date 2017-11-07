/*
  Copyright (c) 2017 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "item.h"
#include "../../inode.h"
#include "../../page_cache.h"
#include "../object.h"

u64 find_data_subvol_extent(const coord_t *coord)
{
	reiser4_key key;

	item_key_by_coord(coord, &key);
	return get_key_ordering(&key);
}

static int filler(void *data, struct page *page)
{
	coord_t *coord = data;

	return reiser4_do_readpage_extent(extent_by_coord(coord), 0, page);
}

/**
 * "Cut off" one block from the beginning of extent
 *
 * @coord: coordinate of the extent item to cut from
 */
static int cut_off_head_block(coord_t *coord)
{
	reiser4_key from;
	reiser4_key to;

	item_key_by_coord(coord, &from);
	memcpy(&from, &to, sizeof(from));
	set_key_offset(&to, get_key_offset(&from) + PAGE_SIZE - 1);

	return cut_node_content(coord, coord, &from, &to, NULL);
}

int append_hole(coord_t *coord, lock_handle *lh, const reiser4_key *key);

/*
 * Migrate one block of data pointed by hole extent
 */
static int migrate_hole_extent(coord_t *coord, lock_handle *lh,
			       struct inode *inode, u64 new_subv_id)
{
	int ret;
	reiser4_key key;

	item_key_by_coord(coord, &key);

	ret = cut_off_head_block(coord);
	done_lh(lh);
	if (ret)
		return ret;
	ret = find_file_item_nohint(coord, lh, &key,
				    ZNODE_WRITE_LOCK, inode);
	if (IS_CBKERR(ret))
		return ret;
	return append_hole(coord, lh, &key);
}

/*
 * Migrate one block of data pointed by allocated extent
 */
static int migrate_allocated_extent(coord_t *coord, lock_handle *lh,
				    struct inode *inode, u64 new_subv_id)
{
	int ret;
	reiser4_key key;
	struct page *page;
	jnode *node;
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
#if REISER4_DEBUG
	u64 old_subv_id;
#endif
	/*
	 * read the first page pointed by the extent pointer;
	 */
	item_key_by_coord(coord, &key);
#if REISER4_DEBUG
	old_subv_id = get_key_ordering(&key);
	assert("edward-1895", new_subv_id != old_subv_id);
#endif
	index = get_key_offset(&key) << PAGE_SHIFT;

	page = read_cache_page(mapping, index, filler, coord);
	if (IS_ERR(page))
		return PTR_ERR(page);
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		put_page(page);
		return RETERR(-EIO);
	}
	/*
	 * "cut off" a block from the beginning of the item.
	 * Block number will be deallocated by ->kill_hook()
	 * of the extent plugin. The page won't be invalidated
	 * bacause of REISER4_FILE_BALANCE_IN_PROGRESS flag.
	 */
	ret = cut_off_head_block(coord);
	done_lh(lh);
	if (ret) {
		put_page(page);
		return ret;
	}
	wait_on_page_writeback(page);
	lock_page(page);
	node = jnode_of_page(page, 1 /* for data IO */);
	if (IS_ERR(node)) {
		unlock_page(page);
		return PTR_ERR(node);
	}
	assert("edward-1896", node->subvol->id == old_subv_id);
	/*
	 * this flag will prevent jnode and page from being disconnected
	 */
	JF_SET(node, JNODE_WRITE_PREPARED);
	node->blocknr = 0;
	node->subvol = current_origin(new_subv_id);
	unlock_page(page);
	/*
	 * append the block to the end of previous item
	 */
	ret = reiser4_update_extent(inode, node, page_offset(page), NULL);
	if (ret)
		warning("edward-1897", "Failed to migrate extent: %d", ret);
	JF_CLR(node, JNODE_WRITE_PREPARED);
	jput(node);
	put_page(page);
	return ret;
}
/**
 * Migrate data blocks pointed by source extent to destination extent
 * in asynchronous manner.
 *
 * Pre-conditions:
 * . exclusive access to the file is taken;
 * . current position on source extent is locked;
 * Post-condition:
 * . on success data pages (all or part) pointed by the source
 *   extent migrated to destination extent.
 *
 * @coord: coord of the source extent
 */
int reiser4_migrate_extent(coord_t *coord, lock_handle *lh,
			   struct inode *inode, u64 new_subv_id)
{
	switch(state_of_extent(extent_by_coord(coord))) {
	case HOLE_EXTENT:
		return migrate_hole_extent(coord, lh, inode, new_subv_id);
	case UNALLOCATED_EXTENT:
	case ALLOCATED_EXTENT:
		return migrate_allocated_extent(coord, lh, inode, new_subv_id);
	default:
		impossible("edward-1898", "Bad state of extent");
		return -1;
	}
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
