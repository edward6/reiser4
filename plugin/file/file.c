/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../../forward.h"
#include "../../debug.h"
#include "../../key.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../seal.h"
#include "../../txnmgr.h"
#include "../../jnode.h"
#include "../../znode.h"
#include "../../tree_walk.h"
#include "../../tree.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../super.h"
#include "../../page_cache.h"

#include "../plugin_header.h"
#include "../item/item.h"
#include "../plugin.h"
#include "../object.h"

#include "file.h"

#include <linux/pagemap.h>
#include <linux/types.h>
#include <linux/fs.h>		/* for struct file  */
#include <linux/mm.h>		/* for struct page */
#include <linux/buffer_head.h>	/* for struct buffer_head */

/* this file contains:
   - file_plugin methods of reiser4 unix file (REGULAR_FILE_PLUGIN_ID) */

/* look for item of file @inode corresponding to @key */

#ifdef PSEUDO_CODE_CAN_COMPILE
find_item()
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
#endif

#define SEARCH_BIAS FIND_MAX_NOT_MORE_THAN
/*#define SEARCH_BIAS FIND_EXACT*/

/* get key of item next to one @coord is set to */
static reiser4_key *
get_next_item_key(const coord_t * coord, reiser4_key * next_key)
{
	if (coord->item_pos == node_num_items(coord->node) - 1) {
		/* get key of next item if it is in right neighbor */
		UNDER_SPIN_VOID(dk, znode_get_tree(coord->node), *next_key = *znode_get_rd_key(coord->node));
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

static void
check_coord(const coord_t * coord, const reiser4_key * key)
{
	coord_t twin;

	if (!REISER4_DEBUG)
		return;
	node_plugin_by_node(coord->node)->lookup(coord->node, key, FIND_MAX_NOT_MORE_THAN, &twin);
	assert("vs-1004", !memcmp(coord, &twin, sizeof (twin)));
}

static int
less_than_ldk(znode * node, const reiser4_key * key)
{
	return UNDER_SPIN(dk, current_tree, keylt(key, znode_get_ld_key(node)));
}

static int
equal_to_rdk(znode * node, const reiser4_key * key)
{
	return UNDER_SPIN(dk, current_tree, keyeq(key, znode_get_rd_key(node)));
}

static int
less_than_rdk(znode * node, const reiser4_key * key)
{
	return UNDER_SPIN(dk, current_tree, keylt(key, znode_get_rd_key(node)));
}

static int
item_of_that_file(const coord_t * coord, const reiser4_key * key)
{
	reiser4_key max_possible;

	assert("vs-1011", coord->iplug->b.max_key_inside);
	return keylt(key, coord->iplug->b.max_key_inside(coord, &max_possible));
}

static int
item_contains_key(const coord_t * coord, const reiser4_key * key)
{
	reiser4_key tmp;

	assert("vs-1082", coord_is_existing_item(coord));
	assert("vs-1011", coord->iplug->b.real_max_key_inside);
	return (keyge(key, item_key_by_coord(coord, &tmp)) &&
		keyle(key, coord->iplug->b.real_max_key_inside(coord, &tmp)));
}

static int
can_append(const coord_t * coord, const reiser4_key * key)
{
	reiser4_key next;

	assert("vs-1012", coord->iplug->b.real_max_key_inside);
	coord->iplug->b.real_max_key_inside(coord, &next);
	set_key_offset(&next, get_key_offset(&next) + 1);
	return keyeq(key, &next);
}

/* coord->node is locked and loaded. Lock its right neighbor, zload it
   and zrelse coord->node and unlock it and set coord to first unit
   in obtained right neighbor */
static int
goto_right_neighbor(coord_t * coord, lock_handle * lh)
{
	int result;
	lock_handle lh_right;

	init_lh(&lh_right);
	result = reiser4_get_right_neighbor(&lh_right, coord->node, ZNODE_WRITE_LOCK, GN_DO_READ);
	if (result) {
		done_lh(&lh_right);
		return result;
	}

	result = zload(lh_right.node);
	if (result) {
		done_lh(&lh_right);
		return result;
	}

	zrelse(coord->node);
	done_lh(lh);

	coord_init_first_unit(coord, lh_right.node);
	move_lh(lh, &lh_right);

	return 0;

}

write_mode how_to_write(coord_t * coord, lock_handle * lh, const reiser4_key * key)
{
	write_mode result;
	reiser4_key check;

	result = zload(coord->node);
	if (result)
		return result;

	if (less_than_ldk(coord->node, key)) {
		assert("vs-1014", get_key_offset(key) == 0);

		coord_init_before_first_item(coord, coord->node);
		/* FIXME-VS: BEFORE_ITEM should be fine, but node's
		   lookup returns BEFORE_UNIT */
		coord->between = BEFORE_UNIT;
		result = FIRST_ITEM;
		goto ok;
	}

	if (equal_to_rdk(coord->node, key)) {
		/* FIXME-VS: switch to right neighbor. The problem is
		   that callers of this currently do not expect
		   coord->node to change
		*/
		result = goto_right_neighbor(coord, lh);
		if (result) {
			zrelse(coord->node);
			return result;
		}
		assert("vs-1027", keyeq(key, item_key_by_coord(coord, &check)));
		assert("vs-1028", coord->item_pos == 0);
		assert("vs-1030", coord->unit_pos == 0);
		assert("vs-1029", coord->between == AT_UNIT);

		result = OVERWRITE_ITEM;
		goto ok;
	}

	assert("vs-1013", less_than_rdk(coord->node, key));

	if (node_is_empty(coord->node)) {
		assert("vs-879", znode_get_level(coord->node) == LEAF_LEVEL);
		assert("vs-880", get_key_offset(key) == 0);
		assert("vs-999", UNDER_SPIN(dk, current_tree, keyeq(key, znode_get_ld_key(coord->node))));
		assert("vs-1000", UNDER_SPIN(dk, current_tree, keylt(key, znode_get_rd_key(coord->node))));
		assert("vs-1002", coord->between == EMPTY_NODE);
		result = FIRST_ITEM;
		goto ok;
	}

	if (coord->item_pos >= node_num_items(coord->node)) {
		info("how_to_write: "
		     "coord->item_pos is out of range: %u (%u)\n", coord->item_pos, node_num_items(coord->node));
		return RESEARCH;
	}

	/* make sure that coord is set properly. Should it be? */
	coord->between = AT_UNIT;
	assert("vs-1007", keyle(item_key_by_coord(coord, &check), key));
	assert("vs-1008", keylt(key, get_next_item_key(coord, &check)));

	if ((item_is_tail(coord) || item_is_extent(coord)) && item_of_that_file(coord, key)) {
		/* @coord is set to item we have to write to */
		if (can_append(coord, key)) {
			/* @key is adjacent to last key of item @coord */
			coord->unit_pos = coord_last_unit_pos(coord);
			coord->between = AFTER_UNIT;
			result = APPEND_ITEM;
			goto ok;
		}

		if (coord->iplug->b.key_in_item(coord, key)) {
			/* @key is in item. coord->unit_pos is set
			   properly */
			coord->between = AT_UNIT;
			result = OVERWRITE_ITEM;
			goto ok;
		}
		impossible("vs-1015", "does this even happen?");
	}

	coord->between = AFTER_ITEM;
	result = FIRST_ITEM;
ok:
	check_coord(coord, key);
	zrelse(coord->node);
	return result;
}

int
find_next_item(struct sealed_coord *hint, const reiser4_key * key,	/* key of position in a file of
									 * next read/write */
	       coord_t * coord,	/* on entry - initilized by 0s or
				   coordinate (locked node and position in
				   it) on which previous read/write
				   operated, on exit - coordinate of
				   position specified by @key */
	       lock_handle * lh,	/* initialized by 0s if @coord is zeroed
					 * or othewise lock handle of locked node
					 * in @coord, on exit - lock handle of
					 * locked node in @coord */
	       znode_lock_mode lock_mode	/* which lock (read/write) to put
						 * on returned node */ ,
	       __u32 cbk_flags
	       /* coord_by_key flags: CBK_UNIQUE [| CBK_FOR_INSERT] */ )
{
	int result;

	/* collect statistics on the number of calls to this function */
	reiser4_stat_file_add(find_next_item);

	if (hint) {
		hint->lock = lock_mode;
		result = hint_validate(hint, key, coord, lh);
		if (!result) {
			
			reiser4_stat_file_add(find_next_item_via_seal);
			return CBK_COORD_FOUND;
		}
	}

	/* collect statistics on the number of calls to this function which did
	   not get optimized */
	reiser4_stat_file_add(find_next_item_via_cbk);
	result = coord_by_key(current_tree, key, coord, lh, lock_mode, SEARCH_BIAS, TWIG_LEVEL, LEAF_LEVEL, cbk_flags);
	return result;
}

/* plugin->u.file.write_flowom = NULL
   plugin->u.file.read_flow = NULL */

/* find position of last byte of last item of the file plus 1 */
static int
find_file_size(struct inode *inode, loff_t * file_size)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

	inode_file_plugin(inode)->key_by_inode(inode, get_key_offset(max_key()), &key);

	coord_init_zero(&coord);
	init_lh(&lh);
	result = find_next_item(0, &key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
	if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
		/* error occured */
		done_lh(&lh);
		return result;
	}

	if (result == CBK_COORD_NOTFOUND) {
		/* there are no items of this file */
		done_lh(&lh);
		*file_size = 0;
		return 0;
	}

	/* there are items of this file (at least one) */
	result = zload(coord.node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(&coord);

	assert("vs-853", iplug->b.real_max_key_inside);
	iplug->b.real_max_key_inside(&coord, &key);

	*file_size = get_key_offset(&key) + 1;

	if (znode_get_level(coord.node) == LEAF_LEVEL)
		inode_set_flag(inode, REISER4_HAS_TAIL);
	else
		inode_clr_flag(inode, REISER4_HAS_TAIL);
	inode_set_flag(inode, REISER4_TAIL_STATE_KNOWN);

	zrelse(coord.node);
	done_lh(&lh);
	return 0;
}

static int
filler(void *vp, struct page *page)
{
	return unix_file_readpage(vp, page);
}

/* part of unix_file_truncate: it is called when truncate is used to make
   file shorter */
static int
shorten_file(struct inode *inode)
{
	int result;
	reiser4_key from, to;
	struct page *page;
	int padd_from;
	unsigned long index;
	char *kaddr;

	inode_file_plugin(inode)->key_by_inode(inode, inode->i_size, &from);
	to = from;
	set_key_offset(&to, get_key_offset(max_key()));

	/* all items of ordinary reiser4 file are grouped together. That is why
	   we can use cut_tree. Plan B files (for instance) can not be
	   truncated that simply */
	result = cut_tree(tree_by_inode(inode), &from, &to);
	if (result)
		return result;

	if (inode->i_size == 0) {
		inode_clr_flag(inode, REISER4_TAIL_STATE_KNOWN);
		return 0;
	}


	if (inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN) && inode_get_flag(inode, REISER4_HAS_TAIL))
		/* file is built of tail items. No need to worry about zeroing
		   last page after new file end */
		return 0;

	padd_from = inode->i_size & (PAGE_CACHE_SIZE - 1);
	if (!padd_from)
		/* file is truncate to page boundary */
		return 0;

	/* last page is partially truncated - zero its content */
	index = (inode->i_size >> PAGE_CACHE_SHIFT);
	page = read_cache_page(inode->i_mapping, index, filler, 0);
	if (IS_ERR(page)) {
		if (likely(PTR_ERR(page) == -EINVAL)) {
			/* looks like file is built of tail items */
			return 0;
		}
		return PTR_ERR(page);
	}
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		page_cache_release(page);
		return -EIO;
	}
	result = unix_file_writepage_nolock(page);
	if (result) {
		page_cache_release(page);
		return result;
	}

	assert("vs-1066", PageLocked(page));
	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + padd_from, 0, PAGE_CACHE_SIZE - padd_from);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	reiser4_unlock_page(page);
	page_cache_release(page);

	return 0;
}

