/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "../../inode.h"
#include "../../super.h"
#include "../../tree_walk.h"
#include "../../carry.h"
#include "../../page_cache.h"
#include "../../ioctl.h"
#include "../object.h"
#include "../../prof.h"
#include "funcs.h"

#include <linux/writeback.h>

/* this file contains file plugin methods of regular reiser4 files. Those files are either built of tail items only (TAIL_ID) or
   of extent items only (EXTENT_POINTER_ID) or empty (have no items but stat data) */


/* get unix file plugin specific portion of inode */
inline unix_file_info_t *
unix_file_inode_data(const struct inode * inode)
{
	return &reiser4_inode_data(inode)->file_plugin_data.unix_file_info;
}

static int file_is_built_of_tails(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_TAILS;
}

int file_is_built_of_extents(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_EXTENTS;
}

int file_is_empty(const struct inode *inode)
{
	return unix_file_inode_data(inode)->container == UF_CONTAINER_EMPTY;
}

void set_file_state_extents(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_EXTENTS;
}

void set_file_state_tails(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_TAILS;
}

static void set_file_state_empty(struct inode *inode)
{
	unix_file_inode_data(inode)->container = UF_CONTAINER_EMPTY;
}

static int
less_than_ldk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keylt(key, znode_get_ld_key(node)));
}

int
equal_to_rdk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keyeq(key, znode_get_rd_key(node)));
}

#if REISER4_DEBUG

static int
less_than_rdk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keylt(key, znode_get_rd_key(node)));
}

int
equal_to_ldk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keyeq(key, znode_get_ld_key(node)));
}

/* get key of item next to one @coord is set to */
static reiser4_key *
get_next_item_key(const coord_t *coord, reiser4_key *next_key)
{
	if (coord->item_pos == node_num_items(coord->node) - 1) {
		/* get key of next item if it is in right neighbor */
		UNDER_RW_VOID(dk, znode_get_tree(coord->node), read,
			      *next_key = *znode_get_rd_key(coord->node));
	} else {
		/* get key of next item if it is in the same node */
		coord_t next;

		coord_dup_nocheck(&next, coord);
		next.unit_pos = 0;
		check_me("vs-730", coord_next_item(&next) == 0);
		item_key_by_coord(&next, next_key);
	}
	return next_key;
}

static int
item_of_that_file(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key max_possible;
	item_plugin *iplug;

	iplug = item_plugin_by_coord(coord);
	assert("vs-1011", iplug->b.max_key_inside);
	return keylt(key, iplug->b.max_key_inside(coord, &max_possible));
}

static int
check_coord(const coord_t *coord, const reiser4_key *key)
{
	coord_t twin;

	if (!REISER4_DEBUG)
		return 1;
	node_plugin_by_node(coord->node)->lookup(coord->node, key, FIND_MAX_NOT_MORE_THAN, &twin);
	return coords_equal(coord, &twin);
}

#endif /* REISER4_DEBUG */

static inline void
invalidate_extended_coord(uf_coord_t *uf_coord)
{
        coord_clear_iplug(&uf_coord->base_coord);
	uf_coord->valid = 0;
}

static inline void
validate_extended_coord(uf_coord_t *uf_coord, loff_t offset)
{
	assert("vs-1333", uf_coord->valid == 0);
	assert("vs-1348", item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension);
	
	/* FIXME: */
	item_body_by_coord(&uf_coord->base_coord);
	item_plugin_by_coord(&uf_coord->base_coord)->s.file.init_coord_extension(uf_coord, offset);
}

write_mode_t
how_to_write(uf_coord_t *uf_coord, const reiser4_key *key)
{
	write_mode_t result;
	coord_t *coord;
	ON_DEBUG(reiser4_key check);

	coord = &uf_coord->base_coord;

	assert("vs-1252", znode_is_wlocked(coord->node));
	assert("vs-1253", znode_is_loaded(coord->node));

	if (uf_coord->valid == 1) {
		assert("vs-1332", check_coord(coord, key));
		return (coord->between == AFTER_UNIT) ? APPEND_ITEM : OVERWRITE_ITEM;
	}

	if (less_than_ldk(coord->node, key)) {
		assert("vs-1014", get_key_offset(key) == 0);

		coord_init_before_first_item(coord, coord->node);
		uf_coord->valid = 1;
		result = FIRST_ITEM;
		goto ok;
	}

	assert("vs-1335", less_than_rdk(coord->node, key));
		
	if (node_is_empty(coord->node)) {
		assert("vs-879", znode_get_level(coord->node) == LEAF_LEVEL);
		assert("vs-880", get_key_offset(key) == 0);
		/*
		 * Situation that check below tried to handle is follows: some
		 * other thread writes to (other) file and has to insert empty
		 * leaf between two adjacent extents. Generally, we are not
		 * supposed to muck with this node. But it is possible that
		 * said other thread fails due to some error (out of disk
		 * space, for example) and leaves empty leaf
		 * lingering. Nothing prevents us from reusing it.
		 */
		assert("vs-1000", UNDER_RW(dk, current_tree, read,
					   keylt(key, znode_get_rd_key(coord->node))));
		assert("vs-1002", coord->between == EMPTY_NODE);
		result = FIRST_ITEM;
		uf_coord->valid = 1;
		goto ok;
	}

	assert("vs-1336", coord->item_pos < node_num_items(coord->node));
	assert("vs-1007", ergo(coord->between == AFTER_UNIT || coord->between == AT_UNIT, keyle(item_key_by_coord(coord, &check), key)));
	assert("vs-1008", ergo(coord->between == AFTER_UNIT || coord->between == AT_UNIT, keylt(key, get_next_item_key(coord, &check))));

	switch(coord->between) {
	case AFTER_ITEM:
		uf_coord->valid = 1;
		result = FIRST_ITEM;
		break;
	case AFTER_UNIT:
		assert("vs-1323", (item_is_tail(coord) || item_is_extent(coord)) && item_of_that_file(coord, key));
		assert("vs-1208", keyeq(item_plugin_by_coord(coord)->s.file.append_key(coord, &check), key));
		result = APPEND_ITEM;
		validate_extended_coord(uf_coord, get_key_offset(key));
		break;
	case AT_UNIT:
		/* FIXME: it would be nice to check that coord matches to key */
		assert("vs-1324", (item_is_tail(coord) || item_is_extent(coord)) && item_of_that_file(coord, key));
		validate_extended_coord(uf_coord, get_key_offset(key));
		result = OVERWRITE_ITEM;
		break;
	default:
		assert("vs-1337", 0);
		result = OVERWRITE_ITEM;
		break;
	}

ok:
	assert("vs-1349", uf_coord->valid == 1);
	assert("vs-1332", check_coord(coord, key));
	return result;
}

/* update inode's timestamps and size. If any of these change - update sd as well */
int
update_inode_and_sd_if_necessary(struct inode *inode,
				 loff_t new_size,
				 int update_i_size, int update_times,
				 int do_update)
{
	int result;
	int inode_changed;

	result = 0;

	/* FIXME: no need to avoid mark_inode_dirty call. It does not do anything but "capturing" inode */
	inode_changed = 0;

	if (update_i_size) {
		assert("vs-1104", inode->i_size != new_size);
		INODE_SET_FIELD(inode, i_size, new_size);
		inode_changed = 1;
	}
	
	if (update_times && (inode->i_ctime.tv_sec != get_seconds() ||
			     inode->i_mtime.tv_sec != get_seconds())) {
		/* time stamps are to be updated */
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		inode_changed = 1;
	}
	
	if (do_update && inode_changed) {
		assert("vs-946", !inode_get_flag(inode, REISER4_NO_SD));
		/* "capture" inode */
		result = reiser4_mark_inode_dirty(inode);
		if (result)
			warning("vs-636", "updating stat data failed: %i", result);
	}

	return result;
}

/* look for item of file @inode corresponding to @key */

