/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"
#include "bitmap.h"

/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. */
struct bnode {
	struct semaphore sema;	/* long term lock object */

	jnode      * wjnode;	/* j-nodes for WORKING ... */
	jnode      * cjnode;	/* ... and COMMIT bitmap blocks */

	bmap_off_t first_zero_bit;	/* for skip_busy option implementation */

	int        loaded       /* a flag which shows that bnode is loaded
				 * already */;
};

static inline char * bnode_working_data (struct bnode * bnode) {
	char * data;

	data = jdata(bnode->wjnode);
	assert ("zam-429", data != NULL);

	return data;
}

static inline char * bnode_commit_data (struct bnode * bnode) {
	char * data;

	data = jdata(bnode->cjnode);
	assert ("zam-430", data != NULL);

	return data;
}


struct bitmap_allocator_data {
	/** an array for bitmap blocks direct access */
	struct bnode * bitmap;
};

#define get_barray(super) \
(((struct bitmap_allocator_data *)(get_super_private(super)->space_allocator.u.generic)) -> bitmap)

#define get_bnode(super, i) (get_barray(super) + i)

/* allocate and initialize jnode with JNODE_BITMAP type */
static jnode * bnew (void)
{
	jnode * jal = jnew ();

	if (jal) jnode_set_type(jal, JNODE_BITMAP);

	return jal;
}
 
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
	unsigned char   mask = (1 << (nr & 0x7));

	*base |= mask;
}

static inline void reiser4_clear_bit (bmap_off_t nr, void * addr)
{
	unsigned char * base = (char*)addr + (nr >> 3);
	unsigned char   mask = (1 << (nr & 0x7));

	*base &= ~mask;
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

static inline int reiser4_test_bit (bmap_off_t nr, void * addr)
{
	unsigned char * base = (char*)addr + (nr  >> 3);
	return *base & (1 << (nr & 0x7));
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

/** calculate bitmap block number and offset within that bitmap block */
static void parse_blocknr (const reiser4_block_nr *block, bmap_nr_t *bmap, bmap_off_t *offset)
{
	struct super_block * super = get_current_context()->super;

	*bmap   = *block >> (super->s_blocksize_bits + 3);
	*offset = *block & ((super->s_blocksize << 3) - 1);

	assert ("zam-433", *bmap < get_nr_bmap(super));
} 

#if REISER4_DEBUG

/* Audited by: green(2002.06.12) */
static void check_block_range (const reiser4_block_nr * start, const reiser4_block_nr * len)
{
	struct super_block * sb = reiser4_get_current_sb();

	assert ("zam-436", sb != NULL);

	assert ("zam-455", start != NULL);
	assert ("zam-437", *start != 0);
	assert ("zam-541", !blocknr_is_fake (start));
	assert ("zam-441", *start < reiser4_block_count(sb));

	if (len != NULL) {
		assert ("zam-438", *len != 0);
		assert ("zam-442", *start + *len <= reiser4_block_count(sb));
	}
}

static void check_bnode_loaded (const struct bnode * bnode)
{
	assert ("zam-485", bnode != NULL);
	assert ("zam-483", jnode_page(bnode->wjnode) != NULL);
	assert ("zam-484", jnode_page(bnode->cjnode) != NULL);
}

#else

#  define check_block_range(start, len) do { /* nothing */} while(0)
#  define check_bnode_loaded(bnode)     do { /* nothing */} while(0)

#endif

/** modify bnode->first_zero_bit (if we free bits before); bnode should be
 * spin-locked */
static inline void adjust_first_zero_bit (struct bnode * bnode, bmap_off_t offset)
{
	if (offset < bnode->first_zero_bit)
		bnode->first_zero_bit = offset;
}


/** return a physical disk address for logical bitmap number @bmap */
/* FIXME-VS: this is somehow related to disk layout? */
#define REISER4_FIRST_BITMAP_BLOCK 18
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

/* construct a fake block number for shadow bitmap (WORKING BITMAP) block */
/* Audited by: green(2002.06.12) */
void get_working_bitmap_blocknr (bmap_nr_t bmap, reiser4_block_nr *bnr)
{
	*bnr = (reiser4_block_nr) ((bmap 
		& ~REISER4_BLOCKNR_STATUS_BIT_MASK) | REISER4_BITMAP_BLOCKS_STATUS_VALUE);
}

/* bnode structure initialization */
static void init_bnode (struct bnode * bnode, struct super_block * super, bmap_nr_t bmap)
{
	xmemset (bnode, 0, sizeof (struct bnode)); 

	sema_init (&bnode->sema, 1);

}

/* This function is for internal bitmap.c use because it assumes that jnode is
 * in under full control of this thread */
static void invalidate_jnode(jnode * node)
{
	if (node) {
		int relsed = 0;
		spin_lock_jnode (node);
		if (JF_ISSET(node, JNODE_LOADED)) {
			jrelse_nolock(node);
			relsed = 1;
		}
		spin_unlock_jnode(node);
		if (relsed)
			jput(node);
		jdrop(node);
	}
}

/** plugin->u.space_allocator.init_allocator
 *  constructor of reiser4_space_allocator object. It is called on fs mount
 */
int bitmap_init_allocator (reiser4_space_allocator * allocator,
			   struct super_block * super, void * arg UNUSED_ARG)
{
	struct bitmap_allocator_data * data = NULL;
	bmap_nr_t bitmap_blocks_nr;
	bmap_nr_t i;

	ON_DEBUG_CONTEXT( assert( "green-3", 
				  lock_counters() -> spin_locked == 0 ) );
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

	for (i = 0; i < bitmap_blocks_nr; i++) init_bnode(data -> bitmap + i, super, i);

	allocator->u.generic = data;

	return 0;
}


/* plugin->u.space_allocator.destroy_allocator
 * destructor. It is called on fs unmount */
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

		down (&bnode->sema);

		if (bnode->loaded) {
			jnode *wj = bnode->wjnode;
			jnode *cj = bnode->cjnode;

			assert ("zam-480", jnode_page (cj) != NULL);
			assert ("zam-633", jnode_page (wj) != NULL);

			if (REISER4_DEBUG) {
				jload(wj);
				jload(cj);

				assert ("zam-634", memcmp(jdata(wj), jdata(wj), super->s_blocksize) == 0);

				jrelse(wj);
				jrelse(cj);
			}

			unpin_jnode_data (cj);
			unpin_jnode_data (wj);

			invalidate_jnode (wj);
			invalidate_jnode (cj);

			bnode->wjnode = NULL;
			bnode->cjnode = NULL;

			bnode->loaded = 0;
		}

		up (&bnode->sema);
	}

	reiser4_kfree (data->bitmap, (size_t) (sizeof(struct bnode) * bitmap_blocks_nr));
	reiser4_kfree (data, sizeof (struct bitmap_allocator_data));

	allocator->u.generic = NULL;

	return 0;
}