/* part of unix_file_truncate: it is called when truncate is used to make file
   longer */
static loff_t append_and_or_overwrite(struct file *file, struct inode *inode, flow_t * f);

/* Add hole to end of file. @cur_size is current file size. Length of hole is new_size - cur_size. This is called with
   NEA obtained */
static int
expand_file(struct inode *inode, loff_t cur_size, loff_t new_size)
{
	int result;
	file_plugin *fplug;
	flow_t f;
	loff_t written;

	assert("vs-909", new_size > cur_size);

	fplug = inode_file_plugin(inode);
	result = fplug->flow_by_inode(inode, 0 /* buf */ , 1 /* user space */ ,
				      new_size - cur_size /* count */ ,
				      cur_size /* pos */ ,
				      WRITE_OP, &f);
	if (result)
		return result;

	written = append_and_or_overwrite(0, inode, &f);
	if (written != new_size - cur_size) {
		/* we were not able to write expand file to desired size */
		if (written < 0)
			return (int) written;
		return -ENOSPC;
	}
	assert("vs-1081", new_size == inode->i_size);
	return 0;
}

static inline loff_t
abs(loff_t v)
{
	return v < 0 ? -v : v;
}


/* Performs estimate of reserved blocks for truncate operation. Note,
   that all estimate fundctions do not count the amount of blocks should
   be reserved for internal blocks */