#ifdef PSEUDO_CODE_CAN_COMPILE
find_item_obsolete()
{
	if (!coord && seal)
		set coord based on seal;
	if (coord) {
		if (key_in_coord(coord))
			return coord;
		coord = get_next_item(coord);
		if (key_in_coord(coord))
			return coord;
	}
	coord_by_key();
}
find_item()
{
	if (seal_is_set) {
		set coord by seal;
		if (key is in coord)
			return;
		if (key is right delim key) {
			get right neighbor;
			return first unit in it;
		}
	}
}
#endif

/* obtain lock on right neighbor and drop lock on current node */
int
goto_right_neighbor(coord_t * coord, lock_handle * lh)
{
	int result;
	lock_handle lh_right;

	assert("vs-1100", znode_is_locked(coord->node));

	init_lh(&lh_right);
	result = reiser4_get_right_neighbor(&lh_right, coord->node,
					    znode_is_wlocked(coord->node) ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK,
					    GN_DO_READ);
	if (result) {
		done_lh(&lh_right);
		return result;
	}

	done_lh(lh);

	coord_init_first_unit_nocheck(coord, lh_right.node);
	move_lh(lh, &lh_right);

	return 0;

}

/* this is to be used after find_file_item to determine real state of file */
static void
set_file_state(unix_file_info_t *uf_info, int cbk_result, tree_level level)
{
	if (!uf_info)
		return;

	assert("vs-1164", level == LEAF_LEVEL || level == TWIG_LEVEL);

	if (uf_info->container == UF_CONTAINER_UNKNOWN) {
		if (cbk_result == CBK_COORD_NOTFOUND)
			uf_info->container = UF_CONTAINER_EMPTY;
		else if (level == LEAF_LEVEL)
			uf_info->container = UF_CONTAINER_TAILS;
		else
			uf_info->container = UF_CONTAINER_EXTENTS;
	} else {
		/* file state is know, check that it is set correctly */
		assert("vs-1161", ergo(cbk_result == CBK_COORD_NOTFOUND,
				       uf_info->container == UF_CONTAINER_EMPTY));
		assert("vs-1162", ergo(level == LEAF_LEVEL && cbk_result == CBK_COORD_FOUND,
				       uf_info->container == UF_CONTAINER_TAILS));
		assert("vs-1165", ergo(level == TWIG_LEVEL && cbk_result == CBK_COORD_FOUND,
				       uf_info->container == UF_CONTAINER_EXTENTS));
	}
}

int
find_file_item(hint_t *hint, /* coord, lock handle and seal are here */
	       const reiser4_key *key, /* key of position in a file of next read/write */
	       znode_lock_mode lock_mode, /* which lock (read/write) to put on returned node */
	       __u32 cbk_flags, /* coord_by_key flags: CBK_UNIQUE [| CBK_FOR_INSERT] */
	       ra_info_t *ra_info,
	       unix_file_info_t *uf_info)
{
	int result;
	coord_t *coord;
	lock_handle *lh;

	assert("nikita-3030", schedulable());

	/* collect statistics on the number of calls to this function */
	reiser4_stat_inc(file.find_file_item);

	coord = &hint->coord.base_coord;
	lh = hint->coord.lh;
	init_lh(lh);
	if (hint) {
		result = hint_validate(hint, key, 1/*check key*/, lock_mode);
		if (!result) {
			if (coord->between == AFTER_UNIT && equal_to_rdk(coord->node, key)) {
				result = goto_right_neighbor(coord, lh);
				if (result == -E_NO_NEIGHBOR)
					return RETERR(-EIO);
				if (result)
					return result;
				assert("vs-1152", equal_to_ldk(coord->node, key));
				/* we moved to different node. Invalidate coord extension, zload is necessary to init it
				   again */
				hint->coord.valid = 0;
				reiser4_stat_inc(file.find_file_item_via_right_neighbor);
			} else {
				reiser4_stat_inc(file.find_file_item_via_seal);
			}

			set_file_state(uf_info, CBK_COORD_FOUND, znode_get_level(coord->node));
			return CBK_COORD_FOUND;
		}
	}

	/* collect statistics on the number of calls to this function which did not get optimized */
	reiser4_stat_inc(file.find_file_item_via_cbk);
	
	coord_init_zero(coord);
	if (uf_info != NULL) {
		result = object_lookup(uf_info->inode,
				       key,
				       coord,
				       lh,
				       lock_mode,
				       FIND_MAX_NOT_MORE_THAN,
				       TWIG_LEVEL,
				       LEAF_LEVEL,
				       cbk_flags,
				       ra_info);
	} else {
		result = coord_by_key(current_tree,
				      key,
				      coord,
				      lh,
				      lock_mode,
				      FIND_MAX_NOT_MORE_THAN,
				      TWIG_LEVEL,
				      LEAF_LEVEL,
				      cbk_flags,
				      ra_info);
	}
	if (!IS_CBKERR(result))
		set_file_state(uf_info, result, znode_get_level(coord->node));

	/* FIXME: we might already have coord extension initialized */
	hint->coord.valid = 0;
	return result;
}

/* plugin->u.file.write_flowom = NULL
   plugin->u.file.read_flow = NULL */

void
hint_init_zero(hint_t *hint, lock_handle *lh)
{
	xmemset(hint, 0, sizeof (*hint));
	hint->coord.lh = lh;
}

/* find position of last byte of last item of the file plus 1. This is used by truncate and mmap to find real file
   size */
static int
find_file_size(struct inode *inode, loff_t *file_size)
{
	int result;
	reiser4_key key;
	hint_t hint;
	coord_t *coord;
	lock_handle lh;
	item_plugin *iplug;

	assert("vs-1247", inode_file_plugin(inode)->key_by_inode == key_by_inode_unix_file);
	key_by_inode_unix_file(inode, get_key_offset(max_key()), &key);

	hint_init_zero(&hint, &lh);
	result = find_file_item(&hint, &key, ZNODE_READ_LOCK, CBK_UNIQUE, 0/* ra_info */, unix_file_inode_data(inode));
	if (result == CBK_COORD_NOTFOUND) {
		/* there are no items of this file */
		done_lh(&lh);
		*file_size = 0;
		return 0;
	}

	if (result != CBK_COORD_FOUND) {
		/* error occured */
		done_lh(&lh);
		return result;
	}

	coord = &hint.coord.base_coord;

	/* there are items of this file (at least one) */
	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (unlikely(result)) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(coord);

	assert("vs-853", iplug->s.file.append_key);
	iplug->s.file.append_key(coord, &key);

	*file_size = get_key_offset(&key);

	zrelse(coord->node);
	done_lh(&lh);

	return 0;
}

/* estimate and reserve space needed to truncate page which gets partially truncated: one block for page itself, stat
   data update (estimate_one_insert_into_item) and one item insertion (estimate_one_insert_into_item) which may happen
   if page corresponds to hole extent and unallocated one will have to be created */
static int reserve_partial_page(reiser4_tree *tree)
{
	grab_space_enable();
	return reiser4_grab_reserved(reiser4_get_current_sb(),
				     1 +
				     2 * estimate_one_insert_into_item(tree),
				     BA_CAN_COMMIT, __FUNCTION__);
}

/* estimate and reserve space needed to cut one item and update one stat data */
int reserve_cut_iteration(reiser4_tree *tree, const char * message)
{
	__u64 estimate = estimate_one_item_removal(tree)
		+ estimate_one_insert_into_item(tree);

	assert("nikita-3172", lock_stack_isclean(get_current_lock_stack()));

	grab_space_enable();
	/* We need to double our estimate now that we can delete more than one
	   node. */
	return reiser4_grab_reserved(reiser4_get_current_sb(), estimate*2, BA_CAN_COMMIT, message);
}

/* cut file items one by one starting from the last one until new file size (inode->i_size) is reached. Reserve space
   and update file stat data on every single cut from the tree */
