/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../../inode.h"
#include "../../super.h"
#include "../../page_cache.h"
#include "../../carry.h"
#include "../../lib.h"

/* this file contains:
   tail2extent and extent2tail */

int find_file_item(struct sealed_coord *, const reiser4_key *, coord_t *, lock_handle *,
		   znode_lock_mode, __u32 cbk_flags, ra_info_t *, struct inode *);
int goto_right_neighbor(coord_t *, lock_handle *);
void set_file_state_extents(struct inode *);
void set_file_state_tails(struct inode *);
int unix_file_writepage_nolock(struct page *page);
int file_is_built_of_extents(const struct inode *inode);


#if REISER4_DEBUG
static inline struct task_struct *
inode_ea_owner(const struct inode *inode)
{
	return unix_file_inode_data(inode)->ea_owner;
}

static void ea_set(struct inode *inode, void *value)
{
	unix_file_inode_data(inode)->ea_owner = value;
}
#else
#define ea_set(inode, value) noop
#endif

static int ea_obtained(const struct inode *inode)
{
	
	assert("vs-1167", ergo (inode_ea_owner(inode) != NULL,
				inode_ea_owner(inode) == current));
	return inode_get_flag(inode, REISER4_EXCLUSIVE_USE);
}

/* exclusive access to a file is acquired when file state changes: tail2extent, empty2tail, extent2tail, etc */
void
get_exclusive_access(struct inode *inode)
{
	assert("nikita-3028", schedulable());
	if (REISER4_DEBUG && is_in_reiser4_context()) {
		assert("nikita-3047", lock_counters()->inode_sem_w == 0);
		assert("nikita-3048", lock_counters()->inode_sem_r == 0);
		lock_counters()->inode_sem_w ++;
	}
	rw_latch_down_write(&unix_file_inode_data(inode)->latch);
	assert("nikita-3060", inode_ea_owner(inode) == NULL);
	assert("vs-1157", !ea_obtained(inode));
	ea_set(inode, current);
	inode_set_flag(inode, REISER4_EXCLUSIVE_USE);
}

void
drop_exclusive_access(struct inode *inode)
{
	assert("nikita-3060", inode_ea_owner(inode) == current);
	assert("vs-1158", ea_obtained(inode));
	ea_set(inode, 0);
	inode_clr_flag(inode, REISER4_EXCLUSIVE_USE);
	rw_latch_up_write(&unix_file_inode_data(inode)->latch);
	if (REISER4_DEBUG && is_in_reiser4_context()) {
		assert("nikita-3049", lock_counters()->inode_sem_r == 0);
		assert("nikita-3049", lock_counters()->inode_sem_w > 0);
		lock_counters()->inode_sem_w --;
	}
}

/* nonexclusive access to a file is acquired for read, write, readpage */
void
get_nonexclusive_access(struct inode *inode)
{
	assert("nikita-3029", schedulable());
	rw_latch_down_read(&unix_file_inode_data(inode)->latch);
	if (REISER4_DEBUG && is_in_reiser4_context()) {
		assert("nikita-3050", lock_counters()->inode_sem_w == 0);
		assert("nikita-3051", lock_counters()->inode_sem_r == 0);
		lock_counters()->inode_sem_r ++;
	}
	assert("nikita-3060", inode_ea_owner(inode) == NULL);
	assert("vs-1159", !ea_obtained(inode));
}

void
drop_nonexclusive_access(struct inode *inode)
{
	assert("nikita-3060", inode_ea_owner(inode) == NULL);
	assert("vs-1160", !ea_obtained(inode));
	rw_latch_up_read(&unix_file_inode_data(inode)->latch);
	if (REISER4_DEBUG && is_in_reiser4_context()) {
		assert("nikita-3049", lock_counters()->inode_sem_w == 0);
		assert("nikita-3049", lock_counters()->inode_sem_r > 0);
		lock_counters()->inode_sem_r --;
	}
}

static void
nea2ea(struct inode *inode)
{
	drop_nonexclusive_access(inode);
	get_exclusive_access(inode);
}

static void
ea2nea(struct inode *inode)
{
	assert("nikita-3060", inode_ea_owner(inode) == current);
	assert("vs-1168", ea_obtained(inode));
	ea_set(inode, 0);
	inode_clr_flag(inode, REISER4_EXCLUSIVE_USE);
	rw_latch_downgrade(&unix_file_inode_data(inode)->latch);
	ON_DEBUG_CONTEXT(lock_counters()->inode_sem_w --);
	ON_DEBUG_CONTEXT(lock_counters()->inode_sem_r ++);
}