reiser4_block_nr unix_file_estimate_truncate(struct inode *inode, loff_t old_size) {
    tail_plugin *tail_plugin;
    
    assert("umka-1233", inode != NULL);
   
    /* Here should be called tail policy plugin in order to handle correctly
       the situation of converting extent to tail */
    tail_plugin = inode_tail_plugin(inode);
    
    assert("umka-1234", tail_plugin != NULL);
    return tail_plugin->estimate(inode, abs(old_size - inode->i_size), 1) + 1;
}

/* plugin->u.file.truncate
   this is called with exclusive access obtained in unix_file_setattr */
int
unix_file_truncate(struct inode *inode, loff_t size)
{
	int result;
	loff_t file_size;
	reiser4_block_nr needed;

	inode->i_size = size;

	result = find_file_size(inode, &file_size);
	if (result)
		return result;
	
	needed = unix_file_estimate_truncate(inode, file_size);
	
	if (reiser4_grab_space_exact(needed, (size <= result)) != 0) {
//		drop_nonexclusive_access (inode);
		return -ENOSPC;
	}
	
	trace_on(TRACE_RESERVE, "file truncate grabs %llu blocks.\n", needed);

	if (file_size < inode->i_size) {
		ea2nea(inode);
		result = expand_file(inode, file_size, inode->i_size);
		nea2ea(inode);
	} else {
		result = shorten_file(inode);
	}
	
	if (!result) {
		result = reiser4_write_sd(inode);
		if (result)
			warning("vs-638", "updating stat data failed: %i", result);
	}
	return result;
}

/* plugin->u.write_sd_by_inode = common_file_save */

/* get access hint (seal, coord, key, level) stored in reiser4 private part of
   struct file if it was stored in a previous access to the file */
static int
load_file_hint(struct file *file, struct sealed_coord *hint)
{
	reiser4_file_fsdata *fsdata;

	xmemset(hint, 0, sizeof (hint));
	if (file) {
		fsdata = reiser4_get_file_fsdata(file);
		if (IS_ERR(fsdata))
			return PTR_ERR(fsdata);

		if (seal_is_set(&fsdata->reg.hint.seal))
			*hint = fsdata->reg.hint;
	}
	return 0;
}

/* this copies hint for future tree accesses back to reiser4 private part of
   struct file */
static void
save_file_hint(struct file *file, const struct sealed_coord *hint)
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
set_hint(struct sealed_coord *hint, const reiser4_key * key, const coord_t * coord)
{

	if (coord->node == NULL) {
		unset_hint(hint);
		return;
	}
	assert("vs-966", znode_is_locked(coord->node));
	seal_init(&hint->seal, coord, key);
	hint->coord = *coord;
	if ((!less_than_rdk(coord->node, key) || less_than_ldk(coord->node, key)) &&
	    /* FIXME-NIKITA: temporary fix. Due to extents on the twig level,
	       it is possible that key we are trying to write at is less than
	       left delimiting key of the node we came into.
	    */
	    !coord_is_before_leftmost(coord)) {
		unset_hint(hint);
		return;
	}
	hint->key = *key;
	hint->level = znode_get_level(coord->node);
	hint->lock = znode_is_wlocked(coord->node) ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK;
}

void
unset_hint(struct sealed_coord *hint)
{
	memset(hint, 0, sizeof (hint));
}

int
hint_is_set(const struct sealed_coord *hint)
{
	return seal_is_set(&hint->seal);
}

int
hint_validate(struct sealed_coord *hint, const reiser4_key * key, coord_t * coord, lock_handle * lh)
{
	int result;

	if (!hint || !hint_is_set(hint) || !keyeq(key, &hint->key))
		/* hint either not set or set for different key */
		return -EAGAIN;

	result = seal_validate(&hint->seal, &hint->coord, key,
			       hint->level, lh, FIND_MAX_NOT_MORE_THAN, hint->lock, ZNODE_LOCK_LOPRI);
	if (result)
		return result;
	coord_dup_nocheck(coord, &hint->coord);
	return 0;
}