static int
cut_file_items(struct inode *inode, loff_t new_size, int update_sd, loff_t cur_size)
{
	reiser4_key from_key, to_key;
	reiser4_key smallest_removed;
	int result;

	assert("vs-1248", inode_file_plugin(inode)->key_by_inode == key_by_inode_unix_file);
	key_by_inode_unix_file(inode, new_size, &from_key);
	to_key = from_key;
	set_key_offset(&to_key, cur_size - 1/*get_key_offset(max_key())*/);

	while (1) {
		result = reserve_cut_iteration(tree_by_inode(inode), __FUNCTION__);
		if (result)
			break;

		result = cut_tree_object(current_tree, &from_key, &to_key,
					 &smallest_removed, inode);
		if (result == -E_REPEAT) {
			/* -E_REPEAT is a signal to interrupt a long file truncation process */
			/* FIXME(Zam) cut_tree does not support that signaling.*/
			result = update_inode_and_sd_if_necessary
				(inode, get_key_offset(&smallest_removed), 1, 1, update_sd);
			if (result)
				break;

			all_grabbed2free(__FUNCTION__);
			reiser4_release_reserved(inode->i_sb);

			continue;
		}
		if (result)
			break;

		/* Final sd update after the file gets its correct size */
		result = update_inode_and_sd_if_necessary(inode, new_size, 1, 1, update_sd);
		break;
	}

	all_grabbed2free(__FUNCTION__);
	reiser4_release_reserved(inode->i_sb);

	return result;
}

int unix_file_writepage_nolock(struct page *page);

/* part of unix_file_truncate: it is called when truncate is used to make file shorter */
static int
shorten_file(struct inode *inode, loff_t new_size, int update_sd, loff_t cur_size)
{
	int result;
	struct page *page;
	int padd_from;
	unsigned long index;
	char *kaddr;

	assert("vs-1106", inode->i_size > new_size);

	/* all items of ordinary reiser4 file are grouped together. That is why we can use cut_tree. Plan B files (for
	   instance) can not be truncated that simply */
	result = cut_file_items(inode, new_size, update_sd, cur_size);
	if (result)
		return result;

	assert("vs-1105", new_size == inode->i_size);
	if (inode->i_size == 0) {
		set_file_state_empty(inode);
		return 0;
	}

	if (file_is_built_of_tails(inode))
		/* No need to worry about zeroing last page after new file end */
		return 0;

	padd_from = inode->i_size & (PAGE_CACHE_SIZE - 1);
	if (!padd_from)
		/* file is truncated to page boundary */
		return 0;

	result = reserve_partial_page(tree_by_inode(inode));
	if (result) {
		reiser4_release_reserved(inode->i_sb);
		return result;
	}

	/* last page is partially truncated - zero its content */
	index = (inode->i_size >> PAGE_CACHE_SHIFT);
	page = read_cache_page(inode->i_mapping, index, readpage_unix_file/*filler*/, 0);
	if (IS_ERR(page)) {
		all_grabbed2free("shorten_file: read_cache_page failed");
		reiser4_release_reserved(inode->i_sb);
		if (likely(PTR_ERR(page) == -EINVAL)) {
			/* looks like file is built of tail items */
			return 0;
		}
		return PTR_ERR(page);
	}
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		all_grabbed2free("shorten_file: page !uptodate");
		page_cache_release(page);
		reiser4_release_reserved(inode->i_sb);
		return RETERR(-EIO);
	}
	result = unix_file_writepage_nolock(page);

	/* FIXME: cut_file_items has already updated inode. Probably it would be better to update it here when file is
	   really truncated */
	all_grabbed2free("shorten_file");
	if (result) {
		page_cache_release(page);
		reiser4_release_reserved(inode->i_sb);
		return result;
	}

	lock_page(page);
	assert("vs-1066", PageLocked(page));
	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + padd_from, 0, PAGE_CACHE_SIZE - padd_from);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	reiser4_unlock_page(page);
	page_cache_release(page);
	reiser4_release_reserved(inode->i_sb);
	return 0;
}

static loff_t
write_flow(struct file *file, unix_file_info_t *uf_info, const char *buf, loff_t count, loff_t pos);

/* it is called when truncate is used to make file longer and when write position is set past real end of file. It
   appends file which has size @cur_size with hole of certain size (@hole_size) */
static int
append_hole(unix_file_info_t *uf_info, loff_t new_size)
{
	int result;
	loff_t written;
	loff_t hole_size;
	
	assert("vs-1107", uf_info->inode->i_size < new_size);

	result = 0;
	hole_size = new_size - uf_info->inode->i_size;
	written = write_flow(0/*file*/, uf_info, 0/*buf*/, hole_size, uf_info->inode->i_size);
	if (written != hole_size) {
		/* return error because file is not expanded as required */
		if (written > 0)
			result = RETERR(-ENOSPC);
		else
			result = written;
	} else {
		assert("vs-1081", uf_info->inode->i_size == new_size);
	}
	return result;
}

int
setattr_reserve(reiser4_tree *tree)
{
	assert("vs-1096", is_grab_enabled(get_current_context()));
	return reiser4_grab_space(estimate_one_insert_into_item(tree), BA_CAN_COMMIT, "setattr_reserve");
}

/* this either cuts or add items of/to the file so that items match new_size. It is used in unix_file_setattr when it is
   used to truncate 
VS-FIXME-HANS: explain that
and in unix_file_delete */
static int
truncate_file(struct inode *inode, loff_t new_size, int update_sd)
{
	int result;
	loff_t cur_size;

	/*INODE_SET_FIELD(inode, i_size, new_size);*/

	result = find_file_size(inode, &cur_size);
	if (!result) {
		if (new_size != cur_size) {
			INODE_SET_FIELD(inode, i_size, cur_size);
			if (cur_size < new_size)
				result = append_hole(unix_file_inode_data(inode), new_size);
			else
				result = shorten_file(inode, new_size, update_sd, cur_size);
		} else {
			/* when file is built of extens - find_file_size can only calculate old file size up to page
			 * size. Case of not changing file size is detected in unix_file_setattr, therefore here we have
			 * expanding file within its last page up to the end of that page */
			assert("vs-1115", file_is_built_of_extents(inode) || (file_is_empty(inode) && cur_size == 0));
			assert("vs-1116", (new_size & ~PAGE_CACHE_MASK) == 0);

			/* update stat data */
			if (update_sd) {
				result = setattr_reserve(tree_by_inode(inode));
				if (!result)
					result = update_inode_and_sd_if_necessary(inode, new_size, 1, 1, 1);
				all_grabbed2free(__FUNCTION__);
			}
		}
	}
	return result;
}

/* plugin->u.file.truncate
   all the work is done on reiser4_setattr->unix_file_setattr->truncate_file
*/
int
truncate_unix_file(struct inode *inode, loff_t new_size)
{
	return 0;
}

/* plugin->u.write_sd_by_inode = write_sd_by_inode_common */

/* get access hint (seal, coord, key, level) stored in reiser4 private part of
   struct file if it was stored in a previous access to the file */
int
load_file_hint(struct file *file, hint_t *hint, lock_handle *lh)
{
	reiser4_file_fsdata *fsdata;

	if (file) {
		fsdata = reiser4_get_file_fsdata(file);
		if (IS_ERR(fsdata))
			return PTR_ERR(fsdata);

		if (seal_is_set(&fsdata->reg.hint.seal)) {
			*hint = fsdata->reg.hint;
			hint->coord.lh = lh;
			return 0;
		}
		xmemset(&fsdata->reg.hint, 0, sizeof(hint_t));
	}
	hint_init_zero(hint, lh);
	return 0;
}


/* this copies hint for future tree accesses back to reiser4 private part of
   struct file */
void
save_file_hint(struct file *file, const hint_t *hint)
{
	reiser4_file_fsdata *fsdata;

	if (!file || !seal_is_set(&hint->seal))
		return;

	fsdata = reiser4_get_file_fsdata(file);
	assert("vs-965", !IS_ERR(fsdata));
	fsdata->reg.hint = *hint;
	return;
}

void
unset_hint(hint_t *hint)
{
	assert("vs-1315", hint);
	seal_done(&hint->seal);
}

/* coord must be set properly. So, that set_hint has nothing to do */
void
set_hint(hint_t *hint, const reiser4_key *key, znode_lock_mode mode)
{
	ON_DEBUG(coord_t *coord = &hint->coord.base_coord);
	assert("vs-1207", WITH_DATA(coord->node, check_coord(coord, key)));

	seal_init(&hint->seal, &hint->coord.base_coord, key);
	hint->offset = get_key_offset(key);
	hint->level = znode_get_level(hint->coord.base_coord.node);
	hint->mode = mode;
}