/* load bitmap blocks "on-demand" */
static int load_and_lock_bnode (struct bnode * bnode)
{
	int ret;

	down (&bnode->sema);

	if (!bnode->loaded) {
		struct super_block * super = get_current_context()->super;
		bmap_nr_t bmap;

		ret = -ENOMEM;

		if ((bnode->wjnode = bnew ()) == NULL) goto fail;

		if ((bnode->cjnode = bnew ()) == NULL) goto fail;

		bmap = bnode - get_bnode(super, 0);

		get_working_bitmap_blocknr (bmap, &bnode->wjnode->blocknr); 
		get_bitmap_blocknr (super, bmap, &bnode->cjnode->blocknr);
		
		if ((ret = jload (bnode->cjnode)) < 0) goto fail; 

		/* allocate memory for working bitmap block */
		ret = jinit_new(bnode->wjnode);
		if (ret < 0) {
			goto fail;
		}

		/* node has been loaded by this jload call  */
		/* working bitmap is initialized by on-disk commit bitmap */
		xmemcpy(bnode_working_data(bnode), bnode_commit_data(bnode), super->s_blocksize);

		pin_jnode_data(bnode->wjnode);
		pin_jnode_data(bnode->cjnode);

		bnode->loaded = 1;
	} else {
		if ((ret = jload(bnode->wjnode)) < 0) goto fail;
		if ((ret = jload(bnode->cjnode)) < 0) goto fail;
	}

	return 0;

fail:
	invalidate_jnode (bnode->wjnode);
	invalidate_jnode (bnode->cjnode);

	bnode->wjnode = NULL;
	bnode->cjnode = NULL;

	up (&bnode->sema);

	return ret;
}

