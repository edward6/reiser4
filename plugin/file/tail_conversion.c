/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "../../inode.h"
#include "../../super.h"
#include "../../page_cache.h"
#include "../../carry.h"
#include "../../lib.h"
#include "../../safe_link.h"
#include "funcs.h"

/* this file contains:
   tail2extent and extent2tail */


/* exclusive access to a file is acquired when file state changes: tail2extent, empty2tail, extent2tail, etc */
reiser4_internal void
get_exclusive_access(unix_file_info_t *uf_info)
{
	assert("nikita-3028", schedulable());
	assert("nikita-3047", LOCK_CNT_NIL(inode_sem_w));
	assert("nikita-3048", LOCK_CNT_NIL(inode_sem_r));
	assert("nikita-3361", get_current_context()->trans->atom == NULL);
	BUG_ON(get_current_context()->trans->atom != NULL);
	LOCK_CNT_INC(inode_sem_w);
	rw_latch_down_write(&uf_info->latch);
	assert("nikita-3060", inode_ea_owner(uf_info) == NULL);
	assert("vs-1157", !ea_obtained(uf_info));
	ea_set(uf_info, current);
	uf_info->exclusive_use = 1;
}

reiser4_internal void
drop_exclusive_access(unix_file_info_t *uf_info)
{
	assert("nikita-3060", inode_ea_owner(uf_info) == current);
	assert("vs-1158", ea_obtained(uf_info));
	ea_set(uf_info, 0);
	uf_info->exclusive_use = 0;
	rw_latch_up_write(&uf_info->latch);
	assert("nikita-3049", LOCK_CNT_NIL(inode_sem_r));
	assert("nikita-3049", LOCK_CNT_GTZ(inode_sem_w));
	LOCK_CNT_DEC(inode_sem_w);
}

/* nonexclusive access to a file is acquired for read, write, readpage */
reiser4_internal void
get_nonexclusive_access(unix_file_info_t *uf_info)
{
	assert("nikita-3029", schedulable());
	rw_latch_down_read(&uf_info->latch);
	LOCK_CNT_INC(inode_sem_r);
	assert("nikita-3060", inode_ea_owner(uf_info) == NULL);
	assert("vs-1159", !ea_obtained(uf_info));
}

reiser4_internal void
drop_nonexclusive_access(unix_file_info_t *uf_info)
{
	assert("nikita-3060", inode_ea_owner(uf_info) == NULL);
	assert("vs-1160", !ea_obtained(uf_info));
	rw_latch_up_read(&uf_info->latch);
	LOCK_CNT_DEC(inode_sem_r);
}

/* part of tail2extent. Cut all items covering @count bytes starting from
   @offset */
/* Audited by: green(2002.06.15) */
static int
cut_formatting_items(struct inode *inode, loff_t offset, int count)
{
	reiser4_key from, to;

	/* AUDIT: How about putting an assertion here, what would check
	   all provided range is covered by tail items only? */
	/* key of first byte in the range to be cut  */
	key_by_inode_unix_file(inode, offset, &from);

	/* key of last byte in that range */
	to = from;
	set_key_offset(&to, (__u64) (offset + count - 1));

	/* cut everything between those keys */
	return cut_tree(tree_by_inode(inode), &from, &to, inode);
}

static void
release_all_pages(struct page **pages, unsigned nr_pages)
{
	unsigned i;

	for (i = 0; i < nr_pages; i++) {
		if (pages[i] == NULL) {
			unsigned j;
			for (j = i + 1; j < nr_pages; j ++)
				assert("vs-1620", pages[j] == NULL);
			break;
		}
		page_cache_release(pages[i]);
		pages[i] = NULL;
	}
}

/* part of tail2extent. replace tail items with extent one. Content of tail
   items (@count bytes) being cut are copied already into
   pages. extent_writepage method is called to create extents corresponding to
   those pages */
static int
replace(struct inode *inode, struct page **pages, unsigned nr_pages, int count)
{
	int result;
	unsigned i;
	STORE_COUNTERS;

	assert("vs-596", nr_pages > 0 && pages[0]);

	/* cut copied items */
	result = cut_formatting_items(inode, (loff_t) pages[0]->index << PAGE_CACHE_SHIFT, count);
	if (result)
		return result;

	CHECK_COUNTERS;

	/* put into tree replacement for just removed items: extent item, namely */
	for (i = 0; i < nr_pages; i++) {
		result = add_to_page_cache_lru(pages[i], inode->i_mapping,
					       pages[i]->index, mapping_gfp_mask(inode->i_mapping));
		if (result)
			break;
		unlock_page(pages[i]);
		result = find_or_create_extent(pages[i]);
		if (result)
			break;
		SetPageUptodate(pages[i]);
	}
	return result;
}

