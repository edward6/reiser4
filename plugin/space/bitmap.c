/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"
#include "bitmap.h"

/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. */
struct bnode {
	spinlock_t guard;
	char     * wpage; /* working bitmap block */
	char     * cpage; /* commit bitmap block */

	struct bnode * next_in_commit_list;
};

/* Audited by: green(2002.06.12) */
static inline void spin_lock_bnode (struct bnode * bnode)
{
	spin_lock (& bnode -> guard);
}

/* Audited by: green(2002.06.12) */
static inline void spin_unlock_bnode (struct bnode * bnode)
{
	spin_unlock (& bnode -> guard);
}

struct bitmap_allocator_data {
	/** an array for bitmap blocks direct access */
	struct bnode * bitmap;
};

#define get_barray(super) \
(((struct bitmap_allocator_data *)(get_super_private(super)->space_allocator.u.generic)) -> bitmap)

#define get_bnode(super, i) (get_barray(super) + i)

/*
 * this file contains:
 * - bitmap based implementation of space allocation plugin
 * - all the helper functions like set bit, find_first_zero_bit, etc
 */

/* Audited by: green(2002.06.12) */
static int find_next_zero_bit_in_byte (unsigned int byte, int start)
{
	unsigned int mask = 1 << start;
	int i = start;

	while ((byte & mask) != 0) {
		mask <<= 1;
		if (++ i >= 8) break;
	}

	return i;
}

#if defined (__KERNEL__)

#include <asm/bitops.h>

#define reiser4_set_bit(nr, addr)    ext2_set_bit(nr, addr)
#define reiser4_clear_bit(nr, addr)  ext2_clear_bit(nr, addr)
#define reiser4_test_bit(nr, addr)  ext2_test_bit(nr, addr)

#define reiser4_find_next_zero_bit(addr, maxoffset, offset) \
ext2_find_next_zero_bit(addr, maxoffset, offset)

#else

static inline void reiser4_set_bit (bmap_off_t nr, void * addr)
{
	unsigned char * base = (char*)addr + (nr  >> 3);
	*base |= (1 << (nr & 0x7));
}

static inline void reiser4_clear_bit (bmap_off_t nr, void * addr)
{
	unsigned char * base = (char*)addr + (nr >> 3);
	*base &= ~(1 << (nr & 0x7));
}

static bmap_nr_t reiser4_find_next_zero_bit (void * addr, bmap_off_t max_offset, bmap_off_t start_offset)
{
	unsigned char * base = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr  = start_offset & 0x7;
	int max_byte_nr = (max_offset - 1) >> 3;

	assert ("zam-388", max_offset != 0);

	if (bit_nr != 0) {
		int nr;

		nr = find_next_zero_bit_in_byte(base[byte_nr], bit_nr);

		if (nr < 8) return (byte_nr << 3) + nr;

		++ byte_nr;
	}

	while (byte_nr <= max_byte_nr) {
		if (base[byte_nr] != 0xFF) {
			return (byte_nr << 3) 
				+ find_next_zero_bit_in_byte(base[byte_nr], 0);
		}

		++ byte_nr;
	}

	return max_offset;
}

#endif

static bmap_nr_t reiser4_find_next_set_bit (void * addr, bmap_off_t max_offset, bmap_off_t start_offset)
{
	unsigned char * base = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr  = start_offset & 0x7;
	int max_byte_nr = (max_offset - 1) >> 3;

	assert ("zam-387", max_offset != 0);

	if (bit_nr != 0) {
		bmap_nr_t nr;

		nr = find_next_zero_bit_in_byte(~ (unsigned int) (base[byte_nr]), bit_nr);

		if (nr < 8) return (byte_nr << 3) + nr;

		++ byte_nr;
	}

	while (byte_nr <= max_byte_nr) {
		if (base[byte_nr] != 0) {
			return (byte_nr << 3) 
				+ find_next_zero_bit_in_byte(
					~ (unsigned int) (base[byte_nr]), 0);
		}

		++ byte_nr;
	}

	return max_offset;
}

