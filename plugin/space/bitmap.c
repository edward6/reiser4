/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../../debug.h"
#include "../../dformat.h"
#include "../../txnmgr.h"
#include "../../jnode.h"
#include "../../block_alloc.h"
#include "../../tree.h"
#include "../../super.h"
#include "../../lib.h"

#include "../plugin.h"
#include "../../diskmap.h"

#include "space_allocator.h"
#include "bitmap.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block  */
#include <asm/semaphore.h>

/* The useful optimization in reiser4 bitmap handling would be dynamic bitmap
   blocks loading/unloading which is different from v3.x where all bitmap
   blocks are loaded at mount time.

   To implement bitmap blocks unloading we need to count bitmap block usage
   and detect currently unused blocks allowing them to be unloaded. It is not
   a simple task since we allow several threads to modify one bitmap block
   simultaneously.

   Briefly speaking, the following schema is proposed: we count in special
   variable associated with each bitmap block. That is for counting of block
   alloc/dealloc operations on that bitmap block. With a deferred block
   deallocation feature of reiser4 all those operation will be represented in
   atom dirty/deleted lists as jnodes for freshly allocated or deleted
   nodes.

   So, we increment usage counter for each new node allocated or deleted, and
   decrement it at atom commit one time for each node from the dirty/deleted
   atom's list.  Of course, freshly allocated node deletion and node reusing
   from atom deleted (if we do so) list should decrement bitmap usage counter
   also.

   FIXME-ZAM: This schema seems to be working but that reference counting is
   not easy to debug. I think we should agree with Hans and do not implement
   it in v4.0. Current code implements "on-demand" bitmap blocks loading only.

   For simplicity bitmap node (both commit and working bitmap blocks) are
   loaded into memory on the first access and remain kmapped until umount.
*/

#define CHECKSUM_SIZE    4

#define bmap_size(blocksize)	    ((blocksize) - CHECKSUM_SIZE)
#define bmap_bit_count(blocksize)   (bmap_size(blocksize) << 3)

/* Block allocation/deallocation are done through special bitmap objects which
   are allocated in an array at fs mount. */
struct bnode {
	struct semaphore sema;	/* long term lock object */

	jnode *wjnode;		/* j-nodes for WORKING ... */
	jnode *cjnode;		/* ... and COMMIT bitmap blocks */

	bmap_off_t first_zero_bit;	/* for skip_busy option implementation */

	atomic_t loaded;	/* a flag which shows that bnode is loaded
				 * already */
};

static inline char *
bnode_working_data(struct bnode *bnode)
{
	char *data;

	data = jdata(bnode->wjnode);
	assert("zam-429", data != NULL);

	return data + CHECKSUM_SIZE;
}

static inline char *
bnode_commit_data(struct bnode *bnode)
{
	char *data;

	data = jdata(bnode->cjnode);
	assert("zam-430", data != NULL);

	return data + CHECKSUM_SIZE;
}

static inline __u32 *
bnode_commit_crc(struct bnode *bnode)
{
	char *data;

	data = jdata(bnode->cjnode);
	assert("vpf-261", data != NULL);

	return (__u32 *) data;
}

struct bitmap_allocator_data {
	/* an array for bitmap blocks direct access */
	struct bnode *bitmap;
};

#define get_barray(super) \
(((struct bitmap_allocator_data *)(get_super_private(super)->space_allocator.u.generic)) -> bitmap)

#define get_bnode(super, i) (get_barray(super) + i)

/* allocate and initialize jnode with JNODE_BITMAP type */
static jnode *
bnew(void)
{
	jnode *jal = jalloc();

	if (jal)
		jnode_init(jal, current_tree, JNODE_BITMAP);

	return jal;
}

/* this file contains:
   - bitmap based implementation of space allocation plugin
   - all the helper functions like set bit, find_first_zero_bit, etc */