/* nolock means: do not get EA or NEA on a file the page belongs to (it is obtained already either in
   unix_file_writepage or in tail2extent). Lock page after long term znode lock is obtained. Return with page locked */
int
unix_file_writepage_nolock(struct page *page)
{
	int result;
	coord_t coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin *iplug;
	struct sealed_coord hint;
	znode *loaded;

	assert("vs-1064", !PageLocked(page));
	assert("vs-1065", page->mapping && page->mapping->host);

	/* get key of first byte of the page */
	unix_file_key_by_inode(page->mapping->host, (loff_t) page->index << PAGE_CACHE_SHIFT, &key);

	while (1) {
		coord_init_zero(&coord);
		init_lh(&lh);
		result = find_next_item(&hint, &key, &coord, &lh, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			done_lh(&lh);
			break;
		}
		result = zload(coord.node);
		if (result) {
			done_lh(&lh);
			break;
		}
		loaded = coord.node;

		reiser4_lock_page(page);

		/* get plugin of extent item */
		iplug = item_plugin_by_id(EXTENT_POINTER_ID);
		result = iplug->s.file.writepage(&coord, &lh, page);
		assert("vs-982", PageLocked(page));
		if (result == -EAGAIN) {
			info("writepage_nolock: item writepage returned EAGAIN\n");
			reiser4_unlock_page(page);
			zrelse(loaded);
			done_lh(&lh);
			continue;
		}
		if (!result && coord.node)
			set_hint(&hint, &key, &coord);
		else
			unset_hint(&hint);
		zrelse(loaded);
		done_lh(&lh);

		return result;
	}
	reiser4_lock_page(page);
	return result;
}

/* plugin->u.file.writepage */
int
unix_file_writepage(struct page *page)
{
	int result;
	struct inode *inode;
	tail_plugin *tail_plugin;
	reiser4_block_nr needed;

	assert("vs-1032", PageLocked(page));
	assert("vs-1084", page->mapping && page->mapping->host);
	assert("vs-1085", (page->mapping->host->i_size > ((loff_t) page->index << PAGE_CACHE_SHIFT)));

	if (PagePrivate(page))
		return 0;

	inode = page->mapping->host;

	tail_plugin = inode_tail_plugin(inode);

	assert("umka-1254", tail_plugin != NULL);

	needed = tail_plugin->estimate(inode, 1, 0);

	trace_on(TRACE_RESERVE, " write page grabbed %llu blocks\n", 
		needed);
	
	/* to keep order of locks right we have to unlock page before
	   call to get_nonexclusive_access */
	page_cache_get(page);
	reiser4_unlock_page(page);

	get_nonexclusive_access(inode);
	if (inode->i_size <= ((loff_t) page->index << PAGE_CACHE_SHIFT)) {
		/* page is out of file */
		drop_nonexclusive_access(inode);
		reiser4_lock_page(page);
		page_cache_release(page);
		return -EIO;
	}

	if ((result = reiser4_grab_space_exact(needed, 0)) != 0)
		goto out;

	result = unix_file_writepage_nolock(page);

	assert("vs-1068", PageLocked(page));

	all_grabbed2free();
 out:
	drop_nonexclusive_access(inode);
	page_cache_release(page);
	return result;
}

/* plugin->u.file.readpage page must be not out of file. This is always called
   with NEA (reiser4's NonExclusive Access) obtained */