static int
file_continues_in_right_neighbor(const struct inode *inode, znode *node)
{
	return UNDER_RW(dk, current_tree, read,
			get_inode_oid(inode) == get_key_objectid(znode_get_rd_key(node)));
}

/* the below two functions are helpers for prepare_tail2extent and prepare_extent2tail. They are to be used like:
   nodes = nodes_spanned();
   calculate and reserve space necessary for whole operation;
   mark_frozen();
*/

/* Lock the leftmost of nodes containing file items and calculate amount of nodes spanned by the file. Return keeping
   first node locked */
static int
nodes_spanned(struct inode *inode, reiser4_block_nr *blocks, coord_t *first_coord, lock_handle *first_lh)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;

	inode_file_plugin(inode)->key_by_inode(inode, 0, &key);

	coord_init_zero(first_coord);
	init_lh(first_lh);
	
	result = find_file_item(0, &key, first_coord, first_lh, ZNODE_WRITE_LOCK, CBK_UNIQUE, 0/* ra_info */, inode);
	if (result != CBK_COORD_FOUND) {
		/* error occured */
		done_lh(first_lh);
		return result;
	}

	coord_dup_nocheck(&coord, first_coord);
	init_lh(&lh);
	result = longterm_lock_znode(&lh, coord.node, ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI);
	if (unlikely(result)) {
		/* error occured */
		done_lh(first_lh);
		return result;
	}
	*blocks = 1;
	while (1) {
		if (!file_continues_in_right_neighbor(inode, coord.node))
			break;
		result = goto_right_neighbor(&coord, &lh);
		if (result) {
			done_lh(&lh);
			done_lh(first_lh);
			return result;
		}
		(*blocks) ++;
	}

	done_lh(&lh);
	return 0;
}

/* Scan all nodes spanned by the file and mark all items of file as frozen. */
static int
mark_frozen(const struct inode *inode, reiser4_block_nr spanned_blocks, coord_t *coord, lock_handle *lh)
{
	int result;
	coord_t twin;
	item_id id;
	reiser4_block_nr blocks;

	coord_dup_nocheck(&twin, coord);
	id =  znode_get_level(twin.node) == LEAF_LEVEL ? FROZEN_TAIL_ID : FROZEN_EXTENT_POINTER_ID;
	blocks = 1;

	result = 0;
	while (1) {
		result = WITH_DATA(twin.node, twin.node->nplug->set_item_plugin(&twin, id));
		if (result)
			break;

		znode_make_dirty(twin.node);
		assert("vs-1124", blocks <= spanned_blocks);
		if (!file_continues_in_right_neighbor(inode, twin.node))
			break;
		result = goto_right_neighbor(&twin, lh);
		if (result)
			break;
		blocks ++;
	}
	if (result)
		warning("vs-1126", "tail conversion not completed. File (ino=%llu) may be unreachable\n", get_inode_oid(inode));
	done_lh(lh);
	return result;
}