int
hint_is_set(const hint_t *hint)
{
	return seal_is_set(&hint->seal);
}

#if REISER4_DEBUG
static int all_but_offset_key_eq(const reiser4_key *k1, const reiser4_key *k2)
{
	return (get_key_locality(k1) == get_key_locality(k2) &&
		get_key_type(k1) == get_key_type(k2) &&
		get_key_band(k1) == get_key_band(k2) &&
		get_key_ordering(k1) == get_key_ordering(k2) &&
		get_key_objectid(k1) == get_key_objectid(k2));
}
#endif

int
hint_validate(hint_t *hint, const reiser4_key *key, int check_key, znode_lock_mode lock_mode)
{
	if (!hint || !hint_is_set(hint) || hint->mode != lock_mode)
		/* hint either not set or set by different operation */
		return RETERR(-E_REPEAT);

	assert("vs-1277", all_but_offset_key_eq(key, &hint->seal.key));
	
	if (check_key && get_key_offset(key) != hint->offset)
		/* hint is set for different key */
		return RETERR(-E_REPEAT);

	return seal_validate(&hint->seal, &hint->coord.base_coord, key,
			     hint->level, hint->coord.lh, FIND_MAX_NOT_MORE_THAN, lock_mode, ZNODE_LOCK_LOPRI);
}

/* nolock means: do not get EA or NEA on a file the page belongs to (it is obtained already either in
   unix_file_writepage or in tail2extent). Lock page after long term znode lock is obtained. Return with page locked.
   Used in shorten_file, replace, writepage_unix_file */
int
unix_file_writepage_nolock(struct page *page)
{
	int result;
	lock_handle lh;
	hint_t hint;
	reiser4_key key;
	item_plugin *iplug;
	znode *loaded;

	reiser4_stat_inc(file.page_ops.writepage_calls);

	assert("vs-1065", page->mapping && page->mapping->host);

	/* get key of first byte of the page */
	key_by_inode_unix_file(page->mapping->host, (loff_t) page->index << PAGE_CACHE_SHIFT, &key);

	hint_init_zero(&hint, &lh);
	result = find_file_item(&hint, &key, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT, 0/*ra_info*/, 0/* inode */);

	if (IS_CBKERR(result)) {
		done_lh(&lh);
		return result;
	}

	result = zload(lh.node);
	if (result) {
		longterm_unlock_znode(&lh);
		return result;
	}
	loaded = lh.node;
	/* get plugin of extent item */
	iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	result = iplug->s.file.writepage(&key, &hint.coord, page, how_to_write(&hint.coord, &key));
	assert("vs-429378", result != -E_REPEAT);
	zrelse(loaded);
	done_lh(&lh);
	return result;
}

/* plugin->u.file.writepage this does not start i/o against this page. It just must garantee that tree has a pointer to
   this page. This is called for pages which were dirtied via mmap-ing by reiser4_writepages */
static int
capture_unix_file_page(struct page *page)
{
	int result;
	struct inode *inode;
	unix_file_info_t *uf_info;

	assert("vs-1084", page->mapping && page->mapping->host);
	inode = page->mapping->host;
	assert("vs-1139", file_is_built_of_extents(inode));
	/* page belongs to file */
	assert("vs-1393", inode->i_size > ((loff_t) page->index << PAGE_CACHE_SHIFT));

	uf_info = unix_file_inode_data(inode);

	/* writepage may involve insertion of one unit into tree */
	result = reiser4_grab_space(estimate_one_insert_into_item(tree_by_inode(inode)), BA_CAN_COMMIT, "unix_file_writepage");
	if (likely(!result)) {
		result = unix_file_writepage_nolock(page);
	}
	all_grabbed2free("unix_file_writepage");
	return result;
}

/* Check mapping for existence of not captured dirty pages */
static int inode_has_anonymous_pages(struct inode *inode)
{
	int ret;
	struct address_space *mapping;

	mapping = inode->i_mapping;
	spin_lock (&mapping->page_lock);
	ret = !list_empty (get_moved_pages(mapping));
	spin_unlock (&mapping->page_lock);
	ret |= (reiser4_inode_data(inode)->eflushed_anon > 0);

	return ret;
}

static int capture_page_and_create_extent(struct page * page)
{
	int result;

	grab_space_enable ();

	result = capture_unix_file_page(page);

	if (result != 0)
		SetPageError(page);
	return result;
}

static void redirty_inode(struct inode *inode)
{
	spin_lock(&inode_lock);
	inode->i_state |= I_DIRTY;
	spin_unlock(&inode_lock);
}

static int capture_anonymous_page(struct page *pg)
{
	struct address_space *mapping;
	int result;

	mapping = pg->mapping;
	result = 0;
	if (PageWriteback(pg)) {
		if (PageDirty(pg))
			list_move(&pg->list, &mapping->dirty_pages);
		else
			list_move(&pg->list, &mapping->locked_pages);
	} else if (!PageDirty(pg))
		list_move(&pg->list, &mapping->clean_pages);
	else {
		jnode *node;

		list_move(&pg->list, &mapping->io_pages);
		page_cache_get (pg);
		
		spin_unlock (&mapping->page_lock);

		lock_page(pg);
		/* page is guaranteed to be in the mapping, because we are
		 * operating under rw-semaphore. */
		assert("nikita-3336", pg->mapping == mapping);
		node = jnode_of_page(pg);
		unlock_page(pg);
		if (!IS_ERR(node)) {
			result = jload(node);
			assert("nikita-3334", result == 0);
			assert("nikita-3335", jnode_page(node) == pg);
			result = capture_page_and_create_extent(pg);
			if (result == 0) {
				assert("nikita-3326", jnode_check_dirty(node));
				assert("nikita-3327", node->atom != NULL);
				JF_CLR(node, JNODE_KEEPME);
			} else
				warning("nikita-3329",
					"Cannot capture anon page: %i", result);
			jrelse(node);
			jput(node);
		} else
			result = PTR_ERR(node);
		page_cache_release(pg);
		spin_lock(&mapping->page_lock);
	}
	return result;
}

#define CAPTURE_AJNODE_BURST     (128)
#define CAPTURE_APAGE_BURST      (1024)

static int capture_anonymous_jnodes(struct inode *inode)
{
	struct list_head *tmp, *next;
	reiser4_inode *info;
	reiser4_tree *tree;
	int nr;
	int found;
	int result;

	tree = tree_by_inode(inode);

	info = reiser4_inode_data(inode);
	result = 0;
	nr = 0;
	do {
		found = 0;
		spin_lock_eflush(tree->super);

		list_for_each_safe(tmp, next, &info->eflushed_jnodes) {
			eflush_node_t *ef;
			jnode *node;

			ef = list_entry(tmp, eflush_node_t, inode_link);
			node = ef->node;
			/*
			 * anonymous jnode doesn't have an atom.
			 *
			 * jnode spin-lock is not needed, because we don't have
			 * requirement to capture _all_ anonymous jnodes anyway.
			 */
			if (node->atom != NULL)
				continue;
			jref(node);
			spin_unlock_eflush(tree->super);
			result = jload(node);
			jput(node);
			if (result != 0)
				return result;
			spin_lock(&inode->i_mapping->page_lock);
			result = capture_anonymous_page(jnode_page(node));
			spin_unlock(&inode->i_mapping->page_lock);
			jrelse(node);
			found ++;
			nr ++;
			if (nr >= CAPTURE_AJNODE_BURST) {
				found = 0;
				redirty_inode(inode);
			}
			spin_lock_eflush(tree->super);
			break;
		}
		spin_unlock_eflush(tree->super);
	} while (found > 0 && result == 0);
	return result;
}