static void release_and_unlock_bnode (struct bnode * bnode)
{
	jrelse (bnode->cjnode);
	jrelse (bnode->wjnode);

	up (&bnode->sema);
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

static int search_one_bitmap (bmap_nr_t bmap, bmap_off_t *offset, bmap_off_t max_offset, 
			      int min_len, int max_len)
{
	struct super_block * super = get_current_context() -> super;
	struct bnode * bnode = get_bnode (super, bmap);

	char * data;

	bmap_off_t search_end;
	bmap_off_t start;
	bmap_off_t end;

	int set_first_zero_bit = 0;

	int ret;

	assert("zam-364", min_len > 0);
	assert("zam-365", max_len >= min_len);
	assert("zam-366", *offset < max_offset);

	ret = load_and_lock_bnode (bnode);

	if (ret) return ret;

	data = bnode_working_data (bnode);

	start = *offset;

	if (bnode->first_zero_bit >= start) {
		start = bnode->first_zero_bit;
		set_first_zero_bit = 1;
	}

	while (start + min_len < max_offset) {

		start = reiser4_find_next_zero_bit((long*) data, max_offset, start);

		if (set_first_zero_bit) {
			bnode->first_zero_bit = start;
			set_first_zero_bit = 0;
		}

		if (start >= max_offset) break;

		search_end = LIMIT(start + max_len, max_offset);
		end = reiser4_find_next_set_bit((long*) data, search_end, start);

		if (end >= start + min_len) {
			/* we can't trust find_next_set_bit result if set bit
			 * was not fount, result may be bigger than
			 * max_offset */
			if (end > search_end) end = search_end;

			ret = end - start;
			*offset = start;

			reiser4_set_bits(data, start, end);

			/* FIXME: we may advance first_zero_bit if [start,
			 * end] region overlaps the first_zero_bit point */

			break; 
		}

		start = end + 1;
	}

	release_and_unlock_bnode (bnode);

	return ret;
}

/** allocate contiguous range of blocks in bitmap */

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

/* plugin->u.space_allocator.alloc_blocks() */
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

/** plugin->u.space_allocator.dealloc_blocks(). */
/* It just frees blocks in WORKING BITMAP. Usually formatted an unformatted
 * nodes deletion is deferred until transaction commit.  However, deallocation
 * of temporary objects like wandered blocks and transaction commit records
 * requires immediate node deletion from WORKING BITMAP.*/
void bitmap_dealloc_blocks (reiser4_space_allocator * allocator UNUSED_ARG,
			    reiser4_block_nr start,
			    reiser4_block_nr len)
{
	struct super_block * super = reiser4_get_current_sb();

	bmap_nr_t  bmap;
	bmap_off_t offset;

	struct bnode * bnode;
	int ret;

	assert ("zam-468", len != 0);
	check_block_range (&start, &len);

	parse_blocknr (&start, &bmap, &offset);

	assert ("zam-469", offset + len <= (super->s_blocksize << 3));

	bnode = get_bnode (super, bmap);

	assert ("zam-470", bnode != NULL);

	ret = load_and_lock_bnode (bnode);
	assert ("zam-481", ret == 0);

	reiser4_clear_bits (bnode_working_data(bnode), offset, (bmap_off_t)(offset + len));

	adjust_first_zero_bit (bnode, offset);

	release_and_unlock_bnode (bnode);
}

#if REISER4_DEBUG

void bitmap_check_blocks (const reiser4_block_nr *start, const reiser4_block_nr * len, int desired)
{
	struct super_block * super = reiser4_get_current_sb();

	bmap_nr_t  bmap;
	bmap_off_t start_offset;
	bmap_off_t end_offset;

	struct bnode * bnode;
	int ret;

	assert ("zam-622", len != NULL);
	check_block_range (start, len);
	parse_blocknr (start, &bmap, &start_offset);

	end_offset = start_offset + *len;
	assert ("nikita-2214", end_offset <= (super->s_blocksize << 3));

	bnode = get_bnode (super, bmap);

	assert ("nikita-2215", bnode != NULL);

	ret = load_and_lock_bnode (bnode);
	assert ("zam-626", ret == 0);

	assert ("nikita-2216", JF_ISSET(bnode->wjnode, JNODE_LOADED));

	if (desired) {
		assert ("zam-623", 
			reiser4_find_next_zero_bit(bnode_working_data(bnode), end_offset, start_offset)
			>= end_offset);
	} else {
		assert ("zam-624",
			reiser4_find_next_set_bit(bnode_working_data(bnode), end_offset, start_offset)
			>= end_offset);
	}

	release_and_unlock_bnode (bnode);
}

#endif

/* conditional insertion of @node into atom's clean list if it was not there */
static void cond_add_to_clean_list (txn_atom * atom, jnode * node)
{
	assert ("zam-546", atom != NULL);
	assert ("zam-547", spin_atom_is_locked(atom));
	assert ("zam-548", node != NULL);

	spin_lock_jnode (node);

	if (node->atom == NULL) {
		jnode_set_wander(node);
		txn_insert_into_clean_list (atom, node);
	} else {
		assert ("zam-549", node->atom == atom);
	}

	spin_unlock_jnode (node);
}


/** an actor which applies delete set to COMMIT bitmap pages and link modified
 * pages in a single-linked list */
/* Audited by: green(2002.06.12) */
static int apply_dset_to_commit_bmap (txn_atom               * atom,
				      const reiser4_block_nr * start,
				      const reiser4_block_nr * len,
				      void                   * data)
{
	
	bmap_nr_t bmap;
	bmap_off_t offset;

	long long * blocks_freed_p = data;

	struct bnode       * bnode;

	struct super_block * sb = reiser4_get_current_sb();

	check_block_range (start, len);

	parse_blocknr(start, &bmap, &offset);

	/* FIXME-ZAM: we assume that all block ranges are allocated by this
	 * bitmap-based allocator and each block range can't go over a zone of
	 * responsibility of one bitmap block; same assumption is used in
	 * other journal hooks in bitmap code. */
	bnode = get_bnode(sb, bmap);
	assert ("zam-448", bnode != NULL);
	
	/* apply DELETE SET */
	check_bnode_loaded (bnode);
	load_and_lock_bnode (bnode);

	/* put bnode in a special list for post-processing */
	cond_add_to_clean_list (atom, bnode->cjnode);

	data = bnode_commit_data(bnode);

	if (len != NULL) {
		/* FIXME-ZAM: a check that all bits are set should be there */
		assert ("zam-443", offset + *len <= (sb->s_blocksize << 3));
		reiser4_clear_bits (data, offset, (bmap_off_t)(offset + *len));

		(*blocks_freed_p) += *len;
	} else {
		reiser4_clear_bit (offset, data);
		(*blocks_freed_p) ++;
	}

	release_and_unlock_bnode (bnode);

	return 0;
}

/** It just applies transaction changes to fs-wide COMMIT BITMAP, hoping the
 * rest is done by transaction manager (allocate wandered locations for COMMIT
 * BITMAP blocks, copy COMMIT BITMAP blocks data). */
/* Only one instance of this function can be running at one given time, because
   only one transaction can be committed a time, therefore it is safe to access
   some global variables without any locking */
void bitmap_pre_commit_hook (void)
{
	reiser4_context * ctx = get_current_context ();

	txn_handle      * tx;
	txn_atom        * atom;

	long long       blocks_freed = 0;

	assert ("zam-433", ctx != NULL);

	tx = ctx->trans;
	assert ("zam-434", tx != NULL);

	atom = atom_get_locked_with_txnh_locked(tx);
	assert ("zam-435", atom != 0);
	spin_unlock_txnh(tx);

	blocknr_set_iterator (atom, &atom->delete_set, apply_dset_to_commit_bmap, &blocks_freed, 0);

	{ /* scan atom's captured list and find all freshly allocated nodes,
	   * mark corresponded bits in COMMIT BITMAP as used */
		capture_list_head * head = &atom->clean_nodes;
		jnode * node = capture_list_front (head);

		while (!capture_list_end (head, node)) {
			/* we detect freshly allocated jnodes */
			if (JF_ISSET(node, JNODE_RELOC))
			{
				bmap_nr_t  bmap;
				bmap_off_t offset;
				struct bnode * bn;

				assert ("zam-559", !JF_ISSET(node, JNODE_OVRWR));
				assert ("zam-460", !blocknr_is_fake(& node->blocknr));

				parse_blocknr(& node->blocknr, &bmap, &offset);

				bn = get_bnode (ctx->super, bmap);

				check_bnode_loaded (bn);
					
				load_and_lock_bnode (bn);
				reiser4_set_bit (offset, bnode_commit_data(bn));
				release_and_unlock_bnode (bn);

				blocks_freed --;

				/* working of this depends on how it inserts
				 * new j-node into clean list, because we are
				 * scanning the same list now. It is OK, if
				 * insertion is done to the list front */
				cond_add_to_clean_list(atom, bn->cjnode);
			}

			node = capture_list_next(node);
		}
	}

	spin_unlock_atom (atom);

	{
		__u64 free_committed_blocks;

		reiser4_spin_lock_sb (ctx->super);

		free_committed_blocks = reiser4_free_committed_blocks (ctx->super);

		free_committed_blocks += blocks_freed;
		reiser4_set_free_committed_blocks (ctx->super, free_committed_blocks);

		reiser4_spin_unlock_sb (ctx->super);
	}
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