/* Audited by: green(2002.06.12) */
static void reiser4_clear_bits (char * addr, bmap_off_t start, bmap_off_t end)
{
	int first_byte;
	int last_byte;

	unsigned char first_byte_mask = 0xFF;
	unsigned char last_byte_mask  = 0xFF;

	assert ("zam-410", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	if (last_byte > first_byte + 1)
		xmemset (addr + first_byte + 1, 0,
			 (size_t)(last_byte - first_byte - 1));

	first_byte_mask >>= 7 - (start & 0x7);
	last_byte_mask  <<= (end - 1) & 0x7;

	if (first_byte == last_byte) {
		addr[first_byte] &= (first_byte_mask | last_byte_mask);
	} else {
		addr[first_byte] &= first_byte_mask;
		addr[last_byte]  &= last_byte_mask;
	}
}

/* Audited by: green(2002.06.12) */
static void reiser4_set_bits (char * addr, bmap_off_t start, bmap_off_t end)
{
	int first_byte;
	int last_byte;

	unsigned char first_byte_mask = 0xFF;
	unsigned char last_byte_mask  = 0xFF;

	assert ("zam-386", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	if (last_byte > first_byte + 1) 
		xmemset (addr + first_byte + 1, 0xFF,
			 (size_t)(last_byte - first_byte - 1));

	first_byte_mask <<= start & 0x7;
	last_byte_mask  >>= 7 - ((end - 1)& 0x7);

	if (first_byte == last_byte) {
		addr[first_byte] |= (first_byte_mask & last_byte_mask);
	} else {
		addr[first_byte] |= first_byte_mask;
		addr[last_byte]  |= last_byte_mask;
	}
}

/* The useful optimization in reiser4 bitmap handling would be dynamic bitmap
 * blocks loading/unloading which is different from v3.x where all bitmap
 * blocks are loaded at mount time.
 *
 * To implement bitmap blocks unloading we need to count bitmap block usage
 * and detect currently unused blocks allowing them to be unloaded. It is not
 * a simple task since we allow several threads to modify one bitmap block
 * simultaneously.
 *
 * Briefly speaking, the following schema is proposed: we count in special
 * variable associated with each bitmap block. That is for counting of block
 * alloc/dealloc operations on that bitmap block. With a deferred block
 * deallocation feature of reiser4 all those operation will be represented in
 * atom dirty/deleted lists as jnodes for freshly allocated or deleted
 * nodes. 
 *
 * So, we increment usage counter for each new node allocated or deleted, and
 * decrement it at atom commit one time for each node from the dirty/deleted
 * atom's list.  Of course, freshly allocated node deletion and node reusing
 * from atom deleted (if we do so) list should decrement bitmap usage counter
 * also.
 *
 * FIXME-ZAM: This schema seems to be working but that reference counting is
 * not easy to debug. I think we should agree with Hans and do not implement
 * it in v4.0. Current code implements "on-demand" bitmap blocks loading only.
 */

#define LIMIT(val, boundary) ((val) > (boundary) ? (boundary) : (val))

/** calculate bitmap block number and offset within that bitmap block */
/* Audited by: green(2002.06.12) */
static void parse_blocknr (const reiser4_block_nr *block, bmap_nr_t *bmap, bmap_off_t *offset)
{
	struct super_block * super = get_current_context()->super;

	*bmap   = *block >> super->s_blocksize_bits;
	*offset = *block & (super->s_blocksize - 1);
} 

/** A number of bitmap blocks for given fs. This number can be stored on disk
 * or calculated on fly; it depends on disk format.
 * FIXME-VS: number of blocks in a filesystem is taken from reiser4
 * super private data */
/* Audited by: green(2002.06.12) */
static bmap_nr_t get_nr_bmap (struct super_block * super)
{
	assert ("zam-393", reiser4_block_count (super) != 0);

	return ((reiser4_block_count (super) - 1) >> (super->s_blocksize_bits + 3)) + 1;
}

/* bnode structure initialization */
/* Audited by: green(2002.06.12) */
static void init_bnode (struct bnode * bnode)
{
	xmemset (bnode, 0, sizeof (struct bnode)); 
	spin_lock_init (& bnode -> guard); 
}

/** return a physical disk address for logical bitmap number @bmap */
/* FIXME-VS: this is somehow related to disk layout? */
#define REISER4_FIRST_BITMAP_BLOCK 100
/* Audited by: green(2002.06.12) */
void get_bitmap_blocknr (struct super_block * super, bmap_nr_t bmap, reiser4_block_nr *bnr)
{

	assert ("zam-390", bmap < get_nr_bmap(super));

	/* FIXME_ZAM: before discussing of disk layouts and disk format
	 * plugins I implement bitmap location scheme which is close to scheme
	 * used in reiser 3.6 */
	if (bmap == 0) {
		*bnr = REISER4_FIRST_BITMAP_BLOCK;
	} else {
		*bnr = bmap * super->s_blocksize * 8;
	}
}

/** plugin->u.space_allocator.init_allocator
 *  constructor of reiser4_space_allocator object. It is called on fs mount
 */
/* Audited by: green(2002.06.12) */
int bitmap_init_allocator (reiser4_space_allocator * allocator,
			   struct super_block * super, void * arg UNUSED_ARG)
{
	struct bitmap_allocator_data * data = NULL;
	bmap_nr_t bitmap_blocks_nr;
	bmap_nr_t i;

	/* getting memory for bitmap allocator private data holder */
	data = reiser4_kmalloc (sizeof (struct bitmap_allocator_data), GFP_KERNEL);

	if (data == NULL) return -ENOMEM;

	/* allocation and initialization for the array of bnodes */
	bitmap_blocks_nr = get_nr_bmap(super); 

	/* FIXME-ZAM: it is not clear what to do with huge number of bitmaps
	 * which is bigger than 2^32. Kmalloc is not possible and, probably,
	 * another dynamic data structure should replace a static array of
	 * bnodes. */
	data->bitmap = reiser4_kmalloc ((size_t)(sizeof(struct bnode) * bitmap_blocks_nr), GFP_KERNEL);

	if (data->bitmap ==  NULL) {
		reiser4_kfree (data, (size_t)(sizeof(struct bnode) * bitmap_blocks_nr));
		return -ENOMEM;
	}

	for (i = 0; i < bitmap_blocks_nr; i++) init_bnode(data -> bitmap + i);

	allocator->u.generic = data;

	return 0;
}


/* plugin->u.space_allocator.destroy_allocator
 * destructor. It is called on fs unmount */
/* Audited by: green(2002.06.12) */
int bitmap_destroy_allocator (reiser4_space_allocator * allocator,
			      struct super_block * super)
{
	bmap_nr_t bitmap_blocks_nr;
	bmap_nr_t i;

	struct bitmap_allocator_data * data = allocator->u.generic;

	assert ("zam-414", data != NULL);
	assert ("zam-376", data -> bitmap != NULL);

	bitmap_blocks_nr = get_nr_bmap(super);

	for (i = 0; i < bitmap_blocks_nr; i ++) {
		struct bnode * bnode = data -> bitmap + i;

		assert ("zam-378", equi(bnode -> wpage == NULL, bnode -> cpage == NULL));

		/* FIXME: we need to release all pinned buffers/pages, it is
		 * not done because release_node isn't ready */
	}

	reiser4_kfree (data->bitmap, (size_t) (sizeof(struct bnode) * bitmap_blocks_nr));
	reiser4_kfree (data, sizeof (struct bitmap_allocator_data));
	allocator->u.generic = NULL;
	return 0;
}

/* construct a fake block number for shadow bitmap (WORKING BITMAP) block */
/* Audited by: green(2002.06.12) */
void get_working_bitmap_blocknr (bmap_nr_t bmap, reiser4_block_nr *bnr)
{
	*bnr = (reiser4_block_nr) ((bmap 
		& ~REISER4_BLOCKNR_STATUS_BIT_MASK) | REISER4_BITMAP_BLOCKS_STATUS_VALUE);
}

/** Load node at given blocknr, update given pointer. This function should be
 * called under bnode spin lock held */
/* Audited by: green(2002.06.12) */
/* AUDIT (green) I think it incorrect that in case of loading failure 
   load_bnode_half and load_and_lock_bnode still returns locked bnode. It should
   only return locked bnode on success. So that caller can immediattely exit
   on failure without unlocking bnode first */
static int load_bnode_half (struct bnode * bnode, char ** data, reiser4_block_nr *block)
{
	struct super_block * super = get_current_context() -> super;
	int (*read_node) (const reiser4_block_nr *, char **, size_t);

	char * tmp = NULL;
	int    ret;

	spin_unlock_bnode (bnode);

	read_node = current_tree -> read_node;

	assert ("zam-415", read_node != NULL);
 
	ret = read_node(block, &tmp, super -> s_blocksize);

	spin_lock_bnode(bnode);

	if (ret) return ret;

	if (*data == NULL) {
		*data = tmp;
	} else {
		spin_unlock_bnode(bnode);

		/* FIXME: tree->release_node (block) should be called here */
		not_yet("zam-416", "proper node releasing");

		spin_lock_bnode(bnode);

		return 1;
	}

	return 0;
}

/* load bitmap blocks "on-demand" */
/* Audited by: green(2002.06.12) */
static int load_and_lock_bnode (struct bnode * bnode)
{
	struct super_block * super = get_current_context()->super;
	int ret = 0;
	bmap_nr_t bmap_nr = bnode - get_barray(super);
	reiser4_block_nr bnr;

	spin_lock_bnode(bnode);

	if (bnode->cpage == NULL) {
		get_bitmap_blocknr(super, bmap_nr, & bnr);
		ret = load_bnode_half(bnode, & bnode -> cpage, & bnr);

		if (ret < 0) return ret;
	}

	if (bnode->wpage == NULL) {
		get_working_bitmap_blocknr(bmap_nr, &bnr);
		ret = load_bnode_half(bnode, & bnode -> wpage, & bnr);

		if (ret < 0) return ret;

		if (ret == 0) {
			/* commit bitmap is initialized by on-disk bitmap
			 * content (working bitmap in this context) */
			xmemcpy(bnode -> wpage, bnode->cpage, super->s_blocksize);
		}

		ret = 0;
	}
	
	return ret;
}

#if 0

/* calls an actor for each bitmap block which is in a given range of disk
 * blocks with parameters of start and end offsets within bitmap block */
static int bitmap_iterator (reiser4_block_nr *start, reiser4_block_nr *start,
		     int (*actor)(void *, bmap_nr_t bmap, int start_offset, int end_offset), 
		     void * opaque)
{
	struct super_block * super = get_current_sb();
	const int max_offset = super->s_blocksize * 8;

	bmap_nr_t  bmap, end_bmap;
	bmap_off_t offset, end_offset;

	reiser4_block_nr tmp;

	int ret;

	assert ("zam-426", *end > *start);
	assert ("zam-427", *end <= reiser4_block_count (super));
	assert ("zam-428", actor != NULL);

	parse_blocknr (start, &bmap, &offset);
	tmp = *end - 1;
	parse_blocknr (&tmp, &end_bmap, &end_offset);
	++end_offset; 

	for (; bmap < end_bmap; bmap ++, offset = 0) {
		ret = actor (opaque, bmap, offset, max_offset);

		if (ret != 0) return ret;
	}

	return actor (opaque, bmap, offset, end_offset);
} 

#endif

/** This function does all block allocation work but only for one bitmap
 * block.*/
/* FIXME_ZAM: It does not allow us to allocate block ranges across bitmap
 * block responsibility zone boundaries. This had no sense in v3.6 but may
 * have it in v4.x */

/* Audited by: green(2002.06.12) */
static int search_one_bitmap (bmap_nr_t bmap, bmap_off_t *offset, bmap_off_t max_offset, 
			      int min_len, int max_len)
{
	struct super_block * super = get_current_context() -> super;
	struct bnode * bnode = get_bnode (super, bmap);

	bmap_off_t search_end;
	bmap_off_t start;
	bmap_off_t end;

	int ret = 0;

	assert("zam-364", min_len > 0);
	assert("zam-365", max_len >= min_len);
	assert("zam-366", *offset < max_offset);

	ret = load_and_lock_bnode (bnode);
	if (ret) goto out;
	/* ret = 0; */

	start = *offset;

	while (start + min_len < max_offset) {

		start = reiser4_find_next_zero_bit((long*) bnode -> wpage, max_offset, start);

		if (start >= max_offset) break;

		search_end = LIMIT(start + max_len, max_offset);
		end = reiser4_find_next_set_bit((long*) bnode->wpage, search_end, start);

		if (end >= start + min_len) {
			ret = end - start;
			*offset = start;

			reiser4_set_bits(bnode->wpage, start, end);
			break;
		}

		start = end + 1;
	}

 out:
	spin_unlock_bnode(bnode);
	/*release_bnode(bnode);*/
	return ret;
}

/** allocate contiguous range of blocks in bitmap */
/* Audited by: green(2002.06.12) */
int bitmap_alloc (reiser4_block_nr *start, const reiser4_block_nr *end, int min_len, int max_len)
{
	bmap_nr_t bmap, end_bmap;
	bmap_off_t offset, end_offset;
	int len;

	reiser4_block_nr tmp;

	struct super_block * super = get_current_context()->super;
	bmap_off_t max_offset = super->s_blocksize << 3;

	parse_blocknr(start, &bmap, &offset);

	tmp = *end - 1;
	parse_blocknr(&tmp, &end_bmap, &end_offset);
	++ end_offset;

	assert("zam-358", end_bmap >= bmap);
	assert("zam-359", ergo(end_bmap == bmap, end_offset > offset));

	for (; bmap < end_bmap; bmap ++, offset = 0) {
		len = search_one_bitmap (bmap, &offset, max_offset, min_len, max_len);
		if (len != 0) goto out;
	}

	len = search_one_bitmap(bmap, &offset, end_offset, min_len, max_len);

 out:
	*start = bmap * max_offset + offset;
	return len;
}

/* plugin->u.space_allocator.alloc_blocks */
/* Audited by: green(2002.06.12) */
int bitmap_alloc_blocks (reiser4_space_allocator * allocator UNUSED_ARG,
			 reiser4_blocknr_hint * hint, int needed,
			 reiser4_block_nr * start, reiser4_block_nr * len)
{
	struct super_block      * super = get_current_context()->super;

	int      actual_len;

	reiser4_block_nr search_start;
	reiser4_block_nr search_end;

	assert ("zam-398", super != NULL);
	assert ("zam-412", hint != NULL);
	assert ("zam-397", hint->blk < reiser4_block_count (super));

	/* These blocks should have been allocated as "new", "not-yet-mapped"
	 * blocks, so we should not decrease blocks_free count twice. */

	/* first, we use @hint -> blk as a search start and search from it to
	 * the end of the disk or in given region if @hint -> max_dist is not
	 * zero */

	search_start = hint -> blk;

	if (hint -> max_dist == 0) {
		search_end = reiser4_block_count(super);
	} else {
		search_end = LIMIT(search_start + hint -> max_dist, reiser4_block_count(super));
	}

	actual_len = bitmap_alloc (&search_start, &search_end, 1, needed);

	/* there is only one bitmap search if max_dist was specified or first
	 * pass was from the beginning of the bitmap */
	if (actual_len != 0 || hint -> max_dist != 0 || search_start == 0) goto out;

	/* next step is a scanning from 0 to search_start */
	search_end = search_start;
	search_start = 0;
	actual_len = bitmap_alloc (&search_start, &search_end, 1, needed);

 out:
	if (actual_len == 0) return -ENOSPC;

	if (actual_len < 0) return actual_len;

	*len = actual_len;
	*start = search_start;

	return 0;
}

/*
 * These functions are hooks from the journal code to manipulate COMMIT BITMAP
 * and WORKING BITMAP objects.
 */

#if REISER4_DEBUG

/* Audited by: green(2002.06.12) */
static void check_block_range (const reiser4_block_nr * start, const reiser4_block_nr * len)
{
	struct super_block * sb = reiser4_get_current_sb();

	assert ("zam-436", sb != NULL);

	assert ("zam-455", start != NULL);
	assert ("zam-437", *start != 0);
	assert ("zam-441", *start < reiser4_block_count(sb));

	if (len != NULL) {
		assert ("zam-438", *len != 0);
		assert ("zam-442", *start + *len <= reiser4_block_count(sb));
	}
}

#else

#  define check_block_range(start, len) do { /* nothing */} while(0)

#endif

static inline void add_bnode_to_commit_list (struct bnode ** commit_list, struct bnode * bnode)
{
	if (bnode -> next_in_commit_list != NULL) return;

	bnode -> next_in_commit_list = *commit_list;
	*commit_list = bnode;
}


/** an actor which applies delete set to COMMIT bitmap pages and link modified
 * pages in a single-linked list */
/* Audited by: green(2002.06.12) */
static int apply_dset_to_commit_bmap (txn_atom               * atom UNUSED_ARG,
				      const reiser4_block_nr * start,
				      const reiser4_block_nr * len,
				      void                   * data)
{
	
	bmap_nr_t bmap;
	bmap_off_t offset;

	struct bnode       * bnode;
	struct bnode      ** commit_list = data;

	struct super_block * sb = reiser4_get_current_sb();

	check_block_range (start, len);

	parse_blocknr(start, &bmap, &offset);

	/* FIXME-ZAM: we assume that all block ranges are allocated by this
	 * bitmap-based allocator and each block range can't go over a zone of
	 * responsibility of one bitmap block; same assumption is used in
	 * other journal hooks in bitmap code. */
	bnode = get_bnode(sb, bmap);
	assert ("zam-448", bnode != NULL);

	/* put bnode in a special list for post-processing */
	add_bnode_to_commit_list (commit_list, bnode);

	/* apply DELETE SET */
	assert ("zam-444", bnode->cpage != NULL);

	spin_lock_bnode (bnode);

	if (len != NULL) {
		/* FIXME-ZAM: a check that all bits are set should be there */
		assert ("zam-443", offset + *len <= sb->s_blocksize);
		reiser4_clear_bits (bnode->cpage, offset, (bmap_off_t)(offset + *len));
	} else {
		reiser4_set_bit (offset, bnode->cpage);
	}

	spin_unlock_bnode (bnode);

	return 0;
}

/** It just applies transaction changes to fs-wide COMMIT BITMAP, hoping the
 * rest is done by transaction manager (allocate wandered locations for COMMIT
 * BITMAP blocks, copy COMMIT BITMAP blocks data). */
/* Audited by: green(2002.06.12) */
/* Only one instance of this function can be running at one given time, because
   only one transaction can be commited at a time, therefore it is safe to
   access some global variables like commit_list without any locking */
void bitmap_pre_commit_hook (void)
{
	reiser4_context * ctx = get_current_context ();

	txn_handle      * tx;
	txn_atom        * atom;

	struct bnode    * commit_list = NULL;

	assert ("zam-433", ctx != NULL);

	tx = ctx->trans;
	assert ("zam-434", tx != NULL);

	atom = atom_get_locked_by_txnh(tx);
	assert ("zam-435", atom != 0);

	blocknr_set_iterator (atom, &atom->delete_set, apply_dset_to_commit_bmap, &commit_list, 0);

	{ /* scan atom's captured list and find all freshly allocated nodes,
	   * mark corresponded bits in COMMIT BITMAP as used */
		int level;

		for (level = 0; level < REAL_MAX_ZTREE_HEIGHT; level ++) {
			capture_list_head * head = &atom->dirty_nodes[level];
			jnode * node = capture_list_front (head);

			while (!capture_list_end (head, node)) {
				/* we detect freshly allocated jnodes */
				if (JF_ISSET(node, ZNODE_ALLOC)) {
					bmap_nr_t  bmap;
					bmap_off_t offset;
					struct bnode * bn;

					assert ("zam-460", !blocknr_is_fake(& node->blocknr));

					parse_blocknr(& node->blocknr, &bmap, &offset);

					bn = get_bnode (ctx->super, bmap);

					assert ("zam-458", bn != NULL);
					assert ("zam-459", bn->cpage != NULL); 
					
					spin_lock_bnode (bn);
					reiser4_set_bit (offset, bn->cpage);
					spin_unlock_bnode (bn);

					/* we use the same commit list to
					 * store bnodes we will capture */
					add_bnode_to_commit_list (&commit_list, bn);
				}

				node = capture_list_next(node);
			}
			
		}
	}

	spin_unlock_atom (atom);

	/* adding bnode->cpage into the transaction. it may wait, so it is
	 * done as a bnode commit list post-processing */
	while (commit_list != NULL) {
		struct bnode * bnode = commit_list;
		struct page  * page;
		int err;

		/* FIXME: page locking ?*/
//		page = virt_to_page (bnode->cpage);
		
		assert ("zam-445", page != NULL);

		err = txn_try_capture_page(page, ZNODE_WRITE_LOCK, 0);

		if (err == -EAGAIN) continue;

		commit_list = bnode->next_in_commit_list;
		bnode->next_in_commit_list = NULL;
	}
}

/* FIXME: it probably needs to be changed when I get understanding what
 * wandered map format Josh proposed. I assume for now that wandered set
 * contains pairs (original location, target location). */
/* Audited by: green(2002.06.12) */
/** an actor which applies delete set to WORKING BITMAP pages */
static int apply_dset_to_working_bmap (txn_atom               * atom UNUSED_ARG,
				       const reiser4_block_nr * start,
				       const reiser4_block_nr * len,
				       void                   * data UNUSED_ARG)
{
	struct super_block * sb = reiser4_get_current_sb ();

	struct bnode * bnode;
	bmap_nr_t bmap;
	bmap_off_t offset;

	check_block_range (start, len);

	parse_blocknr (start, &bmap, &offset);

	bnode = get_bnode (sb, bmap);
	assert ("zam-447", bnode != NULL);

	assert ("zam-450", bnode->wpage != NULL);

	spin_lock_bnode (bnode);

	if (len != NULL) {
		assert ("zam-449", offset + *len <= sb->s_blocksize);
		reiser4_clear_bits(bnode->wpage, offset, (bmap_off_t)(offset + *len));
	} else {
		reiser4_set_bit (offset, bnode->wpage);
	}

	spin_unlock_bnode(bnode);

	return 0;
}

/** called after transaction commit, apply DELETE SET to WORKING BITMAP */
/* Audited by: green(2002.06.12) */
void bitmap_post_commit_hook (void) {
	reiser4_context * ctx = get_current_context ();

	txn_handle * tx;
	txn_atom   * atom;

	tx = ctx->trans;
	assert ("zam-451", tx != NULL);

	atom = atom_get_locked_by_txnh (tx);
	assert ("zam-452", atom != NULL);

	blocknr_set_iterator (atom, &atom->delete_set, apply_dset_to_working_bmap, NULL, 1);

	spin_unlock_atom (atom);
}

/** an actor which clears all wandered locations in a WORKING BITMAP */

/* FIXME-ZAM: I assume that WANDERED MAP is stored in blocknr set data
 * structure as a set of pairs (a = <real block location>, b = <wandered block
 * location>). So, this procedure just makes all blocks which were temporary
 * used for writing of wandered blocks free for reusing -- i.e. clears bits in
 * WORKING BITMAP */

static int apply_wset_to_working_bmap (
	txn_atom               * atom UNUSED_ARG,
	const reiser4_block_nr * a UNUSED_ARG, 
	const reiser4_block_nr * b,
	void                   * data UNUSED_ARG)
{
	struct bnode * bnode;
	struct super_block * sb = reiser4_get_current_sb ();

	bmap_nr_t bmap;
	bmap_off_t offset;

	parse_blocknr (b, &bmap, &offset);

	bnode = get_bnode (sb, bmap);
	assert ("zam-456", bnode != NULL);
	assert ("zam-457", bnode->wpage != NULL);

	spin_lock_bnode(bnode);
	reiser4_clear_bit(offset, bnode->wpage); 
	spin_unlock_bnode(bnode);

	return 0;
}

/** This function is called after write-back (writing blocks from OVERWRITE
 * SET to real locations) transaction stage completes. (clear WANDERED SET in
 * WORKING BITMAP) */
/* Audited by: green(2002.06.12) */
void bitmap_post_write_back_hook (void)
{
	reiser4_context * ctx = get_current_context ();

	txn_handle * tx;
	txn_atom   * atom;

	tx = ctx->trans;
	assert ("zam-453", tx != NULL);

	atom = atom_get_locked_by_txnh (tx);
	assert ("zam-454", atom != NULL);

	blocknr_set_iterator (atom, &atom->wandered_map, apply_wset_to_working_bmap, NULL, 1);

	spin_unlock_atom (atom);
}

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