static int capture_anonymous_pages(struct address_space * mapping)
{
	struct list_head *mpages;
	int result;
	int nr;

	result = 0;
	nr = 0;

	spin_lock (&mapping->page_lock);

	mpages = get_moved_pages(mapping);
	while (result == 0 && !list_empty (mpages) && nr < CAPTURE_APAGE_BURST) {
		struct page *pg = list_entry(mpages->prev, struct page, list);

		result = capture_anonymous_page(pg);
		++ nr;
	}

	spin_unlock(&mapping->page_lock);

	if (nr >= CAPTURE_APAGE_BURST)
		redirty_inode(mapping->host);

	if (result == 0)
		result = capture_anonymous_jnodes(mapping->host);

	if (result != 0)
		warning("nikita-3328", "Cannot capture anon pages: %i", result);
	return result;
}

/*
 * this file plugin method is called to capture into current atom all
 * "anonymous pages", that is, pages modified through mmap(2). For each such
 * page this function creates jnode, captures this jnode, and creates (or
 * modifies) extent. Anonymous pages are kept on the special inode list. Some
 * of them can be emergency flushed. To cope with this list of eflushed jnodes
 * from this inode is scanned.
 */
int
capture_unix_file(struct inode *inode, struct writeback_control *wbc)
{
	int               result;
	unix_file_info_t *uf_info;
	reiser4_context ctx;

	if (!inode_has_anonymous_pages(inode))
		return 0;

	init_context(&ctx, inode->i_sb);
	/* avoid recursive calls to ->sync_inodes */
	ctx.nobalance = 1;
	assert("zam-760", lock_stack_isclean(get_current_lock_stack()));

	result = 0;
	do {
		uf_info = unix_file_inode_data(inode);
		/*
		 * locking: creation of extent requires read-semaphore on
		 * file. _But_, this function can also be called in the
		 * context of write system call from
		 * balance_dirty_pages(). So, write keeps semaphore (possible
		 * in write mode) on file A, and this function tries to
		 * acquire semaphore on (possibly) different file B. A/B
		 * deadlock is on a way. To avoid this try-lock is used
		 * here. This however leads to the complications in the
		 * fsync() case, which are not yet handled.
		 */
		if (rw_latch_try_read(&uf_info->latch) == 0) {
			LOCK_CNT_INC(inode_sem_r);

			result = capture_anonymous_pages(inode->i_mapping);
			rw_latch_up_read(&uf_info->latch);
			LOCK_CNT_DEC(inode_sem_r);
			if (result != 0 || wbc->sync_mode != WB_SYNC_ALL)
				break;
			result = txnmgr_force_commit_all(inode->i_sb, 0);
		} else
			result = RETERR(-EBUSY);
	} while (result == 0 && inode_has_anonymous_pages(inode));

	reiser4_exit_context(&ctx);
	return result;
}

/* plugin->u.file.readpage
   page must be not out of file. This is called either via page fault and in that case vp is struct file *file, or on
   truncate when last page of a file is to be read to perform its partial truncate and in that case vp is 0
*/
int
readpage_unix_file(void *vp, struct page *page)
{
	int result;
	struct inode *inode;
	lock_handle lh;
	reiser4_key key;
	item_plugin *iplug;
	hint_t hint;
	coord_t *coord;
	struct file *file;

	reiser4_stat_inc(file.page_ops.readpage_calls);

	assert("vs-1062", PageLocked(page));
	assert("vs-1061", page->mapping && page->mapping->host);
	assert("vs-1078", (page->mapping->host->i_size > ((loff_t) page->index << PAGE_CACHE_SHIFT)));

	inode = page->mapping->host;

	file = vp;
	result = load_file_hint(file, &hint, &lh);
	if (result)
		return result;

	/* get key of first byte of the page */
	key_by_inode_unix_file(inode, (loff_t) page->index << PAGE_CACHE_SHIFT, &key);

	/* look for file metadata corresponding to first byte of page */
	reiser4_unlock_page(page);
	result = find_file_item(&hint, &key, ZNODE_READ_LOCK, CBK_UNIQUE, 0/* ra_info */, unix_file_inode_data(inode));
	reiser4_lock_page(page);
	if (result != CBK_COORD_FOUND) {
		/* this indicates file corruption */
		done_lh(&lh);
		return result;
	}

	if (PageUptodate(page)) {
		done_lh(&lh);
		unlock_page(page);
		return 0;
	}
	
	coord = &hint.coord.base_coord;
	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	if (!hint.coord.valid)
		validate_extended_coord(&hint.coord, (loff_t) page->index << PAGE_CACHE_SHIFT);

	if (!coord_is_existing_unit(coord)) {
		/* this indicates corruption */
		warning("vs-280",
			"Looking for page %lu of file %llu (size %lli). "
			"No file items found (%d). "
			"File is corrupted?\n",
			page->index, get_inode_oid(inode), inode->i_size, result);
		zrelse(coord->node);
		done_lh(&lh);
		return RETERR(-EIO);
	}

	/* get plugin of found item or use plugin if extent if there are no
	   one */
	iplug = item_plugin_by_coord(coord);
	if (iplug->s.file.readpage)
		result = iplug->s.file.readpage(coord, page);
	else
		result = RETERR(-EINVAL);

	if (!result) {
		set_key_offset(&key, (loff_t) (page->index + 1) << PAGE_CACHE_SHIFT);
		/* FIXME should call set_hint() */
		unset_hint(&hint);
	} else
		unset_hint(&hint);
	zrelse(coord->node);
	done_lh(&lh);

	save_file_hint(file, &hint);

	assert("vs-979", ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
	/* if page has jnode - that jnode is mapped */
	assert("vs-1098", ergo(result == 0 && PagePrivate(page),
			       jnode_mapped(jprivate(page))));
	return result;
}

/* returns 1 if file of that size (@new_size) has to be stored in unformatted
   nodes */
/* Audited by: green(2002.06.15) */
static int
should_have_notail(const unix_file_info_t *uf_info, loff_t new_size)
{
	if (!uf_info->tplug)
		return 1;
	return !uf_info->tplug->have_tail(uf_info->inode, new_size);

}

static reiser4_block_nr unix_file_estimate_read(struct inode *inode,
						loff_t count UNUSED_ARG)
{
    	/* We should reserve one block, because of updating of the stat data
	   item */
	assert("vs-1249", inode_file_plugin(inode)->estimate.update == estimate_update_common);
	return estimate_update_common(inode);
}

/* plugin->u.file.read

   the read method for the unix_file plugin

*/
#if 0
int first_read_started = 0;
int second_read_started = 0;

static int
debugging_can_read(loff_t *off)
{
	if (*off == 0) {
		if (first_read_started == 1) {
			/* second read started */
			return 1;
		} else {
			first_read_started = 1;
			return 0;
		}
	}
}

static ssize_t
debugging_read(struct file *file, unix_file_info_t *uf_info, char *buf, size_t read_amount, loff_t *off)
{
	/* short read */
	struct page *page;

	get_nonexclusive_access(uf_info);
		
	/*needed = unix_file_estimate_read(inode, read_amount);*/
	needed = tree_by_inode(inode)->estimate_one_insert;

	result = reiser4_grab_space(needed, BA_CAN_COMMIT, "unix_file_read");	
	if (result != 0) {
		drop_nonexclusive_access(uf_info);
		return RETERR(-ENOSPC);
	}
	result = unix_file_build_flow(inode, buf, 1 /* user space */ , read_amount, *off, READ_OP, &f);
	if (unlikely(result)) {
		drop_nonexclusive_access(uf_info);
		return result;
	}
	result = load_file_hint(file, &hint);
	if (unlikely(result)) {
		drop_nonexclusive_access(uf_info);
		return result;
	}
	/* initialize readahead info */
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));

	read = 0;
	while (read_amount) {
		if (*off == 0) {
			result = find_file_item(&hint, &f.key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE, &ra_info, uf_info);
			BUG_ON(result != CBK_COORD_FOUND);
		} else {
			result = seal_validate(&hint.seal, &hint.coord, &f.key,
					       hint.level, &lh, FIND_MAX_NOT_MORE_THAN, ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI);
			BUG_ON(result != 0);
		}

		page = find_get_page(inode->i_mapping, *off >> PAGE_CACHE_SHIFT);
		BUG_ON(page == NULL);
		BUG_ON(!PageUptodate(page));
		mark_page_accessed(page);

		reiser4_lock_page(page);
		if (PagePrivate(page)) {
			jnode *j;
			j = jnode_by_page(page);
			if (REISER4_USE_EFLUSH && j)
				UNDER_SPIN_VOID(jnode, j, eflush_del(j, 1));
		}
		reiser4_unlock_page(page);

		__copy_to_user(buf, kmap(page), PAGE_CACHE_SIZE);
		kunmap(page);
		page_cache_release(page);
			
		*off += PAGE_CACHE_SIZE;
		buf += PAGE_CACHE_SIZE;
		read_amount -= PAGE_CACHE_SIZE;
		read += PAGE_CACHE_SIZE;

		if (*off == PAGE_CACHE_SIZE) {
			set_hint(&hint, &f.key, &coord, COORD_RIGHT_STATE);
		}

		move_coord_pages(&coord, 1);
		set_key_offset(&f.key, get_key_offset(&f.key) + PAGE_CACHE_SIZE);
		set_hint(&hint, &f.key, &coord, COORD_RIGHT_STATE);
		
		done_lh(&lh);
		
	}
	if (*off == PAGE_CACHE_SIZE)
		save_file_hint(file, &hint);

	drop_nonexclusive_access(uf_info);
	return read;
		
}
#endif