/* Audited by: green(2002.06.12) */
static int
find_next_zero_bit_in_byte(unsigned int byte, int start)
{
	unsigned int mask = 1 << start;
	int i = start;

	while ((byte & mask) != 0) {
		mask <<= 1;
		if (++i >= 8)
			break;
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

static inline void
reiser4_set_bit(bmap_off_t nr, void *addr)
{
	unsigned char *base = (char *) addr + (nr >> 3);
	unsigned char mask = (1 << (nr & 0x7));

	*base |= mask;
}

static inline void
reiser4_clear_bit(bmap_off_t nr, void *addr)
{
	unsigned char *base = (char *) addr + (nr >> 3);
	unsigned char mask = (1 << (nr & 0x7));

	*base &= ~mask;
}

static bmap_off_t
reiser4_find_next_zero_bit(void *addr, bmap_off_t max_offset, bmap_off_t start_offset)
{
	unsigned char *base = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr = start_offset & 0x7;
	int max_byte_nr = (max_offset - 1) >> 3;

	assert("zam-388", max_offset != 0);

	if (bit_nr != 0) {
		int nr;

		nr = find_next_zero_bit_in_byte(base[byte_nr], bit_nr);

		if (nr < 8)
			return (byte_nr << 3) + nr;

		++byte_nr;
	}

	while (byte_nr <= max_byte_nr) {
		if (base[byte_nr] != 0xFF) {
			return (byte_nr << 3)
			    + find_next_zero_bit_in_byte(base[byte_nr], 0);
		}

		++byte_nr;
	}

	return max_offset;
}

static inline int
reiser4_test_bit(bmap_off_t nr, void *addr)
{
	unsigned char *base = (char *) addr + (nr >> 3);
	return *base & (1 << (nr & 0x7));
}

#endif

static bmap_off_t
reiser4_find_next_set_bit(void *addr, bmap_off_t max_offset, bmap_off_t start_offset)
{
	unsigned char *base = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr = start_offset & 0x7;
	int max_byte_nr = (max_offset - 1) >> 3;

	assert("zam-387", max_offset != 0);

	if (bit_nr != 0) {
		bmap_nr_t nr;

		nr = find_next_zero_bit_in_byte(~(unsigned int) (base[byte_nr]), bit_nr);

		if (nr < 8)
			return (byte_nr << 3) + nr;

		++byte_nr;
	}

	while (byte_nr <= max_byte_nr) {
		if (base[byte_nr] != 0) {
			return (byte_nr << 3)
			    + find_next_zero_bit_in_byte(~(unsigned int)
							 (base[byte_nr]), 0);
		}

		++byte_nr;
	}

	return max_offset;
}

static int
find_last_set_bit_in_byte (unsigned byte, int start)
{
	unsigned bit_mask;
	int nr = start;

	assert ("zam-965", start < 8);
	assert ("zam-966", start >= 0);

	bit_mask = (1 << nr);

	while (bit_mask != 0) {
		if (bit_mask & byte)
			return nr;
		bit_mask >>= 1;
		nr --;
	}
	return 8;
}

/* Search bitmap for a set bit in backward direction from the end to the
 * beginning of given region */
static int
reiser4_find_last_set_bit (bmap_off_t * result, void * addr, bmap_off_t low_off, bmap_off_t high_off)
{
	unsigned char * base = addr;
	int last_byte;
	int first_byte;
	int last_bit;
	int nr;

	assert ("zam-961", high_off >= 0);
	assert ("zam-962", high_off >= low_off);

	last_byte = high_off >> 3;
	last_bit = high_off & 0x7;
	first_byte = low_off >> 3;

	if (last_bit <= 7) {
		nr = find_last_set_bit_in_byte(base[last_byte], last_bit);
		if (nr < 8) {
			*result = (last_byte << 3) + nr;
			return 0;
		}
		-- last_byte;
	}
	while (last_byte >= first_byte) {
		if (base[last_byte] != 0x0) {
			last_bit = find_last_set_bit_in_byte((unsigned)base[last_byte], 7);
			assert ("zam-972", last_bit < 8);
			*result = (last_byte << 3) + last_bit;
			return 0;
		}
		-- last_byte;
	}

	return -1;		/* set bit not found */
}

/* Search bitmap for a clear bit in backward direction from the end to the
 * beginning of given region */
static int
reiser4_find_last_zero_bit (bmap_off_t * result, void * addr, bmap_off_t low_off, bmap_off_t high_off)
{
	unsigned char * base = addr;
	int last_byte;
	int first_byte;
	int last_bit;
	int nr;

	last_byte = high_off >> 3;
	last_bit = high_off & 0x7;
	first_byte = low_off >> 3;

	if (last_bit != 0) {
		nr = find_last_set_bit_in_byte(~(unsigned)base[last_byte], last_bit);
		if (nr < 8) {
			 *result = (last_byte << 3) + nr;
			 return 0;
		}
		-- last_byte;
	}
	while (last_byte >= first_byte) {
		if (base[last_byte] != 0xFF) {
			*result =  (last_byte << 3) +
				find_last_set_bit_in_byte(~(unsigned)base[last_byte], 7);
			return 0;
		}
		-- last_byte;
	}

	return -1;	/* zero bit not found */
}

/* Audited by: green(2002.06.12) */
static void
reiser4_clear_bits(char *addr, bmap_off_t start, bmap_off_t end)
{
	int first_byte;
	int last_byte;

	unsigned char first_byte_mask = 0xFF;
	unsigned char last_byte_mask = 0xFF;

	assert("zam-410", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	if (last_byte > first_byte + 1)
		xmemset(addr + first_byte + 1, 0, (size_t) (last_byte - first_byte - 1));

	first_byte_mask >>= 8 - (start & 0x7);
	last_byte_mask <<= ((end - 1) & 0x7) + 1;

	if (first_byte == last_byte) {
		addr[first_byte] &= (first_byte_mask | last_byte_mask);
	} else {
		addr[first_byte] &= first_byte_mask;
		addr[last_byte] &= last_byte_mask;
	}
}

/* Audited by: green(2002.06.12) */
static void
reiser4_set_bits(char *addr, bmap_off_t start, bmap_off_t end)
{
	int first_byte;
	int last_byte;

	unsigned char first_byte_mask = 0xFF;
	unsigned char last_byte_mask = 0xFF;

	assert("zam-386", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	if (last_byte > first_byte + 1)
		xmemset(addr + first_byte + 1, 0xFF, (size_t) (last_byte - first_byte - 1));

	first_byte_mask <<= start & 0x7;
	last_byte_mask >>= 7 - ((end - 1) & 0x7);

	if (first_byte == last_byte) {
		addr[first_byte] |= (first_byte_mask & last_byte_mask);
	} else {
		addr[first_byte] |= first_byte_mask;
		addr[last_byte] |= last_byte_mask;
	}
}

#define ADLER_BASE    65521
#define ADLER_NMAX    5552

/* Calculates the adler32 checksum for the data pointed by `data` of the
    length `len`. This function was originally taken from zlib, version 1.1.3,
    July 9th, 1998.

    Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
	claim that you wrote the original software. If you use this software
	in a product, an acknowledgment in the product documentation would be
	appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
	misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.

    Jean-loup Gailly        Mark Adler
    jloup@gzip.org          madler@alumni.caltech.edu

    The above comment is applyed to the only alder32 function.
*/

static __u32
adler32(char *data, __u32 len)
{
	unsigned char *t = data;
	__u32 s1 = 1;
	__u32 s2 = 0;
	int k;

	while (len > 0) {
		k = len < ADLER_NMAX ? len : ADLER_NMAX;
		len -= k;

		while (k--) {
			s1 += *t++;
			s2 += s1;
		}

		s1 %= ADLER_BASE;
		s2 %= ADLER_BASE;
	}
	return (s2 << 16) | s1;
}

/* Recalculates the adler32 checksum for only 1 byte change.
    adler - previous adler checksum
    old_data, data - old, new byte values.
    tail == (chunk - offset) : length, checksum was calculated for, - offset of
    the changed byte within this chunk.
    This function could be used for checksum calculation optimisation, but not
    used for now. -Vitaly.
*/

static __u32
adler32_recalc(__u32 adler, unsigned char old_data, unsigned char data, __u32 tail)
{
	__u32 delta = data - old_data + 2 * ADLER_BASE;
	__u32 s1 = adler & 0xffff;
	__u32 s2 = (adler >> 16) & 0xffff;

	s1 = (delta + s1) % ADLER_BASE;
	s2 = (delta * tail + s2) % ADLER_BASE;

	return (s2 << 16) | s1;
}

#define LIMIT(val, boundary) ((val) > (boundary) ? (boundary) : (val))

/* A number of bitmap blocks for given fs. This number can be stored on disk
   or calculated on fly; it depends on disk format.
   FIXME-VS: number of blocks in a filesystem is taken from reiser4
   super private data */
/* Audited by: green(2002.06.12) */
static bmap_nr_t
get_nr_bmap(struct super_block *super)
{
	assert("zam-393", reiser4_block_count(super) != 0);

	return div64_32(reiser4_block_count(super) - 1, bmap_bit_count(super->s_blocksize), NULL) + 1;

}

/* calculate bitmap block number and offset within that bitmap block */
static void
parse_blocknr(const reiser4_block_nr * block, bmap_nr_t * bmap, bmap_off_t * offset)
{
	struct super_block *super = get_current_context()->super;

	*bmap = div64_32(*block, bmap_bit_count(super->s_blocksize), offset);

	assert("zam-433", *bmap < get_nr_bmap(super));
}

#if REISER4_DEBUG

/* Audited by: green(2002.06.12) */
static void
check_block_range(const reiser4_block_nr * start, const reiser4_block_nr * len)
{
	struct super_block *sb = reiser4_get_current_sb();

	assert("zam-436", sb != NULL);

	assert("zam-455", start != NULL);
	assert("zam-437", *start != 0);
	assert("zam-541", !blocknr_is_fake(start));
	assert("zam-441", *start < reiser4_block_count(sb));

	if (len != NULL) {
		assert("zam-438", *len != 0);
		assert("zam-442", *start + *len <= reiser4_block_count(sb));
	}
}

static void
check_bnode_loaded(const struct bnode *bnode)
{
	assert("zam-485", bnode != NULL);
	assert("zam-483", jnode_page(bnode->wjnode) != NULL);
	assert("zam-484", jnode_page(bnode->cjnode) != NULL);
	assert("nikita-2820", jnode_is_loaded(bnode->wjnode));
	assert("nikita-2821", jnode_is_loaded(bnode->cjnode));
}

#else

#  define check_block_range(start, len) do { /* nothing */} while(0)
#  define check_bnode_loaded(bnode)     do { /* nothing */} while(0)

#endif

/* modify bnode->first_zero_bit (if we free bits before); bnode should be
   spin-locked */
static inline void
adjust_first_zero_bit(struct bnode *bnode, bmap_off_t offset)
{
	if (offset < bnode->first_zero_bit)
		bnode->first_zero_bit = offset;
}

/* return a physical disk address for logical bitmap number @bmap */
/* FIXME-VS: this is somehow related to disk layout? */
#define REISER4_FIRST_BITMAP_BLOCK 18
/* Audited by: green(2002.06.12) */
void
get_bitmap_blocknr(struct super_block *super, bmap_nr_t bmap, reiser4_block_nr * bnr)
{

	assert("zam-390", bmap < get_nr_bmap(super));

#ifdef CONFIG_REISER4_BADBLOCKS
#define BITMAP_PLUGIN_DISKMAP_ID ((REISER4_SPACE_ALLOCATOR_PLUGIN_TYPE<<16) | (BITMAP_SPACE_ALLOCATOR_ID))
	/* Check if the diskmap have this already, first. */
	if ( reiser4_get_diskmap_value( BITMAP_PLUGIN_DISKMAP_ID, bmap, bnr) == 0 )
		return; /* Found it in diskmap */
#endif
	/* FIXME_ZAM: before discussing of disk layouts and disk format
	   plugins I implement bitmap location scheme which is close to scheme
	   used in reiser 3.6 */
	if (bmap == 0) {
		*bnr = REISER4_FIRST_BITMAP_BLOCK;
	} else {
		*bnr = bmap * bmap_bit_count(super->s_blocksize);
	}
}

/* construct a fake block number for shadow bitmap (WORKING BITMAP) block */
/* Audited by: green(2002.06.12) */
void
get_working_bitmap_blocknr(bmap_nr_t bmap, reiser4_block_nr * bnr)
{
	*bnr = (reiser4_block_nr) ((bmap & ~REISER4_BLOCKNR_STATUS_BIT_MASK) | REISER4_BITMAP_BLOCKS_STATUS_VALUE);
}

/* bnode structure initialization */
static void
init_bnode(struct bnode *bnode,
	   struct super_block *super UNUSED_ARG, bmap_nr_t bmap UNUSED_ARG)
{
	xmemset(bnode, 0, sizeof (struct bnode));

	sema_init(&bnode->sema, 1);
	atomic_set(&bnode->loaded, 0);
}

static void
release(jnode *node)
{
	jrelse(node);
	JF_SET(node, JNODE_HEARD_BANSHEE);
	jput(node);
}

/* This function is for internal bitmap.c use because it assumes that jnode is
   in under full control of this thread */
static void
done_bnode(struct bnode *bnode)
{
	if (bnode) {
		atomic_set(&bnode->loaded, 0);
		if (bnode->wjnode != NULL)
			release(bnode->wjnode);
		if (bnode->cjnode != NULL)
			release(bnode->cjnode);
		bnode->wjnode = bnode->cjnode = NULL;
	}
}


static int
prepare_bnode(struct bnode *bnode, jnode **cjnode_ret, jnode **wjnode_ret)
{
	struct super_block *super;
	jnode *cjnode;
	jnode *wjnode;
	bmap_nr_t bmap;
	int ret;

	super = reiser4_get_current_sb();

	*wjnode_ret = wjnode = bnew();
	if (wjnode == NULL)
		return RETERR(-ENOMEM);

	*cjnode_ret = cjnode = bnew();
	if (cjnode == NULL)
		return RETERR(-ENOMEM);

	bmap = bnode - get_bnode(super, 0);

	get_working_bitmap_blocknr(bmap, &wjnode->blocknr);
	get_bitmap_blocknr(super, bmap, &cjnode->blocknr);

	jref(cjnode);
	jref(wjnode);

	/* load commit bitmap */
	ret = jload_gfp(cjnode, GFP_NOFS);
	if (ret == 0) {
		/* allocate memory for working bitmap block. Note that for
		 * bitmaps jinit_new() doesn't actually modifies node content,
		 * so parallel calls to this are ok. */
		ret = jinit_new(wjnode);
		if (ret != 0)
			jrelse(cjnode);
	}
	if (ret != 0) {
		jput(cjnode);
		jput(wjnode);
		*wjnode_ret = *cjnode_ret = NULL;
	}
	return ret;
}

/* load bitmap blocks "on-demand" */
static int
load_and_lock_bnode(struct bnode *bnode)
{
	int ret;

	jnode *cjnode;
	jnode *wjnode;

	assert("nikita-3040", schedulable());

	if (atomic_read(&bnode->loaded)) {
		/* bitmap is already loaded, nothing to do */
		check_bnode_loaded(bnode);
		down(&bnode->sema);
		assert("nikita-2827", atomic_read(&bnode->loaded));
		return 0;
	}

	ret = prepare_bnode(bnode, &cjnode, &wjnode);
	if (ret == 0) {
		down(&bnode->sema);

		if (!atomic_read(&bnode->loaded)) {
			assert("nikita-2822", cjnode != NULL);
			assert("nikita-2823", wjnode != NULL);
			assert("nikita-2824", jnode_is_loaded(cjnode));
			assert("nikita-2825", jnode_is_loaded(wjnode));

			bnode->wjnode = wjnode;
			bnode->cjnode = cjnode;

			cjnode = wjnode = NULL;
			atomic_set(&bnode->loaded, 1);
			/* working bitmap is initialized by on-disk commit
			 * bitmap. This should be performed under
			 * semaphore. */
			xmemcpy(bnode_working_data(bnode),
				bnode_commit_data(bnode), bmap_size(current_blocksize));
		} else
			/* race: someone already loaded bitmap while we were
			 * busy initializing data. */
			check_bnode_loaded(bnode);
	}

	if (wjnode != NULL)
		release(wjnode);
	if (cjnode != NULL)
		release(cjnode);

	return ret;
}

static void
release_and_unlock_bnode(struct bnode *bnode)
{
	check_bnode_loaded(bnode);
	up(&bnode->sema);
}

#if 0

/* calls an actor for each bitmap block which is in a given range of disk
   blocks with parameters of start and end offsets within bitmap block */
static int
bitmap_iterator(reiser4_block_nr * start, reiser4_block_nr * start,
		int (*actor) (void *, bmap_nr_t bmap, int start_offset, int end_offset), void *opaque)
{
	struct super_block *super = get_current_sb();
	const int max_offset = super->s_blocksize * 8;

	bmap_nr_t bmap, end_bmap;
	bmap_off_t offset, end_offset;

	reiser4_block_nr tmp;

	int ret;

	assert("zam-426", *end > *start);
	assert("zam-427", *end <= reiser4_block_count(super));
	assert("zam-428", actor != NULL);

	parse_blocknr(start, &bmap, &offset);
	tmp = *end - 1;
	parse_blocknr(&tmp, &end_bmap, &end_offset);
	++end_offset;

	for (; bmap < end_bmap; bmap++, offset = 0) {
		ret = actor(opaque, bmap, offset, max_offset);

		if (ret != 0)
			return ret;
	}

	return actor(opaque, bmap, offset, end_offset);
}

#endif

/* This function does all block allocation work but only for one bitmap
   block.*/
/* FIXME_ZAM: It does not allow us to allocate block ranges across bitmap
   block responsibility zone boundaries. This had no sense in v3.6 but may
   have it in v4.x */

static int
search_one_bitmap_forward(bmap_nr_t bmap, bmap_off_t * offset, bmap_off_t max_offset,
			  int min_len, int max_len)
{
	struct super_block *super = get_current_context()->super;
	struct bnode *bnode = get_bnode(super, bmap);

	char *data;

	bmap_off_t search_end;
	bmap_off_t start;
	bmap_off_t end;

	int set_first_zero_bit = 0;

	int ret;

	assert("zam-364", min_len > 0);
	assert("zam-365", max_len >= min_len);
	assert("zam-366", *offset < max_offset);

	ret = load_and_lock_bnode(bnode);

	if (ret)
		return ret;

	data = bnode_working_data(bnode);

	start = *offset;

	if (bnode->first_zero_bit >= start) {
		start = bnode->first_zero_bit;
		set_first_zero_bit = 1;
	}

	while (start + min_len < max_offset) {

		start = reiser4_find_next_zero_bit((long *) data, max_offset, start);
		if (set_first_zero_bit) {
			bnode->first_zero_bit = start;
			set_first_zero_bit = 0;
		}
		if (start >= max_offset)
			break;

		search_end = LIMIT(start + max_len, max_offset);
		end = reiser4_find_next_set_bit((long *) data, search_end, start);
		if (end >= start + min_len) {
			/* we can't trust find_next_set_bit result if set bit
			   was not fount, result may be bigger than
			   max_offset */
			if (end > search_end)
				end = search_end;

			ret = end - start;
			*offset = start;

			reiser4_set_bits(data, start, end);

			/* FIXME: we may advance first_zero_bit if [start,
			   end] region overlaps the first_zero_bit point */

			break;
		}

		start = end + 1;
	}

	release_and_unlock_bnode(bnode);

	return ret;
}

static int
search_one_bitmap_backward (bmap_nr_t bmap, bmap_off_t * start_offset, bmap_off_t end_offset,
			    int min_len, int max_len)
{
	struct super_block *super = get_current_context()->super;
	struct bnode *bnode = get_bnode(super, bmap);
	char *data;
	bmap_off_t start;
	int ret;

	assert("zam-958", min_len > 0);
	assert("zam-959", max_len >= min_len);
	assert("zam-960", *start_offset >= end_offset);

	ret = load_and_lock_bnode(bnode);
	if (ret)
		return ret;

	data = bnode_working_data(bnode);
	start = *start_offset;

	while (1) {
		bmap_off_t end, search_end;

		/* Find the beginning of the zero filled region */
		if (reiser4_find_last_zero_bit(&start, data, end_offset, start))
			break;
		/* Is there more than `min_len' bits from `start' to
		 * `end_offset'?  */
		if (start < end_offset + min_len - 1)
			break;

		/* Do not search to `end_offset' if we need to find less than
		 * `max_len' zero bits. */
		if (end_offset + max_len - 1 < start)
			search_end = start - max_len + 1;
		else
			search_end = end_offset;

		if (reiser4_find_last_set_bit(&end, data, search_end, start))
			end = search_end;
		else
			end ++;

		if (end + min_len <= start + 1) {
			if (end < search_end)
				end = search_end;
			ret = start - end + 1;
			*start_offset = end; /* `end' is lowest offset */
			assert ("zam-987", reiser4_find_next_set_bit(data, start + 1, end) >= start + 1);
			reiser4_set_bits(data, end, start + 1);
			break;
		}

		if (end <= end_offset)
			/* left search boundary reached. */
			break;
		start = end - 1;
	}

	release_and_unlock_bnode(bnode);
	return ret;
}

/* allocate contiguous range of blocks in bitmap */
static int bitmap_alloc_forward(reiser4_block_nr * start, const reiser4_block_nr * end,
				int min_len, int max_len)
{
	bmap_nr_t bmap, end_bmap;
	bmap_off_t offset, end_offset;
	int len;

	reiser4_block_nr tmp;

	struct super_block *super = get_current_context()->super;
	const bmap_off_t max_offset = bmap_bit_count(super->s_blocksize);

	parse_blocknr(start, &bmap, &offset);

	tmp = *end - 1;
	parse_blocknr(&tmp, &end_bmap, &end_offset);
	++end_offset;

	assert("zam-358", end_bmap >= bmap);
	assert("zam-359", ergo(end_bmap == bmap, end_offset > offset));

	for (; bmap < end_bmap; bmap++, offset = 0) {
		len = search_one_bitmap_forward(bmap, &offset, max_offset, min_len, max_len);
		if (len != 0)
			goto out;
	}
		
	len = search_one_bitmap_forward(bmap, &offset, end_offset, min_len, max_len);
out:
	*start = bmap * max_offset + offset;
	return len;
}

/* allocate contiguous range of blocks in bitmap (from @start to @end in
 * backward direction) */
static int bitmap_alloc_backward(reiser4_block_nr * start, const reiser4_block_nr * end,
				 int min_len, int max_len)
{
	bmap_nr_t bmap, end_bmap;
	bmap_off_t offset, end_offset;
	int len;
	struct super_block *super = get_current_context()->super;
	const bmap_off_t max_offset = bmap_bit_count(super->s_blocksize);

	parse_blocknr(start, &bmap, &offset);
	parse_blocknr(end, &end_bmap, &end_offset);

	assert("zam-961", end_bmap <= bmap);
	assert("zam-962", ergo(end_bmap == bmap, end_offset <= offset));

	for (; bmap > end_bmap; end_bmap --, offset = max_offset - 1) {
		len = search_one_bitmap_backward(bmap, &offset, 0, min_len, max_len);
		if (len != 0)
			goto out;
	}
		
	len = search_one_bitmap_backward(bmap, &offset, end_offset, min_len, max_len);
 out:
	*start = bmap * max_offset + offset;
	return len;
}

/* plugin->u.space_allocator.alloc_blocks() */
int alloc_blocks_forward(reiser4_blocknr_hint * hint, int needed,
			 reiser4_block_nr * start, reiser4_block_nr * len)
{
	struct super_block *super = get_current_context()->super;
	int actual_len;

	reiser4_block_nr search_start;
	reiser4_block_nr search_end;

	assert("zam-398", super != NULL);
	assert("zam-412", hint != NULL);
	assert("zam-397", hint->blk < reiser4_block_count(super));

	if (hint->max_dist == 0)
		search_end = reiser4_block_count(super);
	else
		search_end = LIMIT(hint->blk + hint->max_dist, reiser4_block_count(super));

	/* We use @hint -> blk as a search start and search from it to the end
	   of the disk or in given region if @hint -> max_dist is not zero */
	search_start = hint->blk;

	actual_len = bitmap_alloc_forward(&search_start, &search_end, 1, needed);

	/* There is only one bitmap search if max_dist was specified or first
	   pass was from the beginning of the bitmap. We also do one pass for
	   scanning bitmap in backward direction. */
	if (!(actual_len != 0 || hint->max_dist != 0  || search_start == 0)) {
		/* next step is a scanning from 0 to search_start */
		search_end = search_start;
		search_start = 0;
		actual_len = bitmap_alloc_forward(&search_start, &search_end, 1, needed);
	}
	if (actual_len == 0)
		return RETERR(-ENOSPC);
	if (actual_len < 0)
		return RETERR(actual_len);
	*len = actual_len;
	*start = search_start;
	return 0;
}

static int alloc_blocks_backward (reiser4_blocknr_hint * hint, int needed,
				  reiser4_block_nr * start, reiser4_block_nr * len)
{
	reiser4_block_nr search_start;
	reiser4_block_nr search_end;
	int actual_len;

	ON_DEBUG(struct super_block * super = reiser4_get_current_sb());

	assert ("zam-969", super != NULL);
	assert ("zam-970", hint != NULL);
	assert ("zam-971", hint->blk < reiser4_block_count(super));

	search_start = hint->blk;
	if (hint->max_dist == 0 || search_start <= hint->max_dist)
		search_end = 0;
	else
		search_end = search_start - hint->max_dist;

	actual_len = bitmap_alloc_backward(&search_start, &search_end, 1, needed);
	if (actual_len == 0)
		return RETERR(-ENOSPC);
	if (actual_len < 0)
		return RETERR(actual_len);
	*len = actual_len;
	*start = search_start;
	return 0;
}

/* plugin->u.space_allocator.alloc_blocks() */
int alloc_blocks_bitmap(reiser4_space_allocator * allocator UNUSED_ARG,
			reiser4_blocknr_hint * hint, int needed,
			reiser4_block_nr * start, reiser4_block_nr * len)
{
	if (hint->backward)
		return alloc_blocks_backward(hint, needed, start, len);
	return alloc_blocks_forward(hint, needed, start, len);
}

/* plugin->u.space_allocator.dealloc_blocks(). */
/* It just frees blocks in WORKING BITMAP. Usually formatted an unformatted
   nodes deletion is deferred until transaction commit.  However, deallocation
   of temporary objects like wandered blocks and transaction commit records
   requires immediate node deletion from WORKING BITMAP.*/
void
dealloc_blocks_bitmap(reiser4_space_allocator * allocator UNUSED_ARG, reiser4_block_nr start, reiser4_block_nr len)
{
	struct super_block *super = reiser4_get_current_sb();

	bmap_nr_t bmap;
	bmap_off_t offset;

	struct bnode *bnode;
	int ret;

	assert("zam-468", len != 0);
	check_block_range(&start, &len);

	parse_blocknr(&start, &bmap, &offset);

	assert("zam-469", offset + len <= bmap_bit_count(super->s_blocksize));

	bnode = get_bnode(super, bmap);

	assert("zam-470", bnode != NULL);

	ret = load_and_lock_bnode(bnode);
	assert("zam-481", ret == 0);

	reiser4_clear_bits(bnode_working_data(bnode), offset, (bmap_off_t) (offset + len));

	adjust_first_zero_bit(bnode, offset);

	release_and_unlock_bnode(bnode);
}

#if REISER4_DEBUG

/* plugin->u.space_allocator.check_blocks(). */
void
check_blocks_bitmap(const reiser4_block_nr * start, const reiser4_block_nr * len, int desired)
{
	struct super_block *super = reiser4_get_current_sb();

	bmap_nr_t bmap;
	bmap_off_t start_offset;
	bmap_off_t end_offset;

	struct bnode *bnode;
	int ret;

	assert("zam-622", len != NULL);
	check_block_range(start, len);
	parse_blocknr(start, &bmap, &start_offset);

	end_offset = start_offset + *len;
	assert("nikita-2214", end_offset <= bmap_bit_count(super->s_blocksize));

	bnode = get_bnode(super, bmap);

	assert("nikita-2215", bnode != NULL);

	ret = load_and_lock_bnode(bnode);
	assert("zam-626", ret == 0);

	assert("nikita-2216", jnode_is_loaded(bnode->wjnode));

	if (desired) {
		assert("zam-623", reiser4_find_next_zero_bit(bnode_working_data(bnode), end_offset, start_offset)
		       >= end_offset);
	} else {
		assert("zam-624", reiser4_find_next_set_bit(bnode_working_data(bnode), end_offset, start_offset)
		       >= end_offset);
	}

	release_and_unlock_bnode(bnode);
}

#endif

/* conditional insertion of @node into atom's overwrite set  if it was not there */
static void
cond_add_to_overwrite_set (txn_atom * atom, jnode * node)
{
	assert("zam-546", atom != NULL);
	assert("zam-547", atom->stage == ASTAGE_PRE_COMMIT);
	assert("zam-548", node != NULL);

	LOCK_JNODE(node);

	if (node->atom == NULL) {
		JF_SET(node, JNODE_OVRWR);
		insert_into_atom_ovrwr_list(atom, node);
	} else {
		assert("zam-549", node->atom == atom);
	}

	UNLOCK_JNODE(node);
}

/* an actor which applies delete set to COMMIT bitmap pages and link modified
   pages in a single-linked list */
/* Audited by: green(2002.06.12) */
static int
apply_dset_to_commit_bmap(txn_atom * atom, const reiser4_block_nr * start, const reiser4_block_nr * len, void *data)
{

	bmap_nr_t bmap;
	bmap_off_t offset;
	int ret;

	long long *blocks_freed_p = data;

	struct bnode *bnode;

	struct super_block *sb = reiser4_get_current_sb();

	check_block_range(start, len);

	parse_blocknr(start, &bmap, &offset);

	/* FIXME-ZAM: we assume that all block ranges are allocated by this
	   bitmap-based allocator and each block range can't go over a zone of
	   responsibility of one bitmap block; same assumption is used in
	   other journal hooks in bitmap code. */
	bnode = get_bnode(sb, bmap);
	assert("zam-448", bnode != NULL);

	/* it is safe to unlock atom with is in ASTAGE_PRE_COMMIT */
	assert ("zam-767", atom->stage == ASTAGE_PRE_COMMIT);
	ret = load_and_lock_bnode(bnode);
	if (ret)
		return ret;

	/* put bnode into atom's overwrite set */
	cond_add_to_overwrite_set (atom, bnode->cjnode);

	data = bnode_commit_data(bnode);

	if (REISER4_DEBUG && *bnode_commit_crc(bnode) != adler32(bnode_commit_data(bnode), bmap_size(sb->s_blocksize)))
		warning("vpf-263", "Checksum for the bitmap block %llu is incorrect", bmap);

	if (len != NULL) {
		/* FIXME-ZAM: a check that all bits are set should be there */
		assert("zam-443", offset + *len <= bmap_bit_count(sb->s_blocksize));
		reiser4_clear_bits(data, offset, (bmap_off_t) (offset + *len));

		(*blocks_freed_p) += *len;
	} else {
		reiser4_clear_bit(offset, data);
		(*blocks_freed_p)++;
	}

	*bnode_commit_crc(bnode) = adler32(bnode_commit_data(bnode), bmap_size(sb->s_blocksize));

	release_and_unlock_bnode(bnode);

	return 0;
}

/* plugin->u.space_allocator.pre_commit_hook(). */
/* It just applies transaction changes to fs-wide COMMIT BITMAP, hoping the
   rest is done by transaction manager (allocate wandered locations for COMMIT
   BITMAP blocks, copy COMMIT BITMAP blocks data). */
/* Only one instance of this function can be running at one given time, because
   only one transaction can be committed a time, therefore it is safe to access
   some global variables without any locking */
void
pre_commit_hook_bitmap(void)
{
	struct super_block * super = reiser4_get_current_sb();
	txn_atom *atom;

	long long blocks_freed = 0;

	atom = get_current_atom_locked ();
	assert ("zam-876", atom->stage == ASTAGE_PRE_COMMIT);
	spin_unlock_atom(atom);

	{			/* scan atom's captured list and find all freshly allocated nodes,
				 * mark corresponded bits in COMMIT BITMAP as used */
		capture_list_head *head = &atom->clean_nodes;
		jnode *node = capture_list_front(head);

		while (!capture_list_end(head, node)) {
			/* we detect freshly allocated jnodes */
			if (JF_ISSET(node, JNODE_RELOC)) {
				bmap_nr_t bmap;

				bmap_off_t offset;
				struct bnode *bn;
				__u32 size = bmap_size(super->s_blocksize);
				char byte;

				assert("zam-559", !JF_ISSET(node, JNODE_OVRWR));
				assert("zam-460", !blocknr_is_fake(&node->blocknr));

				parse_blocknr(&node->blocknr, &bmap, &offset);
				bn = get_bnode(super, bmap);

				assert("vpf-276", offset / 8 < size);

				if (REISER4_DEBUG && *bnode_commit_crc(bn) != adler32(bnode_commit_data(bn), size))
					warning("vpf-262", "Checksum for the bitmap block %llu is incorrect", bmap);

				check_bnode_loaded(bn);
				load_and_lock_bnode(bn);

				byte = *(bnode_commit_data(bn) + offset / 8);
				reiser4_set_bit(offset, bnode_commit_data(bn));

				*bnode_commit_crc(bn) =
				    adler32_recalc(*bnode_commit_crc(bn), byte,
						    *(bnode_commit_data(bn) + offset / 8), size - offset / 8);

				release_and_unlock_bnode(bn);

				if (REISER4_DEBUG && *bnode_commit_crc(bn) != adler32(bnode_commit_data(bn), size))
					warning("vpf-275", "Checksum for the bitmap block %llu is incorrect", bmap);

				/* working of this depends on how it inserts
				   new j-node into clean list, because we are
				   scanning the same list now. It is OK, if
				   insertion is done to the list front */
				cond_add_to_overwrite_set (atom, bn->cjnode);
			}

			node = capture_list_next(node);
		}
	}

	blocknr_set_iterator(atom, &atom->delete_set, apply_dset_to_commit_bmap, &blocks_freed, 0);

	blocks_freed -= atom->nr_blocks_allocated;

	{
		reiser4_super_info_data *sbinfo;

		sbinfo = get_super_private(super);

		reiser4_spin_lock_sb(sbinfo);
		sbinfo->blocks_free_committed += blocks_freed;
		reiser4_spin_unlock_sb(sbinfo);
	}
}

/* plugin->u.space_allocator.init_allocator
    constructor of reiser4_space_allocator object. It is called on fs mount */
int
init_allocator_bitmap(reiser4_space_allocator * allocator, struct super_block *super, void *arg UNUSED_ARG)
{
	struct bitmap_allocator_data *data = NULL;
	bmap_nr_t bitmap_blocks_nr;
	bmap_nr_t i;

	assert("nikita-3039", schedulable());
	/* getting memory for bitmap allocator private data holder */
	data = reiser4_kmalloc(sizeof (struct bitmap_allocator_data), GFP_KERNEL);

	if (data == NULL)
		return RETERR(-ENOMEM);

	/* allocation and initialization for the array of bnodes */
	bitmap_blocks_nr = get_nr_bmap(super);

	/* FIXME-ZAM: it is not clear what to do with huge number of bitmaps
	   which is bigger than 2^32. Kmalloc is not possible and, probably,
	   another dynamic data structure should replace a static array of
	   bnodes. */
	data->bitmap = reiser4_kmalloc((size_t) (sizeof (struct bnode) * bitmap_blocks_nr), GFP_KERNEL);

	if (data->bitmap == NULL) {
		reiser4_kfree(data, (size_t) (sizeof (struct bnode) * bitmap_blocks_nr));
		return RETERR(-ENOMEM);
	}

	for (i = 0; i < bitmap_blocks_nr; i++)
		init_bnode(data->bitmap + i, super, i);

	allocator->u.generic = data;

#if REISER4_DEBUG
	get_super_private(super)->min_blocks_used += bitmap_blocks_nr;
#endif

	/* Load all bitmap blocks at mount time. */
	if (!test_bit(REISER4_DONT_LOAD_BITMAP, &get_super_private(super)->fs_flags)) {
		__u64 start_time, elapsed_time;
		struct bnode * bnode;
		int ret;

		printk(KERN_INFO "loading reiser4 bitmap...\n");
		start_time = jiffies;

		for (i = 0; i < bitmap_blocks_nr; i++) {
			bnode = data->bitmap + i;
			ret = load_and_lock_bnode(bnode);
			if (ret) {
				destroy_allocator_bitmap(allocator, super);
				return ret;
			}
			release_and_unlock_bnode(bnode);
		}

		elapsed_time = jiffies - start_time;
		printk(KERN_INFO "...done (%llu jiffies)\n", (unsigned long long)elapsed_time);
	}

	return 0;
}

/* plugin->u.space_allocator.destroy_allocator
   destructor. It is called on fs unmount */
int
destroy_allocator_bitmap(reiser4_space_allocator * allocator, struct super_block *super)
{
	bmap_nr_t bitmap_blocks_nr;
	bmap_nr_t i;

	struct bitmap_allocator_data *data = allocator->u.generic;

	assert("zam-414", data != NULL);
	assert("zam-376", data->bitmap != NULL);

	bitmap_blocks_nr = get_nr_bmap(super);

	for (i = 0; i < bitmap_blocks_nr; i++) {
		struct bnode *bnode = data->bitmap + i;

		down(&bnode->sema);

#if REISER4_DEBUG
		if (atomic_read(&bnode->loaded)) {
			jnode *wj = bnode->wjnode;
			jnode *cj = bnode->cjnode;

			assert("zam-480", jnode_page(cj) != NULL);
			assert("zam-633", jnode_page(wj) != NULL);

			assert("zam-634",
			       memcmp(jdata(wj), jdata(wj),
				      bmap_size(super->s_blocksize)) == 0);

		}
#endif
		done_bnode(bnode);
		up(&bnode->sema);
	}

	reiser4_kfree(data->bitmap, (size_t) (sizeof (struct bnode) * bitmap_blocks_nr));
	reiser4_kfree(data, sizeof (struct bitmap_allocator_data));

	allocator->u.generic = NULL;

	return 0;
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   scroll-step: 1
   End:
*/