int
unix_file_readpage(struct file *file, struct page *page)
{
	int result;
	coord_t coord;
	lock_handle lh;
	reiser4_key key;
	item_plugin *iplug;
	struct sealed_coord hint;

	assert("vs-1062", PageLocked(page));
	assert("vs-1061", page->mapping && page->mapping->host);
	assert("vs-1078", (page->mapping->host->i_size > ((loff_t) page->index << PAGE_CACHE_SHIFT)));

	/* get key of first byte of the page */
	unix_file_key_by_inode(page->mapping->host, (loff_t) page->index << PAGE_CACHE_SHIFT, &key);

	/* look for file metadata corresponding to first byte of page
	   FIXME-VS: seal might be used here */
	result = load_file_hint(file, &hint);
	if (result)
		return result;

	coord_init_zero(&coord);
	init_lh(&lh);
	reiser4_unlock_page(page);
	result = find_next_item(&hint, &key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
	reiser4_lock_page(page);
	if (result != CBK_COORD_FOUND) {
		/* this indicates file corruption */
		done_lh(&lh);
		return result;
	}

	if (PageUptodate(page)) {
		info("unix_file_readpage: page became already uptodate\n");
		done_lh(&lh);
		unlock_page(page);
		return 0;
	}

	result = zload(coord.node);
	if (result) {
		done_lh(&lh);
		return result;
	}

	/*assert("nikita-2720", page->mapping == file->f_dentry->d_inode->i_mapping); */

	if (!coord_is_existing_unit(&coord)) {
		/* this indicates corruption */
		warning("vs-280",
			"Looking for page %lu of file %lu (size %lli). "
			"No file items found (%d). "
			"File is corrupted?\n",
			page->index, page->mapping->host->i_ino, page->mapping->host->i_size, result);
		zrelse(coord.node);
		done_lh(&lh);
		return -EIO;
	}

	/* get plugin of found item or use plugin if extent if there are no
	   one */
	iplug = item_plugin_by_coord(&coord);
	if (iplug->s.file.readpage)
		result = iplug->s.file.readpage(&coord, &lh, page);
	else
		result = -EINVAL;

	if (!result)
		set_hint(&hint, &key, &coord);
	else
		unset_hint(&hint);
	zrelse(coord.node);
	done_lh(&lh);

	save_file_hint(file, &hint);

	assert("vs-979", ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
	return result;
}

static void set_tail_state(struct inode *inode, item_id id)
{
	if (!inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN)) {
		inode_set_flag(inode, REISER4_TAIL_STATE_KNOWN);
		if (id == TAIL_ID)
			inode_set_flag(inode, REISER4_HAS_TAIL);
		else
			inode_clr_flag(inode, REISER4_HAS_TAIL);
	} else {
		/* tail state is claimed to be known, check whether it is known right */
		assert("vs-1089", ((id == TAIL_ID && inode_get_flag(inode, REISER4_HAS_TAIL)) ||
				   (id == EXTENT_POINTER_ID && !inode_get_flag(inode, REISER4_HAS_TAIL))));
	}
}

/* returns 1 if file of that size (@new_size) has to be stored in unformatted
   nodes */
/* Audited by: green(2002.06.15) */
static int
should_have_notail(struct inode *inode, loff_t new_size)
{
	if (!inode_tail_plugin(inode))
		return 1;
	return !inode_tail_plugin(inode)->have_tail(inode, new_size);

}

/* this is called by unix_file_read and unix_file_write->append_and_or_overwrite to decide which item plugin to perform action by */
static item_id
item_to_operate_on(struct inode *inode, flow_t * f, const coord_t * coord)
{
	reiser4_item_data data; /* this variable is necessary to use item_can_contain_key */
	loff_t new_size;

	assert("vs-1084", znode_is_loaded(coord->node));

	if (znode_get_level(coord->node) == TWIG_LEVEL) {

		/* extent item of this file found */
		data.iplug = item_plugin_by_id(EXTENT_POINTER_ID);
		assert("vs-919", ((f->op == WRITE_OP && item_can_contain_key(coord, &f->key, &data)) ||
				  (f->op == READ_OP && item_contains_key(coord, &f->key))));
		return EXTENT_POINTER_ID;
	}

	if (f->op == READ_OP) {
		assert("vs-1083", item_contains_key(coord, &f->key));
		return TAIL_ID;	
	}

	new_size = get_key_offset(&f->key) + f->length;
	data.iplug = item_plugin_by_id(TAIL_ID);
	if (coord_is_existing_item(coord) && item_can_contain_key(coord, &f->key, &data)) {
		/* tail item of this file found */
		if (should_have_notail(inode, new_size))
			/* this indicates need to convert file into extents */
			return LAST_ITEM_ID;
		return TAIL_ID;
	}

	/* there are no any items of this file yet */
	if (should_have_notail(inode, new_size))
		return EXTENT_POINTER_ID;
	return TAIL_ID;
}

reiser4_block_nr unix_file_estimate_read(struct inode *inode, loff_t count) {
    	/* We should reserve the one block, because of updating of the stat data
	   item */
	return inode_file_plugin(inode)->estimate.update(inode);
}
/* plugin->u.file.read */
ssize_t unix_file_read(struct file * file, char *buf, size_t read_amount, loff_t * off)
{
	int result;
	struct inode *inode;
	coord_t coord;
	lock_handle lh;
	item_id id;
	item_plugin *iplug;
	flow_t f;
	struct sealed_coord hint;

	reiser4_block_nr needed;

	if (unlikely(!read_amount))
		return 0;

	inode = file->f_dentry->d_inode;

	assert("vs-972", !inode_get_flag(inode, REISER4_NO_SD));

	get_nonexclusive_access(inode);
	needed = unix_file_estimate_read(inode, read_amount);
	result = reiser4_grab_space_exact(needed, 0);
	
	if (result != 0) {
	    drop_nonexclusive_access(inode);
	    return -ENOSPC;
	}
	
	trace_on(TRACE_RESERVE, "file read grabs %llu blocks.\n", needed);

	/* build flow */
	result = inode_file_plugin(inode)->flow_by_inode(inode, buf, 1 /* user space */ ,
							 read_amount, *off, READ_OP, &f);
	if (unlikely(result)) {
		drop_nonexclusive_access(inode);
		return result;
	}

	init_lh(&lh);

	/* get seal and coord sealed with it from reiser4 private data of
	   struct file.  The coord will tell us where our last read of this
	   file finished, and the seal will help us determine if that location
	   is still valid.
	*/
	result = load_file_hint(file, &hint);
	if (unlikely(result)) {
		drop_nonexclusive_access(inode);
		return result;
	}

	read_amount = f.length;
	while (f.length) {
		loff_t cur_offset;

		cur_offset = (loff_t) get_key_offset(&f.key);
		if (cur_offset >= inode->i_size)
			/* do not read out of file */
			break;

		page_cache_readahead(inode->i_mapping, &file->f_ra, file, cur_offset >> PAGE_CACHE_SHIFT);

		result = find_next_item(&hint, &f.key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
		if (result != CBK_COORD_FOUND) {
			/* item had to be found, as it was not - we have
			   -EIO */
			done_lh(&lh);
			break;
		}
		result = zload(coord.node);
		if (result) {
			done_lh(&lh);
			return result;
		}

		id = item_to_operate_on(inode, &f, &coord);
		zrelse(coord.node);
		
		assert("vs-1088", id == EXTENT_POINTER_ID || id == TAIL_ID);
		iplug = item_plugin_by_id(id);

		/* call read method of found item */
		result = iplug->s.file.read(inode, &coord, &f);
		if (result == -EAGAIN) {
			unset_hint(&hint);
			done_lh(&lh);
			continue;
		}
		if (result) {
			done_lh(&lh);
			break;
		}
		set_tail_state(inode, id);
		set_hint(&hint, &f.key, &coord);
		done_lh(&lh);
	}

	save_file_hint(file, &hint);

	if (read_amount - f.length) {
		/* something was read. Update stat data */
		UPDATE_ATIME(inode);
	}

	drop_nonexclusive_access(inode);

	/* update position in a file */
	*off += (read_amount - f.length);

	/* return number of read bytes or error code if nothing is read */
	return (read_amount - f.length) ? (read_amount - f.length) : result;
}

#ifdef NEW_READ_IS_READY

/* plugin->u.file.read */
ssize_t unix_file_read(struct file * file, char *buf, size_t read_amount, loff_t * off)
{
	int result;
	struct inode *inode;
	coord_t coord;
	lock_handle lh;
	size_t to_read;		/* do we really need both this and read_amount? */
	item_plugin *iplug;
	reiser4_plugin_id id;
	sink_t userspace_sink;

	inode = file->f_dentry->d_inode;
	result = 0;

	/* this should now be called userspace_sink_build, now that we have
	   both sinks and flows.  See discussion of sinks and flows in
	   www.namesys.com/v4/v4.html */
	result = userspace_sink_build(inode, buf, 1 /* user space */ , read_amount,
				      *off, READ_OP, &f);

	return result;

	get_nonexclusive_access(inode);

	/* have generic_readahead to return number of pages to
	   readahead. generic_readahead must not do readahead, but return
	   number of pages to readahead */
	logical_intrafile_readahead_amount = logical_generic_readahead(struct file * file, off, read_amount);

	while (intrafile_readahead_amount) {
		if ((loff_t) get_key_offset(&f.key) >= inode->i_size)
			/* do not read out of file */
			break;	/* coord will point to current item on entry and next item on exit */
		readahead_result = find_next_item(file, &f.key, &coord, &lh, ZNODE_READ_LOCK);
		if (readahead_result != CBK_COORD_FOUND)
			/* item had to be found, as it was not - we have
			   -EIO */
			break;

		/* call readahead method of found item */
		iplug = item_plugin_by_coord(&coord);
		if (!iplug->s.file.readahead) {
			readahead_result = -EINVAL;
			break;
		}

		readahead_result = iplug->s.file.readahead(inode, &coord, &lh, &intrafile_readahead_amount);
		if (readahead_result)
			break;
	}

	unix_file_interfile_readahead(struct file *file, off, read_amount, coord);

	to_read = f.length;
	while (f.length) {
		if ((loff_t) get_key_offset(&f.key) >= inode->i_size)
			/* do not read out of file */
			break;

		page_cache_readahead(file, (unsigned long) (get_key_offset(&f.key) >> PAGE_CACHE_SHIFT));

		/* coord will point to current item on entry and next item on exit */
		coord_init_zero(&coord);
		init_lh(&lh);

		/* FIXEM-VS: seal might be used here */
		result = find_next_item(0, &f.key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
		if (result != CBK_COORD_FOUND) {
			/* item had to be found, as it was not - we have
			   -EIO */
			done_lh(&lh);
			break;
		}

		result = zload(coord.node);
		if (result) {
			done_lh(&lh);
			break;
		}

		iplug = item_plugin_by_coord(&coord);
		id = item_plugin_id(iplug);
		if (id != EXTENT_POINTER_ID && id != TAIL_ID) {
			result = -EIO;
			zrelse(coord.node);
			done_lh(&lh);
			break;
		}

		/* for debugging sake make sure that tail status is set
		   correctly if it claimes to be known */
		if (REISER4_DEBUG && inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN)) {
			assert("vs-829", (id == TAIL_ID && inode_get_flag(inode, REISER4_HAS_TAIL))
			       || (id == EXTENT_POINTER_ID && !inode_get_flag(inode, REISER4_HAS_TAIL)));
		}

		/* get tail status if it is not known yet */
		if (!inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN)) {
			if (id == TAIL_ID)
				inode_set_flag(inode, REISER4_HAS_TAIL);
			else
				inode_clr_flag(inode, REISER4_HAS_TAIL);
		}

		/* call read method of found item */
		result = iplug->s.file.read(inode, &coord, &lh, &f);
		zrelse(coord.node);
		done_lh(&lh);
		if (result) {
			break;
		}
	}

	if (to_read - f.length) {
		/* something was read. Update stat data */
		UPDATE_ATIME(inode);
	}

	drop_nonexclusive_access(inode);

	/* update position in a file */
	*off += (to_read - f.length);
	/* return number of read bytes or error code if nothing is read */

/* VS-FIXME-HANS: readahead_result needs to be handled here */
	return (to_read - f.length) ? (to_read - f.length) : result;
}

unix_file_interfile_readahead(struct file * file, off, read_amount, coord)
{

	interfile_readahead_amount = unix_file_interfile_readahead_amount(struct file *file, off, read_amount);

	while (interfile_readahead_amount--) {
		right = get_right_neightbor(current);
		if (right is just after current) {
			coord_dup(right, current);
			zload(current);

		} else {
			break;
/* VS-FIXME-HANS: insert some coord releasing code here */
		}

	}

}

unix_file_interfile_readahead_amount(struct file *file, off, read_amount)
{
	/* current generic guess.  More sophisticated code can come later in v4.1+. */
	return 8;
}

#endif				/* NEW_READ_IS_READY */


/* This searches for write position in the tree and calls write method of
   appropriate item to actually copy user data into filesystem. This loops
   until all the data from flow @f are written to a file. */
static loff_t
append_and_or_overwrite(struct file *file, struct inode *inode, flow_t * f)
{
	int result;
	coord_t coord;
	lock_handle lh;
	size_t to_write;
	item_id id;
	item_plugin *iplug;
	struct sealed_coord hint;

	init_lh(&lh);

	/* get seal and coord sealed with it from reiser4 private data of
	   struct file */
	result = load_file_hint(file, &hint);
	if (result)
		return result;

	to_write = f->length;
	while (1) {
		/* look for file's metadata (extent or tail item) corresponding
		   to position we write to */
		result = find_next_item(&hint, &f->key, &coord, &lh, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT);
		if (result != CBK_COORD_FOUND && result != CBK_COORD_NOTFOUND) {
			/* error occurred */
			done_lh(&lh);
			return result;
		}

		result = zload(coord.node);
		if (result) {
			done_lh(&lh);
			return result;
		}

		id = item_to_operate_on(inode, f, &coord);
		zrelse(coord.node);

		assert("vs-1085", id == EXTENT_POINTER_ID || id == TAIL_ID || id == LAST_ITEM_ID);
		if (id == LAST_ITEM_ID) {
			done_lh(&lh);
			result = tail2extent(inode);
			if (result)
				return result;
			unset_hint(&hint);
			continue;
		}
		iplug = item_plugin_by_id(id);

		result = iplug->s.file.write(inode, &coord, &lh, f);
		if (result == -EAGAIN) {
			/*done_lh(&lh);*/
			unset_hint(&hint);
			continue;
		}
		if (result) {
			done_lh(&lh);
			break;			
		}
		set_tail_state(inode, id);
		set_hint(&hint, &f->key, &coord);
		done_lh(&lh);

		if (!to_write) {
			/* expanding truncate */
			if ((loff_t) get_key_offset(&f->key) < inode->i_size)
				continue;
		}

		if (f->length == 0)
			/* write is done */
			break;
	}
	update_sd_if_necessary(inode, f);
		
	save_file_hint(file, &hint);

	/* if nothing were written - there must be an error */
	assert("vs-951", ergo((to_write == f->length), result < 0));

	return (to_write - f->length) ? (to_write - f->length) : result;
}

int update_sd_if_necessary(struct inode *inode, const flow_t *f)
{
	int result;
	int update_inode;

	update_inode = 0;
	result = 0;
	if ((loff_t) get_key_offset(&f->key) > inode->i_size) {
		/* file got longer, stat data has to be updated */
		inode->i_size = get_key_offset(&f->key);
		update_inode = 1;
	}
	
	if (inode->i_ctime != CURRENT_TIME || inode->i_mtime != CURRENT_TIME) {
		/* time stamps are to be updated */
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		update_inode = 1;
	}
	
	if (update_inode) {
		assert("vs-946", !inode_get_flag(inode, REISER4_NO_SD));
		result = reiser4_write_sd(inode);
		if (result)
			warning("vs-636", "updating stat data failed: %i", result);
	}
	return result;
}

reiser4_block_nr unix_file_estimate_write(struct inode *inode, loff_t count,
    loff_t *off) 
{
    tail_plugin *tail_plugin;
    
    assert("umka-1242", inode != NULL);
    assert("umka-1248", inode != NULL);
    
    tail_plugin = inode_tail_plugin(inode);
    assert("umka-1241", tail_plugin != NULL);
   
    return tail_plugin->estimate(inode, count, 0/*is_fake*/) + 1;
}

/* plugin->u.file.write */
ssize_t unix_file_write(struct file * file,	/* file to write to */
			const char *buf,	/* comments are needed */
			size_t count,	/* number of bytes to write */
			loff_t * off /* position to write which */ )
{
	int result;
	struct inode *inode;
	flow_t f;
	ssize_t written;
	loff_t pos;
	reiser4_block_nr needed = 0;


	inode = file->f_dentry->d_inode;

	assert("vs-855", count > 0);
	assert("vs-947", !inode_get_flag(inode, REISER4_NO_SD));

	get_nonexclusive_access(inode);
	needed = unix_file_estimate_write(inode, count, off);
	
	/* FIXME-VITALY: grab needed blocks. They will go either 
	    to fake allocated or to overwrite set. */
	result = reiser4_grab_space_exact(needed, 0);

	if (result != 0) {
		drop_nonexclusive_access(inode);
		return -ENOSPC;
	}
	
	trace_on(TRACE_RESERVE, "file write grabs %llu blocks "
		"for %lu bytes long data.\n", needed, (unsigned long)count);
	
	pos = *off;
	if (file->f_flags & O_APPEND)
		pos = inode->i_size;

	if (inode->i_size < *off) {
		/* append file with a hole. This allows extent_write and
		   tail_write to not decide when hole appending is
		   necessary. When it is required f->length == 0 */
		result = expand_file(inode, inode->i_size, *off);
		if (result) {
			/* FIXME-VS: i_size may now be set incorrectly */
			drop_nonexclusive_access(inode);
			return result;
		}
	}

	/* build flow */
	result = inode_file_plugin(inode)->flow_by_inode(inode, (char *) buf, 1 /* user space */ ,
							 count, pos, WRITE_OP, &f);
	if (result)
		return result;

	written = append_and_or_overwrite(file, inode, &f);
	if (written < 0) {
		drop_nonexclusive_access(inode);
		return written;
	}

	if (written) {
		/* if we were not able to update stat data warning will be issued, but write will not return failure
		   notification */
		update_sd_if_necessary(inode, &f);
	}

	drop_nonexclusive_access(inode);

	/* update position in a file */
	*off = pos + written;
	/* return number of written bytes */
	return written;
}

/* Performs estimate of reserved blocks for file release operation. Note,
   that all estimate functions do not count the amount of blocks should
   be reserved for internal blocks */
reiser4_block_nr unix_file_estimate_release(struct inode *inode) {
    __u64 file_size;
    tail_plugin *tail_plugin;
    
    assert("umka-1237", inode != NULL);
    
    file_size = inode->i_size;

    if (file_size == 0) {
	/* If new file size is zero, we are reserving only some amount of blocks 
	   for the stat data updating */
	return inode_file_plugin(inode)->estimate.update(inode);
    }
    
    /* Here should be called tail policy plugin in order to handle correctly
       the situation of converting extent to tail */
    tail_plugin = inode_tail_plugin(inode);
    assert("umka-1238", tail_plugin != NULL);
    
    {
	reiser4_block_nr amount;
	estimate_internal_amount(1, tree_by_inode(inode)->height, &amount);
    
	return (tail_plugin->have_tail(inode, file_size) ? 
		tail_plugin->estimate(inode, file_size, 0) : amount + 1) + 
		inode_file_plugin(inode)->estimate.update(inode) + 1;
    }
}

/* plugin->u.file.release
   convert all extent items into tail items if necessary */
int
unix_file_release(struct file *file)
{
	int result;
	struct inode *inode;
	reiser4_block_nr needed;

	inode = file->f_dentry->d_inode;

	if (inode->i_size == 0)
		return 0;

	needed = unix_file_estimate_release(inode);
	result = reiser4_grab_space_exact(needed, 1);
	
	if (result != 0) return -ENOSPC;
	
	trace_on(TRACE_RESERVE, "file release grabs %llu blocks.\n", needed);

	/* FIXME-VS: it is not clear where to do extent2tail conversion yet */
	if (!inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN))
		/* there were no accesses to file body. Leave it as it is */
		return 0;

	if (should_have_notail(inode, inode->i_size)) {
		if (inode_get_flag(inode, REISER4_HAS_TAIL))
			info("file_release: " "file is built of tails instead of extents\n");
		return 0;
	}
	if (inode_get_flag(inode, REISER4_HAS_TAIL))
		/* file is already built of tails */
		return 0;
	return extent2tail(file);
}