static int
prepare_tail2extent(struct inode *inode)
{
	int result;
	reiser4_block_nr formatted_nodes, unformatted_nodes;
	lock_handle first_lh;
	coord_t coord;
	tree_level height;

	height = tree_by_inode(inode)->height;

	/* number of leaf formatted nodes file spans */
	result = nodes_spanned(inode, &formatted_nodes, &coord, &first_lh);
	if (result)
		return result;
	/* number of unformatted nodes which will be created */
	unformatted_nodes = (inode->i_size + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;

	/* space necessary for tail2extent convertion: space for @nodes removals from tree, @unformatted_nodes blocks
	   for unformatted nodes, and space for @unformatted_nodes insertions into item (extent insertions) */
	/*
	 * FIXME-NIKITA if grab_space would try to commit current transaction
	 * at this point we are stymied, because long term lock is held in
	 * @first_lh. I removed BA_CAN_COMMIT from garbbing flags.
	 */
	result = reiser4_grab_space_force(formatted_nodes * estimate_one_item_removal(height) + unformatted_nodes +
					  unformatted_nodes * estimate_one_insert_into_item(height), 0, "tail2extent");
	if (result) {
		done_lh(&first_lh);
		return result;
	}
	return mark_frozen(inode, formatted_nodes, &coord, &first_lh);
}

/* part of tail2extent. Cut all items covering @count bytes starting from
   @offset */
/* Audited by: green(2002.06.15) */
static int
cut_tail_items(struct inode *inode, loff_t offset, int count)
{
	reiser4_key from, to;

	/* AUDIT: How about putting an assertion here, what would check
	   all provided range is covered by tail items only? */
	/* key of first byte in the range to be cut  */
	unix_file_key_by_inode(inode, offset, &from);

	/* key of last byte in that range */
	to = from;
	set_key_offset(&to, (__u64) (offset + count - 1));

	/* cut everything between those keys */
	return cut_tree(tree_by_inode(inode), &from, &to);
}

typedef enum {
	UNLOCK = 0,
	RELEASE = 1,
	DROP = 2
} page_action;

static void
for_all_pages(struct page **pages, unsigned nr_pages, page_action action)
{
	unsigned i;

	for (i = 0; i < nr_pages; i++) {
		if (!pages[i])
			continue;
		switch(action) {
		case UNLOCK:
			reiser4_unlock_page(pages[i]);
			break;
		case DROP:
			reiser4_unlock_page(pages[i]);
		case RELEASE:
			assert("vs-1082", !PageLocked(pages[i]));
			page_cache_release(pages[i]);
			pages[i] = NULL;
			break;
		}
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
	result = cut_tail_items(inode, (loff_t) pages[0]->index << PAGE_CACHE_SHIFT, count);
	if (result)
		return result;

	CHECK_COUNTERS;

	/* put into tree replacement for just removed items: extent item, namely */
	for (i = 0; i < nr_pages; i++) {
		result = unix_file_writepage_nolock(pages[i]);
		reiser4_unlock_page(pages[i]);
		if (result)
			break;
		SetPageUptodate(pages[i]);
	}
	return result;
}

#define TAIL2EXTENT_PAGE_NUM 3	/* number of pages to fill before cutting tail
				 * items */

/* this can be called with either exclusive (via truncate) or with non-exclusive (via write) access to file obtained */
int
tail2extent(struct inode *inode)
{
	int result;
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
	int access_switched;

	/* switch inode's rw_semaphore from read_down (set by unix_file_write)
	   to write_down */
	access_switched = 0;
	if (!ea_obtained(inode)) {
		/* we are called from write */
		access_switched = 1;
		nea2ea(inode);
	}

	if (file_is_built_of_extents(inode)) {
		warning("vs-1171", 
			"file %llu is built of tails already. Should not happen",
			get_inode_oid(inode));
		/* tail was converted by someone else */
		if (access_switched)
			ea2nea(inode);
		return 0;
	}

	result = prepare_tail2extent(inode);
	if (result) {
		if (access_switched)
			ea2nea(inode);
		return result;
	}

	/* collect statistics on the number of tail2extent conversions */
	reiser4_stat_inc(file.tail2extent);

	/* get key of first byte of a file */
	unix_file_key_by_inode(inode, 0ull, &key);

	done = 0;
	result = 0;
	while (!done) {
		xmemset(pages, 0, sizeof (pages));
		for (i = 0; i < sizeof_array(pages) && !done; i++) {
			assert("vs-598", (get_key_offset(&key) & ~PAGE_CACHE_MASK) == 0);
			pages[i] = grab_cache_page(inode->i_mapping, (unsigned long) (get_key_offset(&key)
										      >> PAGE_CACHE_SHIFT));
			if (!pages[i]) {
				result = -ENOMEM;
				goto error;
			}

			/* usually when one is going to longterm lock znode (as
			   find_next_item does, for instance) he must not hold
			   locked pages. However, there is an exception for
			   case tail2extent. Pages appearing here are not
			   reachable to everyone else, they are clean, they do
			   not have jnodes attached so keeping them locked do
			   not risk deadlock appearance
			*/
			assert("vs-983", !PagePrivate(pages[i]));

			for (page_off = 0; page_off < PAGE_CACHE_SIZE;) {
				coord_t coord;
				lock_handle lh;

				/* get next item */
				coord_init_zero(&coord);
				init_lh(&lh);
				result = find_file_item(0, &key, &coord, &lh, ZNODE_READ_LOCK, CBK_UNIQUE, 0/* ra_info */, inode);
				if (result != CBK_COORD_FOUND) {
					/* tail conversion can not be called for empty file */
					assert("vs-1169", result != CBK_COORD_NOTFOUND);
					done_lh(&lh);
					goto error;
				}
				if (coord.between == AFTER_UNIT) {
					/* this is used to detect end of file when inode->i_size can not be used */
					done_lh(&lh);
					done = 1;
					p_data = kmap_atomic(pages[i], KM_USER0);
					xmemset(p_data + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic(p_data, KM_USER0);
					break;
				}
				result = zload(coord.node);
				if (result) {
					done_lh(&lh);
					goto error;
				}
				assert("vs-562", unix_file_owns_item(inode, &coord));
				assert("vs-856", coord.between == AT_UNIT);
				assert("green-11", keyeq(&key, unit_key_by_coord(&coord, &tmp)));
				assert("vs-1170", item_id_by_coord(&coord) == FROZEN_TAIL_ID);
#if 0
				if (item_id_by_coord(&coord) != TAIL_ID && item_id_by_coord(&coord) != FROZEN_TAIL_ID) {
					/* something other than tail found. This is only possible when first item of a
					   file found during call to reiser4_mmap.
					*/
					result = -EIO;
					if (get_key_offset(&key) == 0 && item_id_by_coord(&coord) == EXTENT_POINTER_ID)
						result = 0;

					zrelse(coord.node);
					done_lh(&lh);
					goto error;
				}
#endif
				item = ((char *)item_body_by_coord(&coord)) + 
					coord.unit_pos;

				/* how many bytes to copy */
				count = item_length_by_coord(&coord) - coord.unit_pos;
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

				zrelse(coord.node);
				done_lh(&lh);

				if (get_key_offset(&key) == (__u64)inode->i_size) {
					/* end of file is detected here */
					p_data = kmap_atomic(pages[i], KM_USER0);
					memset(p_data + page_off, 0, PAGE_CACHE_SIZE - page_off);
					kunmap_atomic(p_data, KM_USER0);
					done = 1;
					break;
				}
			}	/* for */
		}		/* for */

		/* to keep right lock order unlock pages before calling replace which will have to obtain longterm
		   znode lock */
		for_all_pages(pages, sizeof_array(pages), UNLOCK);

		result = replace(inode, pages, i, (int) ((i - 1) * PAGE_CACHE_SIZE + page_off));
		for_all_pages(pages, sizeof_array(pages), RELEASE);
		if (result)
			goto exit;
	}
	/* tail converted */
	set_file_state_extents(inode);

	for_all_pages(pages, sizeof_array(pages), RELEASE);
	if (access_switched)
		ea2nea(inode);

	/* It is advisable to check here that all grabbed pages were freed */

	/* file could not be converted back to tails while we did not
	   have neither NEA nor EA to the file */
	assert("vs-830", file_is_built_of_extents(inode));
	assert("vs-1083", result == 0);
	all_grabbed2free("tail2extent");
	return 0;

error:
	for_all_pages(pages, sizeof_array(pages), DROP);
exit:
	if (access_switched)
		ea2nea(inode);
	all_grabbed2free("tail2exten failed");
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
	coord_t coord;
	lock_handle lh;
	znode *loaded;
	item_plugin *iplug;
	int result;

	result = 0;

	assert("vs-1089", count);

	coord_init_zero(&coord);
	init_lh(&lh);

	/* build flow */
	inode_file_plugin(inode)->flow_by_inode(inode, kmap(page), 0 /* not user space */ ,
						count, (loff_t) (page->index << PAGE_CACHE_SHIFT), WRITE_OP, &f);
	iplug = item_plugin_by_id(TAIL_ID);
	while (f.length) {
		result = find_file_item(0, &f.key, &coord, &lh, ZNODE_WRITE_LOCK, CBK_UNIQUE | CBK_FOR_INSERT, 0/* ra_info */, 0/* inode */);
		if (result != CBK_COORD_NOTFOUND && result != CBK_COORD_FOUND)
			break;

		assert("vs-957", ergo(result == CBK_COORD_NOTFOUND, get_key_offset(&f.key) == 0));
		assert("vs-958", ergo(result == CBK_COORD_FOUND, get_key_offset(&f.key) != 0));

		result = zload(coord.node);
		if (result)
			break;

		loaded = coord.node;
		result = iplug->s.file.write(inode, &coord, &lh, &f, 0, 1);
		zrelse(loaded);
		done_lh(&lh);
		if (result == -EAGAIN)
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
int min_bytes_per_flow(void)
{
	assert("vs-1103", current_tree->nplug && current_tree->nplug->max_item_size);
	return CARRY_FLOW_NEW_NODES_LIMIT * current_tree->nplug->max_item_size();
}

static int prepare_extent2tail(struct inode *inode)
{
	int result;
	reiser4_block_nr twig_nodes, flow_insertions;
	lock_handle first_lh;
	coord_t coord;
	tree_level height;

	height = tree_by_inode(inode)->height;

	/* number of twig nodes file spans */
	result = nodes_spanned(inode, &twig_nodes, &coord, &first_lh);
	if (result)
		return result;
	/* number of "flow insertions" which will be needed */
	flow_insertions = div64_32(inode->i_size + min_bytes_per_flow() - 1, min_bytes_per_flow(), NULL);

	/* space necessary for extent2tail convertion: space for @nodes removals from tree and space for calculated
	 * amount of flow insertions and 1 node and one insertion into tree for search_by_key(CBK_FOR_INSERT) */
	/*
	 * FIXME-NIKITA if grab_space would try to commit current transaction
	 * at this point we are stymied, because long term lock is held in
	 * @first_lh. I removed BA_CAN_COMMIT from garbbing flags.
	 */
	result = reiser4_grab_space(twig_nodes * estimate_one_item_removal(height) +
				    flow_insertions * estimate_insert_flow(height) +
				    1 + estimate_one_insert_item(height), 0, "extent2tail");
	if (result) {
		done_lh(&first_lh);
		return result;
	}
	return mark_frozen(inode, twig_nodes, &coord, &first_lh);
}

/* for every page of file: read page, cut part of extent pointing to this page,
   put data of page tree by tail item */
int
extent2tail(struct inode *inode)
{
	int result;
	struct page *page;
	unsigned long num_pages, i;
	reiser4_key from;
	reiser4_key to;
	unsigned count;

	/* collect statistics on the number of extent2tail conversions */
	reiser4_stat_inc(file.extent2tail);

	result = prepare_extent2tail(inode);
	if (result) {
		/* no space? Leave file stored in extent state */
		return 0;
	}

	/* number of pages in the file */
	num_pages = (inode->i_size + PAGE_CACHE_SIZE - 1) / PAGE_CACHE_SIZE;

	unix_file_key_by_inode(inode, 0ull, &from);
	to = from;

	result = 0;

	for (i = 0; i < num_pages; i++) {
		page = read_cache_page(inode->i_mapping, (unsigned) i, unix_file_readpage/*filler*/, 0);
		if (IS_ERR(page)) {
			result = PTR_ERR(page);
			break;
		}

		wait_on_page_locked(page);

		if (!PageUptodate(page)) {
			page_cache_release(page);
			result = -EIO;
			break;
		}

		/* detach jnode if any */
		reiser4_lock_page(page);
		assert("nikita-2689", page->mapping == inode->i_mapping);
		if (PagePrivate(page)) {
			/*
			 * it is possible that io is underway for this
			 * page. Wait for io completion. We don't want to
			 * detach jnode from in-flight page.
			 */
			wait_on_page_writeback(page);
			result = page->mapping->a_ops->invalidatepage(page, 0);
			if (result) {
				reiser4_unlock_page(page);
				page_cache_release(page);
				break;
			}
		}
		reiser4_unlock_page(page);

		/* cut part of file we have read */
		set_key_offset(&from, (__u64) (i << PAGE_CACHE_SHIFT));
		set_key_offset(&to, (__u64) ((i << PAGE_CACHE_SHIFT) + PAGE_CACHE_SIZE - 1));
		result = cut_tree(tree_by_inode(inode), &from, &to);
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
		reiser4_lock_page(page);
		assert("vs-1086", page->mapping == inode->i_mapping);
		assert("nikita-2690", (!PagePrivate(page) && page->private == 0));
		/* waiting for writeback completion with page lock held is
		 * perfectly valid. */
		wait_on_page_writeback(page);
		drop_page(page, NULL);
		/* release reference taken by read_cache_page() above */
		page_cache_release(page);
	}

	if (i == num_pages)
		/* FIXME-VS: not sure what to do when conversion did
		   not complete */
		set_file_state_tails(inode);
	else {
		warning("nikita-2282",
			"Partial conversion of %llu: %lu of %lu: %i",
			get_inode_oid(inode), i, num_pages, result);
		print_inode("inode", inode);
	}
	all_grabbed2free("extent2tail");
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