#define TAIL2EXTENT_PAGE_NUM 3	/* number of pages to fill before cutting tail
				 * items */

static int
reserve_tail2extent_iteration(struct inode *inode)
{
	reiser4_block_nr unformatted_nodes;
	reiser4_tree *tree;

	tree = tree_by_inode(inode);

	/* number of unformatted nodes which will be created */
	unformatted_nodes = TAIL2EXTENT_PAGE_NUM;

	/*
	 * space required for one iteration of extent->tail conversion:
	 *
	 *     1. kill N tail items
	 *
	 *     2. insert TAIL2EXTENT_PAGE_NUM unformatted nodes
	 *
	 *     3. insert TAIL2EXTENT_PAGE_NUM (worst-case single-block
	 *     extents) extent units.
	 *
	 *     4. drilling to the leaf level by coord_by_key()
	 *
	 *     5. possible update of stat-data
	 *
	 *     6. removal of safe-link
	 *
	 */
	grab_space_enable();
	return reiser4_grab_space
		(2 * tree->height +
		 TAIL2EXTENT_PAGE_NUM +
		 TAIL2EXTENT_PAGE_NUM * estimate_one_insert_into_item(tree) +
		 1 + estimate_one_insert_item(tree) +
		 inode_file_plugin(inode)->estimate.update(inode) +
		 safe_link_tograb(tree),
		 BA_CAN_COMMIT);
}

static int
find_start(struct inode *object, reiser4_plugin_id id, __u64 *offset)
{
	int               result;
	lock_handle       lh;
	unix_file_info_t *ufo;
	int               found;
	reiser4_key       key;

	ufo = unix_file_inode_data(object);
	init_lh(&lh);
	result = 0;
	found = 0;
	key_by_inode_unix_file(object, *offset, &key);
	do {
		hint_t      hint;

		hint_init_zero(&hint, &lh);
		result = find_file_item(&hint, &key,
					ZNODE_READ_LOCK, CBK_UNIQUE, 0, ufo);

		if (result == CBK_COORD_FOUND) {
			coord_t    *coord;

			coord = &hint.coord.base_coord;
			if (coord->between == AT_UNIT) {
				coord_clear_iplug(coord);
				result = zload(coord->node);
				if (result == 0) {
					if (item_id_by_coord(coord) == id)
						found = 1;
					else
						item_plugin_by_coord(coord)->s.file.append_key(coord, &key);
					zrelse(coord->node);
				}
			} else
				result = RETERR(-ENOENT);
		}
	} while (result == 0 && !found);
	done_lh(&lh);
	*offset = get_key_offset(&key);
	return result;
}