struct page *
unix_file_filemap_nopage(struct vm_area_struct *area, unsigned long address, int unused)
{
	struct page *page;
	struct inode *inode;

	inode = area->vm_file->f_dentry->d_inode;
	get_nonexclusive_access(inode);
	page = filemap_nopage(area, address, unused);
	drop_nonexclusive_access(inode);
	return page;
}

static struct vm_operations_struct unix_file_vm_ops = {
	.nopage = unix_file_filemap_nopage,
};

reiser4_block_nr unix_file_estimate_mmap(struct inode *inode, loff_t count) 
{
	tail_plugin *tail_plugin;
	assert("umka-1246", inode != NULL);
	
	tail_plugin = inode_tail_plugin(inode);
	assert("umka-1238", tail_plugin != NULL);
	
	return tail_plugin->estimate(inode, count, 0) + 
		inode_file_plugin(inode)->estimate.update(inode) + 1;
}

/* plugin->u.file.mmap
   make sure that file is built of extent blocks */
/* Audited by: green(2002.06.15) */
int
unix_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode;
	int result;
	reiser4_block_nr needed;

	inode = file->f_dentry->d_inode;

	/* FIXME-VITALY: Should get_nonexclusive_access be first? */
	needed = unix_file_estimate_mmap(inode, inode->i_size);
	result = reiser4_grab_space_exact(needed, 0);
	
	if (result != 0) return -ENOSPC;
	
	trace_on(TRACE_RESERVE, "file mmap grabs %llu blocks.\n", needed);

	/* tail2extent expects file to be nonexclusively locked */
	get_nonexclusive_access(inode);

	if (!inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN)) {
		loff_t file_size;

		result = find_file_size(inode, &file_size);
		if (result)
			return result;
	}
	assert("vs-1074", inode_get_flag(inode, REISER4_TAIL_STATE_KNOWN));
	if (!inode_get_flag(inode, REISER4_HAS_TAIL)) {
		/* file is built of extents already */
		drop_nonexclusive_access(inode);
		return generic_file_mmap(file, vma);
	}

	result = tail2extent(inode);
	drop_nonexclusive_access(inode);
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
unix_file_get_block(struct inode *inode,
		    sector_t block, struct buffer_head *bh_result, int create UNUSED_ARG)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;
	     
	assert("vs-1091", create == 0);
	unix_file_key_by_inode(inode, (loff_t) block * current_blocksize, &key);

	coord_init_zero(&coord);
	init_lh(&lh);
	result = find_next_item(0, &key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE);
	if (result != CBK_COORD_FOUND || coord.between != AT_UNIT) {
		done_lh(&lh);
		return result;
	}
	result = zload(coord.node);
	if (result) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(&coord);
	if (iplug->s.file.get_block)
		result = iplug->s.file.get_block(&coord, block, bh_result);
	else
		result = -EINVAL;

	zrelse(coord.node);
	done_lh(&lh);
	return result;
}