ssize_t read_unix_file(struct file *file, char *buf, size_t read_amount, loff_t *off)
{
	int result;
	struct inode *inode;
	flow_t f;
	lock_handle lh;
	hint_t hint;
	coord_t *coord;

	size_t read;
	reiser4_block_nr needed;
	ra_info_t ra_info;
	int (*read_f) (struct file *, flow_t *, hint_t *);
	unix_file_info_t *uf_info;

	if (unlikely(!read_amount))
		return 0;

	inode = file->f_dentry->d_inode;
	assert("vs-972", !inode_get_flag(inode, REISER4_NO_SD));
	uf_info = unix_file_inode_data(inode);

/*
	if (debugging_can_read(off))
		return debugging_read();
*/

	get_nonexclusive_access(uf_info);
	if (*off >= inode->i_size) {
		/* position to read from is past the end of file */
		drop_nonexclusive_access(uf_info);
		return 0;
	}
	if (*off + read_amount > inode->i_size)
		read_amount = inode->i_size - *off;

	needed = unix_file_estimate_read(inode, read_amount); /* FIXME: tree_by_inode(inode)->estimate_one_insert */
	result = reiser4_grab_space(needed, BA_CAN_COMMIT, "unix_file_read");	
	if (result != 0) {
		drop_nonexclusive_access(uf_info);
		return RETERR(-ENOSPC);
	}

	/* build flow */
	assert("vs-1250", inode_file_plugin(inode)->flow_by_inode == flow_by_inode_unix_file);
	result = flow_by_inode_unix_file(inode, buf, 1 /* user space */ , read_amount, *off, READ_OP, &f);
	if (unlikely(result)) {
		drop_nonexclusive_access(uf_info);
		return result;
	}

	/* get seal and coord sealed with it from reiser4 private data of struct file.  The coord will tell us where our
	   last read of this file finished, and the seal will help to determine if that location is still valid.
	*/
	result = load_file_hint(file, &hint, &lh);
	if (unlikely(result)) {
		drop_nonexclusive_access(uf_info);
		return result;
	}

	/* initialize readahead info */
	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));/*FIXME: ~0ull*/

	switch(uf_info->container) {
	case UF_CONTAINER_EXTENTS:
		read_f = item_plugin_by_id(EXTENT_POINTER_ID)->s.file.read;
		break;
	case UF_CONTAINER_TAILS:
		read_f = item_plugin_by_id(TAIL_ID)->s.file.read;
		break;
	case UF_CONTAINER_UNKNOWN:
		read_f = 0;
		break;
	default:
		read_f = 0;
		warning("vs-1297", "File (ino %llu) has unknown state: %d\n", get_inode_oid(inode), uf_info->container);
		return -EIO;
	}

	while (f.length) {
		assert("vs-1354", inode->i_size > get_key_offset(&f.key));

		result = find_file_item(&hint, &f.key, ZNODE_READ_LOCK, CBK_UNIQUE, &ra_info, uf_info);
		if (result != CBK_COORD_FOUND) {
			/* item had to be found, as it was not - we have
			   -EIO */
			done_lh(&lh);
			break;
		}

		coord = &hint.coord.base_coord;
		if (coord->between != AT_UNIT) {
			printk("zam-829: unix_file_read: key not in item, "
			       "reading offset (%llu) from the file (oid %llu) with size (%llu)\n",
			       (unsigned long long)get_key_offset(&f.key),
			       get_inode_oid(inode),
			       (unsigned long long)inode->i_size);
			longterm_unlock_znode(&lh);
			break;
		}

		coord_clear_iplug(coord);
		hint.coord.valid = 0;
		result = zload_ra(coord->node, &ra_info);
		if (unlikely(result)) {
			longterm_unlock_znode(&lh);
			return result;
		}
		validate_extended_coord(&hint.coord, get_key_offset(&f.key));

		/* call item's read method */
		if (!read_f)
			read_f = item_plugin_by_coord(coord)->s.file.read;
		result = read_f(file, &f, &hint);
		zrelse(coord->node);
		done_lh(&lh);
		if (result == -E_REPEAT) {
			printk("zam-830: unix_file_read: key was not found in item, repeat search\n");
			unset_hint(&hint);
			continue;
		}
		if (result)
			break;
	}

	save_file_hint(file, &hint);

	read = read_amount - f.length;
	if (read) {
		/* something was read. Update stat data */
		update_atime(inode);
	}

	drop_nonexclusive_access(uf_info);

	/* update position in a file */
	*off += read;

	/* return number of read bytes or error code if nothing is read */
	return read ?: result;
}

typedef int (*write_f_t)(struct inode *, flow_t *, hint_t *, int grabbed, write_mode_t);

/* This searches for write position in the tree and calls write method of
   appropriate item to actually copy user data into filesystem. This loops
   until all the data from flow @f are written to a file. */
static loff_t
append_and_or_overwrite(struct file *file, unix_file_info_t *uf_info, flow_t *flow)
{
	int result;
	lock_handle lh;
	hint_t hint;
	loff_t to_write;
	write_f_t write_f;
	file_container_t cur_container, new_container;
	znode *loaded;

	assert("nikita-3031", schedulable());
	assert("vs-1109", get_current_context()->grabbed_blocks == 0);

	/* get seal and coord sealed with it from reiser4 private data of
	   struct file */
	result = load_file_hint(file, &hint, &lh);
	if (result)
		return result;

	to_write = flow->length;

	while (flow->length) {
		assert("vs-1123", get_current_context()->grabbed_blocks == 0);
		if (to_write == flow->length) {
			/* it may happend that find_next_item will have to insert empty node to the tree (empty leaf
			   node between two extent items) */
			result = reiser4_grab_space_force(1 + estimate_one_insert_item(tree_by_inode(uf_info->inode)), 0,
							  "append_and_or_overwrite: for cbk and eottl");
			if (result)
				return result;
		}
		/* look for file's metadata (extent or tail item) corresponding to position we write to */
		result = find_file_item(&hint, &flow->key, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT, 0/* ra_info */, uf_info);
		all_grabbed2free("append_and_or_overwrite after cbk");
		if (IS_CBKERR(result)) {
			/* error occurred */
			done_lh(&lh);
			return result;
		}
		
		cur_container = uf_info->container;
		switch (cur_container) {
		case UF_CONTAINER_EMPTY:
			assert("vs-1196", get_key_offset(&flow->key) == 0);
			if (should_have_notail(uf_info, get_key_offset(&flow->key) + flow->length)) {
				new_container = UF_CONTAINER_EXTENTS;
				write_f = item_plugin_by_id(EXTENT_POINTER_ID)->s.file.write;
			} else {
				new_container = UF_CONTAINER_TAILS;
				write_f = item_plugin_by_id(TAIL_ID)->s.file.write;
			}
			break;

		case UF_CONTAINER_EXTENTS:
			write_f = item_plugin_by_id(EXTENT_POINTER_ID)->s.file.write;
			new_container = cur_container;
			break;

		case UF_CONTAINER_TAILS:
			if (should_have_notail(uf_info, get_key_offset(&flow->key) + flow->length)) {
				longterm_unlock_znode(&lh);
				result = tail2extent(uf_info);
				if (result)
					return result;
				unset_hint(&hint);
				continue;
			}
			write_f = item_plugin_by_id(TAIL_ID)->s.file.write;
			new_container = cur_container;
			break;

		default:			
			longterm_unlock_znode(&lh);
			return RETERR(-EIO);
		}
		
		result = zload(lh.node);
		if (result) {
			longterm_unlock_znode(&lh);
			return result;
		}
		loaded = lh.node;
		/*
		 * XXX NIKITA coord has to be re-validated after zload()
		 */

		result = write_f(uf_info->inode, flow, &hint, 0/* not grabbed */, how_to_write(&hint.coord, &flow->key));

		assert("nikita-3142", get_current_context()->grabbed_blocks == 0);
		if (cur_container == UF_CONTAINER_EMPTY && to_write != flow->length) {
			/* file was empty and we have written something and we are having exclusive access to the file -
			   change file state */
			assert("vs-1195", (new_container == UF_CONTAINER_TAILS ||
					   new_container == UF_CONTAINER_EXTENTS));
			uf_info->container = new_container;
		}
		zrelse(loaded);
		done_lh(&lh);
		if (result && result != -E_REPEAT)
			break;
		preempt_point();
	}
	if (result == -EEXIST)
		printk("write returns EEXIST!\n");
	save_file_hint(file, &hint);

	/* if nothing were written - there must be an error */
	assert("vs-951", ergo((to_write == flow->length), result < 0));
	assert("vs-1110", get_current_context()->grabbed_blocks == 0);

	return (to_write - flow->length) ? (to_write - flow->length) : result;
}