reiser4_internal int
tail2extent(unix_file_info_t *uf_info)
{
	int result;
	int s_result;
	reiser4_key key;	/* key of next byte to be moved to page */
	ON_DEBUG(reiser4_key tmp;)
	char *p_data;		/* data of page */
	unsigned page_off = 0,	/* offset within the page where to copy data */
	 count;			/* number of bytes of item which can be
				 * copied to page */
	struct page *pages[TAIL2EXTENT_PAGE_NUM];
	int done;		/* set to 1 when all file is read */
	char *item;
	int i;
	struct inode *inode;
	__u64 offset;
	int first_iteration;

	assert("nikita-3362", ea_obtained(uf_info));

	inode = unix_file_info_to_inode(uf_info);

	assert("nikita-3412", !IS_RDONLY(inode));

	if (uf_info->container == UF_CONTAINER_EXTENTS) {
		warning("vs-1171",
			"file %llu is built of tails already. Should not happen",
			get_inode_oid(inode));
		return 0;
	}

	/* collect statistics on the number of tail2extent conversions */
	reiser4_stat_inc(file.tail2extent);

	offset = 0;
	if (inode_get_flag(inode, REISER4_PART_CONV)) {
		/* find_start() doesn't need block reservation */
		result = find_start(inode, FORMATTING_ID, &offset);
		if (result == -ENOENT)
			/* no extent found, everything is converted */
			return 0;
		else if (result != 0)
			/* some other error */
			return result;
	}

	result = safe_link_grab(tree_by_inode(inode), BA_CAN_COMMIT);
	if (result == 0) {
		result = safe_link_add(inode, SAFE_T2E);
		if (result != 0)
			goto out;
	} else if (result != -EEXIST)
		goto out;

	/* get key of first byte of a file */
	key_by_inode_unix_file(inode, offset, &key);

	done = 0;
	result = 0;
	first_iteration = 1;
	while (!done) {
		xmemset(pages, 0, sizeof (pages));
		all_grabbed2free();
		result = reserve_tail2extent_iteration(inode);
		if (result != 0)
			goto out;
		if (first_iteration) {
			inode_set_flag(inode, REISER4_PART_CONV);
			reiser4_update_sd(inode);
			first_iteration = 0;
		}
		for (i = 0; i < sizeof_array(pages) && !done; i++) {
			assert("vs-598", (get_key_offset(&key) & ~PAGE_CACHE_MASK) == 0);
			pages[i] = alloc_page(mapping_gfp_mask(inode->i_mapping));
			if (!pages[i]) {
				result = RETERR(-ENOMEM);
				goto error;
			}

			pages[i]->index = (unsigned long) (get_key_offset(&key) >> PAGE_CACHE_SHIFT);
			/* usually when one is going to longterm lock znode (as
			   find_file_item does, for instance) he must not hold
			   locked pages. However, there is an exception for
			   case tail2extent. Pages appearing here are not
			   reachable to everyone else, they are clean, they do
			   not have jnodes attached so keeping them locked do
			   not risk deadlock appearance
			*/
			assert("vs-983", !PagePrivate(pages[i]));

			for (page_off = 0; page_off < PAGE_CACHE_SIZE;) {
				hint_t hint;
				coord_t *coord;
				lock_handle lh;

				/* get next item */
				hint_init_zero(&hint, &lh);
				result = find_file_item(&hint, &key, ZNODE_READ_LOCK, CBK_UNIQUE, 0/* ra_info */, uf_info);
				if (result != CBK_COORD_FOUND) {
					/* tail conversion can not be called for empty file */
					assert("vs-1169", result != CBK_COORD_NOTFOUND);
					done_lh(&lh);
					goto error;
				}
				coord = &hint.coord.base_coord;
				if (coord->between == AFTER_UNIT) {
					/* this is used to detect end of file when inode->i_size can not be used */
					done_lh(&lh);
					done = 1;
					p_data = kmap_atomic(pages[i], KM_USER0);
					xmemset(p_data + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic(p_data, KM_USER0);
					break;
				}
				coord_clear_iplug(coord);
				result = zload(coord->node);
				if (result) {
					done_lh(&lh);
					goto error;
				}
				assert("vs-562", owns_item_unix_file(inode, coord));
				assert("vs-856", coord->between == AT_UNIT);
				assert("green-11", keyeq(&key, unit_key_by_coord(coord, &tmp)));
				item = ((char *)item_body_by_coord(coord)) + coord->unit_pos;

				/* how many bytes to copy */
				count = item_length_by_coord(coord) - coord->unit_pos;
				/* limit length of copy to end of page */
				if (count > PAGE_CACHE_SIZE - page_off)
					count = PAGE_CACHE_SIZE - page_off;

				/* kmap/kunmap are necessary for pages which
				   are not addressable by direct kernel virtual
				   addresses */
				p_data = kmap_atomic(pages[i], KM_USER0);
				/* copy item (as much as will fit starting from
				   the beginning of the item) into the page */
				memcpy(p_data + page_off, item, (unsigned) count);
				kunmap_atomic(p_data, KM_USER0);

				page_off += count;
				set_key_offset(&key, get_key_offset(&key) + count);

				zrelse(coord->node);
				done_lh(&lh);

				if (get_key_offset(&key) == (__u64)unix_file_info_to_inode(uf_info)->i_size) {
					/* end of file is detected here */
					p_data = kmap_atomic(pages[i], KM_USER0);
					memset(p_data + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic(p_data, KM_USER0);
					done = 1;
					break;
				}
			}	/* for */
		}		/* for */

		assert("vs-1619", i > 0);
		result = replace(inode, pages, i, (int) ((i - 1) * PAGE_CACHE_SIZE + page_off));
		release_all_pages(pages, sizeof_array(pages));
		if (result)
			goto error;
		/* throttle the conversion */
		balance_dirty_page_unix_file(inode);
	}

	if (result == 0) {
		/* tail converted */
		uf_info->container = UF_CONTAINER_EXTENTS;

		if (inode_get_flag(inode, REISER4_PART_CONV)) {
			inode_clr_flag(inode, REISER4_PART_CONV);
			reiser4_update_sd(inode);
		}
		/* It is advisable to check here that all grabbed pages were
		 * freed */
	} else {
		/* conversion is not complete. Inode was already marked as
		 * REISER4_PART_CONV and stat-data were updated at the first
		 * iteration of the loop above. */
 error:
		release_all_pages(pages, sizeof_array(pages));
		warning("nikita-2282", "Partial conversion of %llu: %i",
			get_inode_oid(inode), result);
		print_inode("inode", inode);
	}

	s_result = safe_link_del(inode, SAFE_T2E);
	if (s_result != 0)
		warning("nikita-3425", "Cannot kill safe-link %lli: %i",
			get_inode_oid(inode), s_result);

 out:
	safe_link_release(tree_by_inode(inode));
	all_grabbed2free();
	return result;
}


/* part of extent2tail. Page contains data which are to be put into tree by
   tail items. Use tail_write for this. flow is composed like in
   unix_file_write. The only difference is that data for writing are in
   kernel space */
/* Audited by: green(2002.06.15) */
static int
write_page_by_tail(struct inode *inode, struct page *page, unsigned count)
{
	flow_t f;
	hint_t hint;
	coord_t *coord;
	lock_handle lh;
	znode *loaded;
	item_plugin *iplug;
	int result;

	result = 0;

	assert("vs-1089", count);

	/* build flow */
	inode_file_plugin(inode)->flow_by_inode(inode, kmap(page), 0 /* not user space */ ,
						count, (loff_t) (page->index << PAGE_CACHE_SHIFT), WRITE_OP, &f);
	iplug = item_plugin_by_id(FORMATTING_ID);
	while (f.length) {
		hint_init_zero(&hint, &lh);
		result = find_file_item(&hint, &f.key, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT, 0/* ra_info */, 0/* inode */);
		if (IS_CBKERR(result))
			break;

		assert("vs-957", ergo(result == CBK_COORD_NOTFOUND, get_key_offset(&f.key) == 0));
		assert("vs-958", ergo(result == CBK_COORD_FOUND, get_key_offset(&f.key) != 0));

		coord = &hint.coord.base_coord;
		coord_clear_iplug(coord);
		result = zload(coord->node);
		if (result)
			break;

		loaded = coord->node;
		result = iplug->s.file.write(inode, &f, &hint, 1/*grabbed*/, how_to_write(&hint.coord, &f.key));
		zrelse(loaded);
		done_lh(&lh);
		if (result == -E_REPEAT)
			result = 0;
		else if (result)
			break;
	}

	done_lh(&lh);
	kunmap(page);

	/* result of write is 0 or error */
	assert("vs-589", result <= 0);
	/* if result is 0 - all @count bytes is written completely */
	assert("vs-588", ergo(result == 0, f.length == 0));
	return result;
}

/* flow insertion is limited by CARRY_FLOW_NEW_NODES_LIMIT of new nodes. Therefore, minimal number of bytes of flow
   which can be put into tree by one insert_flow is number of bytes contained in CARRY_FLOW_NEW_NODES_LIMIT nodes if
   they all are filled completely by one tail item. Fortunately, there is a one to one mapping between bytes of tail
   items and bytes of flow. If there were not, we would have to have special item plugin */
reiser4_internal int min_bytes_per_flow(void)
{
	assert("vs-1103", current_tree->nplug && current_tree->nplug->max_item_size);
	return CARRY_FLOW_NEW_NODES_LIMIT * current_tree->nplug->max_item_size();
}

static int
reserve_extent2tail_iteration(struct inode *inode)
{
	reiser4_tree *tree;

	tree = tree_by_inode(inode);
	/*
	 * reserve blocks for (in this order):
	 *
	 *     1. removal of extent item
	 *
	 *     2. insertion of tail by insert_flow()
	 *
	 *     3. drilling to the leaf level by coord_by_key()
	 *
	 *     4. possible update of stat-data
	 *
	 *     5. removal of safe-link
	 */
	grab_space_enable();
	return reiser4_grab_space
		(estimate_one_item_removal(tree) +
		 estimate_insert_flow(tree->height) +
		 1 + estimate_one_insert_item(tree) +
		 inode_file_plugin(inode)->estimate.update(inode) +
		 safe_link_tograb(tree),
		 BA_CAN_COMMIT);
}

/* for every page of file: read page, cut part of extent pointing to this page,
   put data of page tree by tail item */
reiser4_internal int
extent2tail(unix_file_info_t *uf_info)
{
	int result;
	int s_result;
	struct inode *inode;
	struct page *page;
	unsigned long num_pages, i;
	unsigned long start_page;
	reiser4_key from;
	reiser4_key to;
	unsigned count;
	__u64 offset;
	int space_reserved;

	/* collect statistics on the number of extent2tail conversions */
	reiser4_stat_inc(file.extent2tail);

	inode = unix_file_info_to_inode(uf_info);
	assert("nikita-3412", !IS_RDONLY(inode));

	offset = 0;
	if (inode_get_flag(inode, REISER4_PART_CONV)) {
		/* find_start() doesn't need block reservation */
		result = find_start(inode, EXTENT_POINTER_ID, &offset);
		if (result == -ENOENT)
			/* no extent found, everything is converted */
			return 0;
		else if (result != 0)
			/* some other error */
			return result;
	}

	result = safe_link_grab(tree_by_inode(inode), BA_CAN_COMMIT);
	if (result == 0) {
		result = safe_link_add(inode, SAFE_E2T);
		if (result != 0)
			return result;
	} else if (result != -EEXIST)
		return result;

	/* number of pages in the file */
	num_pages =
		(inode->i_size - offset + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	start_page = offset >> PAGE_CACHE_SHIFT;

	key_by_inode_unix_file(inode, offset, &from);
	to = from;

	result = 0;
	space_reserved = 0;
	for (i = 0; i < num_pages; i++) {
		__u64 start_byte;

		all_grabbed2free();
		space_reserved = 0;
		result = reserve_extent2tail_iteration(inode);
		if (result != 0)
			break;
		space_reserved = 1;
		if (i == 0) {
			inode_set_flag(inode, REISER4_PART_CONV);
			reiser4_update_sd(inode);
		}

		page = read_cache_page(inode->i_mapping,
				       (unsigned) (i + start_page),
				       readpage_unix_file/*filler*/, 0);
		if (IS_ERR(page)) {
			result = PTR_ERR(page);
			break;
		}

		wait_on_page_locked(page);

		if (!PageUptodate(page)) {
			page_cache_release(page);
			result = RETERR(-EIO);
			break;
		}

		/* cut part of file we have read */
		start_byte = (__u64) (i << PAGE_CACHE_SHIFT) + offset;
		set_key_offset(&from, start_byte);
		set_key_offset(&to, start_byte + PAGE_CACHE_SIZE - 1);
		/*
		 * cut_tree_object() returns -E_REPEAT to allow atom
		 * commits during over-long truncates. But
		 * extent->tail conversion should be performed in one
		 * transaction.
		 */
		result = cut_tree(tree_by_inode(inode), &from, &to, inode);

		if (result) {
			page_cache_release(page);
			break;
		}

		/* put page data into tree via tail_write */
		count = PAGE_CACHE_SIZE;
		if (i == num_pages - 1)
			count = (inode->i_size & ~PAGE_CACHE_MASK) ? : PAGE_CACHE_SIZE;
		result = write_page_by_tail(inode, page, count);
		if (result) {
			page_cache_release(page);
			break;
		}

		/* release page */
		lock_page(page);
		/* page is already detached from jnode and mapping. */
		assert("vs-1086", page->mapping == NULL);
		assert("nikita-2690", (!PagePrivate(page) && page->private == 0));
		/* waiting for writeback completion with page lock held is
		 * perfectly valid. */
		wait_on_page_writeback(page);
		drop_page(page);
		/* release reference taken by read_cache_page() above */
		page_cache_release(page);
	}

	assert("vs-1260", reiser4_inode_data(inode)->eflushed == 0);

	if (i == num_pages) {
		uf_info->container = UF_CONTAINER_TAILS;
		assert("nikita-3471", space_reserved);
		if (inode_get_flag(inode, REISER4_PART_CONV)) {
			inode_clr_flag(inode, REISER4_PART_CONV);
			reiser4_update_sd(inode);
		}
	} else {
		/* conversion is not complete. Inode was already marked as
		 * REISER4_PART_CONV and stat-data were updated at the first
		 * iteration of the loop above. */
		warning("nikita-2282",
			"Partial conversion of %llu: %lu of %lu: %i",
			get_inode_oid(inode), i, num_pages, result);
		print_inode("inode", inode);
	}
	if (space_reserved) {
		s_result = safe_link_del(inode, SAFE_E2T);
		if (s_result != 0) {
			warning("nikita-3422", "Cannot kill safe-link %lli: %i",
				get_inode_oid(inode), s_result);
		}
	} else
		s_result = 0;
	all_grabbed2free();
	return result ? : s_result;
}

reiser4_internal int
finish_conversion(struct inode *inode)
{
	int result;

	if (inode_get_flag(inode, REISER4_PART_CONV))
		result = tail2extent(unix_file_inode_data(inode));
	else
		result = 0;
	return result;
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