/* plugin->u.file.flow_by_inode  = common_build_flow */

/* plugin->u.file.key_by_inode */
/* Audited by: green(2002.06.15) */
int
unix_file_key_by_inode(struct inode *inode, loff_t off, reiser4_key *key)
{
	build_sd_key(inode, key);
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) off);
	return 0;
}

/* plugin->u.file.set_plug_in_sd = NULL
   plugin->u.file.set_plug_in_inode = NULL
   plugin->u.file.create_blank_sd = NULL */

/* plugin->u.file.delete = NULL
   plugin->u.file.add_link = NULL
   plugin->u.file.rem_link = NULL */

/* plugin->u.file.owns_item 
   this is common_file_owns_item with assertion */
/* Audited by: green(2002.06.15) */
int
unix_file_owns_item(const struct inode *inode	/* object to check
						 * against */ ,
		    const coord_t * coord /* coord to check */ )
{
	int result;

	result = common_file_owns_item(inode, coord);
	if (!result)
		return 0;
	if (item_type_by_coord(coord) != ORDINARY_FILE_METADATA_TYPE)
		return 0;
	assert("vs-547", (item_id_by_coord(coord) == EXTENT_POINTER_ID || item_id_by_coord(coord) == TAIL_ID));
	return 1;
}

/* plugin->u.file.setattr method */
/* This calls inode_setattr and if truncate is in effect it also takes
   exclusive inode access to avoid races */
int
unix_file_setattr(struct inode *inode,	/* Object to change attributes */
		  struct iattr *attr /* change description */ )
{
	int truncate = attr->ia_valid & ATTR_SIZE;
	int retval;

	if (truncate)
		get_exclusive_access(inode);

	retval = inode_setattr(inode, attr);

	if (truncate)
		drop_exclusive_access(inode);

	return retval;
}

/* plugin->u.file.can_add_link = common_file_can_add_link */

/* Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