/* make flow and write data (@buf) to the file. If @buf == 0 - hole of size @count will be created. This is called with
   uf_info->latch either read- or write-locked */
static loff_t
write_flow(struct file *file, unix_file_info_t *uf_info, const char *buf, loff_t count, loff_t pos)
{
	int result;
	flow_t flow;

	assert("vs-1251", inode_file_plugin(uf_info->inode)->flow_by_inode == flow_by_inode_unix_file);
	
	result = flow_by_inode_unix_file(uf_info->inode, (char *)buf, 1 /* user space */, count, pos, WRITE_OP, &flow);
	if (result)
		return result;

	return append_and_or_overwrite(file, uf_info, &flow);
}

static void
drop_access(unix_file_info_t *uf_info)
{
	if (uf_info->exclusive_use)
		drop_exclusive_access(uf_info);
	else
		drop_nonexclusive_access(uf_info);
}

void balance_dirty_page_unix_file(struct inode *object)
{
	/* balance dirty pages periodically */
	balance_dirty_pages_ratelimited(object->i_mapping);
}

/* plugin->u.file.write */
ssize_t
write_unix_file(struct file *file, /* file to write to */
		const char *buf, /* address of user-space buffer */
		size_t count, /* number of bytes to write */
		loff_t *off /* position to write which */)
{
	struct inode *inode;
	int result;
	ssize_t written;
	loff_t pos;
	unix_file_info_t *uf_info;

	inode = file->f_dentry->d_inode;

	down(&inode->i_sem);

	assert("nikita-3032", schedulable());

	result = generic_write_checks(inode, file, off, &count, 0);
	if (unlikely(result != 0)) {
		up(&inode->i_sem);
		return result;
	}

	if (unlikely(count == 0)) {
		up(&inode->i_sem);
		return 0;
	}

	/* linux's VM requires this. See mm/vmscan.c:shrink_list() */
/* setting this causes the following behavior.  If we request memory, and vmscan finds a dirty page from the same device
 * we are writing to, and the device is congested, vmscan submits the page to this congested device, which causes the IO
 * to block, which causes vmscan to block and perform slowly, which throttles this function. This is in addition to
 * balance_dirty throttling.  I can't say we understand why it is needed.  -Hans */
	current->backing_dev_info = inode->i_mapping->backing_dev_info;

	/* UNIX behavior: clear suid bit on file modification */
	remove_suid(file->f_dentry);

	assert("vs-855", count > 0);
	assert("vs-947", !inode_get_flag(inode, REISER4_NO_SD));
	uf_info = unix_file_inode_data(inode);
	if (inode->i_size == 0)
		get_exclusive_access(uf_info);
	else
		get_nonexclusive_access(uf_info);

	/* estimation for write is entrusted to write item plugins */

	pos = *off;

	if (inode->i_size < pos) {
		/* pos is set past real end of file */
		result = append_hole(uf_info, pos);
		if (result) {
			drop_access(uf_info);
			current->backing_dev_info = 0;
			up(&inode->i_sem);
			return result;
		}
		assert("vs-1081", pos == inode->i_size);
	}

	/* write user data to the file */
	written = write_flow(file, uf_info, buf, count, pos);
	drop_access(uf_info);
	/* linux's VM requires this. See mm/vmscan.c:shrink_list() */
	current->backing_dev_info = 0;

	up(&inode->i_sem);
	if (written < 0) {
		if (written == -EEXIST)
			printk("write_file returns EEXIST!\n");
		return written;
	}

	/* update position in a file */
	*off = pos + written;
	/* return number of written bytes */
	return written;
}

/* plugin->u.file.release() convert all extent items into tail items if
   necessary */
int
release_unix_file(struct inode *object, struct file *file)
{
	unix_file_info_t *uf_info;
	int result;

	uf_info = unix_file_inode_data(object);
	result = 0;

	get_exclusive_access(uf_info);
	if (atomic_read(&file->f_dentry->d_count) == 1 &&
	    uf_info->container == UF_CONTAINER_EXTENTS &&
	    !should_have_notail(uf_info, object->i_size)) {
		result = extent2tail(uf_info);
		if (result != 0) {
			warning("nikita-3233", "Failed to convert in %s (%llu)",
				__FUNCTION__, get_inode_oid(object));
			print_inode("inode", object);
		}
	}
	drop_exclusive_access(uf_info);
	return 0;
}

struct page *
unix_file_filemap_nopage(struct vm_area_struct *area, unsigned long address, int unused)
{
	struct page *page;
	struct inode *inode;

	inode = area->vm_file->f_dentry->d_inode;
	get_nonexclusive_access(unix_file_inode_data(inode));
	page = filemap_nopage(area, address, unused);
	drop_nonexclusive_access(unix_file_inode_data(inode));
	return page;
}

static struct vm_operations_struct unix_file_vm_ops = {
	.nopage = unix_file_filemap_nopage,
};

static void
set_file_notail(struct inode *inode)
{
	reiser4_inode *state;
	tail_plugin   *tplug;

	state = reiser4_inode_data(inode);
	tplug = tail_plugin_by_id(NEVER_TAIL_ID);
	plugin_set_tail(&state->pset, tplug);
	inode_set_plugin(inode, tail_plugin_to_plugin(tplug));
}

/* if file is built of tails - convert it to extents */
static int
unpack(struct inode *inode, int forever)
{
	int            result = 0;
	unix_file_info_t *uf_info;

	uf_info = unix_file_inode_data(inode);
	
	get_exclusive_access(uf_info);

	if (uf_info->container == UF_CONTAINER_UNKNOWN) {
		loff_t file_size;

		result = find_file_size(inode, &file_size);
	}
	assert("vs-1074", ergo(result == 0, uf_info->container != UF_CONTAINER_UNKNOWN));
	if (result == 0) {
		if (uf_info->container == UF_CONTAINER_TAILS)
			result = tail2extent(uf_info);
		if (result == 0 && forever)
			set_file_notail(inode);
	}

	drop_exclusive_access(uf_info);

	if (result == 0) {
		__u64 tograb;

		grab_space_enable();
		tograb = inode_file_plugin(inode)->estimate.update(inode);
		result = reiser4_grab_space(tograb, BA_CAN_COMMIT, __FUNCTION__);
		if (result == 0)
			update_atime(inode);
	}

	return result;
}

/* plugin->u.file.ioctl */
int
ioctl_unix_file(struct inode *inode, struct file *filp UNUSED_ARG, unsigned int cmd, unsigned long arg UNUSED_ARG)
{
	int result;

	switch (cmd) {
	case REISER4_IOC_UNPACK:
		result = unpack(inode, 1);
		break;

	default:
		result = RETERR(-ENOTTY);
		break;
	}
	return result;
}

/* plugin->u.file.mmap
   make sure that file is built of extent blocks. An estimation is in tail2extent */
int
mmap_unix_file(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode;
	int result;

	inode = file->f_dentry->d_inode;

	result = unpack(inode, 0);
	if (result)
		return result;

	result = generic_file_mmap(file, vma);
	if (result)
		return result;
	vma->vm_ops = &unix_file_vm_ops;
	return 0;
}

/* plugin->u.file.get_block */
int
get_block_unix_file(struct inode *inode,
		    sector_t block, struct buffer_head *bh_result, int create UNUSED_ARG)
{
	int result;
	reiser4_key key;
	hint_t hint;
	lock_handle lh;
	item_plugin *iplug;

	assert("vs-1091", create == 0);
	key_by_inode_unix_file(inode, (loff_t) block * current_blocksize, &key);

	hint_init_zero(&hint, &lh);
	result = find_file_item(&hint, &key, ZNODE_READ_LOCK, CBK_UNIQUE, 0/* ra_info */, unix_file_inode_data(inode));
	if (result != CBK_COORD_FOUND || hint.coord.base_coord.between != AT_UNIT) {
		done_lh(&lh);
		return result;
	}
	coord_clear_iplug(&hint.coord.base_coord);
	result = zload(hint.coord.base_coord.node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(&hint.coord.base_coord);
	if (!hint.coord.valid)
		validate_extended_coord(&hint.coord,
					(loff_t) block << PAGE_CACHE_SHIFT);
	if (iplug->s.file.get_block)
		result = iplug->s.file.get_block(&hint.coord, block, bh_result);
	else
		result = RETERR(-EINVAL);

	zrelse(hint.coord.base_coord.node);
	done_lh(&lh);
	return result;
}

/* plugin->u.file.flow_by_inode
   initialize flow (key, length, buf, etc) */
int
flow_by_inode_unix_file(struct inode *inode /* file to build flow for */ ,
			char *buf /* user level buffer */ ,
			int user  /* 1 if @buf is of user space, 0 - if it is kernel space */ ,
			loff_t size /* buffer size */ ,
			loff_t off /* offset to start operation(read/write) from */ ,
			rw_op op /* READ or WRITE */ ,
			flow_t *flow /* resulting flow */ )
{
	assert("nikita-1100", inode != NULL);

	flow->length = size;
	flow->data = buf;
	flow->user = user;
	flow->op = op;
	assert("nikita-1931", inode_file_plugin(inode) != NULL);
	assert("nikita-1932", inode_file_plugin(inode)->key_by_inode == key_by_inode_unix_file);
	/* calculate key of write position and insert it into flow->key */
	return key_by_inode_unix_file(inode, off, &flow->key);
}

/* plugin->u.file.key_by_inode */
int
key_by_inode_unix_file(struct inode *inode, loff_t off, reiser4_key *key)
{
	key_init(key);
	set_key_locality(key, reiser4_inode_data(inode)->locality_id);
	set_key_ordering(key, get_inode_ordering(inode));
	set_key_objectid(key, get_inode_oid(inode));/*FIXME: inode->i_ino */
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) off);
	return 0;
}

/* plugin->u.file.set_plug_in_sd = NULL
   plugin->u.file.set_plug_in_inode = NULL
   plugin->u.file.create_blank_sd = NULL */

/* plugin->u.file.delete */
int
delete_unix_file(struct inode *inode)
{
	int result;

	/* FIXME: file is truncated to 0 already */
	assert("vs-1099", inode->i_nlink == 0);
	if (inode->i_size) {
		result = truncate_file(inode, 0, 0/* no stat data update */);
		if (result) {
			warning("nikita-2848",
				"Cannot truncate unnamed file %lli: %i",
				get_inode_oid(inode), result);
			return result;
		}
	}
	return delete_file_common(inode);
}

/*
   plugin->u.file.add_link = add_link_common
   plugin->u.file.rem_link = NULL */

/* plugin->u.file.owns_item
   this is common_file_owns_item with assertion */
/* Audited by: green(2002.06.15) */
int
owns_item_unix_file(const struct inode *inode	/* object to check against */ ,
		    const coord_t *coord /* coord to check */ )
{
	int result;

	result = owns_item_common(inode, coord);
	if (!result)
		return 0;
	if (item_type_by_coord(coord) != ORDINARY_FILE_METADATA_TYPE)
		return 0;
	assert("vs-547", (item_id_by_coord(coord) == EXTENT_POINTER_ID || item_id_by_coord(coord) == TAIL_ID ||
			  item_id_by_coord(coord) == FROZEN_EXTENT_POINTER_ID || item_id_by_coord(coord) == FROZEN_TAIL_ID));
	return 1;
}

/* plugin->u.file.setattr method */
/* This calls inode_setattr and if truncate is in effect it also takes
   exclusive inode access to avoid races */
int
setattr_unix_file(struct inode *inode,	/* Object to change attributes */
		  struct iattr *attr /* change description */ )
{
	int result;

	if (attr->ia_valid & ATTR_SIZE) {
		/* truncate does reservation itself and requires exclusive access obtained */
		if (inode->i_size != attr->ia_size) {
			loff_t old_size;

			inode_check_scale(inode, inode->i_size, attr->ia_size);

			old_size = inode->i_size;

			get_exclusive_access(unix_file_inode_data(inode));
			/* VS-FIXME-HANS: explain why setattr calls truncate file */
			result = truncate_file(inode, attr->ia_size, 1/* update stat data */);
			if (!result) {
				/* items are removed already. inode_setattr will call vmtruncate to invalidate truncated
				   pages and unix_file_truncate which will do nothing. FIXME: is this necessary? */
				INODE_SET_FIELD(inode, i_size, old_size);
				result = inode_setattr(inode, attr);
			}
			drop_exclusive_access(unix_file_inode_data(inode));
		} else
			result = 0;
	} else {
		result = setattr_reserve(tree_by_inode(inode));
		if (!result) {
			result = inode_setattr(inode, attr);
			if (!result)
				/* "capture" inode */
				result = reiser4_mark_inode_dirty(inode);
			all_grabbed2free(__FUNCTION__);
		}
	}
	return result;
}

/* plugin->u.file.can_add_link = common_file_can_add_link */
/* VS-FIXME-HANS: why does this always resolve to extent pointer?  this wrapper serves what purpose?  get rid of it. */
/* plugin->u.file.readpages method */
void
readpages_unix_file(struct file *file, struct address_space *mapping,
		    struct list_head *pages)
{
	reiser4_file_fsdata *fsdata;
	item_plugin *iplug;

	assert("vs-1282", unix_file_inode_data(mapping->host)->container == UF_CONTAINER_EXTENTS);

	fsdata = reiser4_get_file_fsdata(file);
	iplug = item_plugin_by_id(EXTENT_POINTER_ID);
	iplug->s.file.readpages(fsdata->reg.coord, mapping, pages);
	return;
}

/* plugin->u.file.init_inode_data */
void
init_inode_data_unix_file(struct inode *inode,
			  reiser4_object_create_data *crd, int create)
{
	unix_file_info_t *data;

	data = unix_file_inode_data(inode);
	data->container = create ? UF_CONTAINER_EMPTY : UF_CONTAINER_UNKNOWN;
	rw_latch_init(&data->latch);
	data->tplug = inode_tail_plugin(inode);
	data->inode = inode;
	data->exclusive_use = 0;
#if REISER4_DEBUG
	data->ea_owner = 0;
#endif
	init_inode_ordering(inode, crd, create);
}
/* VS-FIXME-HANS: what is pre deleting all about? */
/* plugin->u.file.pre_delete */
int
pre_delete_unix_file(struct inode *inode)
{
	return truncate_file(inode, 0/* size */, 0/* no stat data update */);
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
