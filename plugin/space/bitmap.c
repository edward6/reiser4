/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"
#include "bitmap.h"
/*
 * this file contains:
 * - bitmap based implementation of space allocation plugin
 * - all the helper functions like set bit, find_first_zero_bit, etc
 */

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

static inline void reiser4_set_bit (int nr, void * addr)
{
	unsigned char * base = (char*)addr + ((unsigned int)nr  >> 3);
	*base |= (1 << (nr & 0x7));
}

static inline void reiser4_clear_bit (int nr, void * addr)
{
	unsigned char * base = (char*)addr + ((unsigned int)nr >> 3);
	*base &= ~(1 << (nr & 0x7));
}

static int reiser4_find_next_zero_bit (void * addr, int size, int start_offset)
{
	unsigned char * p = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr  = start_offset & 0x7;
	int max_byte_nr = (size - 1) >> 3;

	assert ("zam-388", size != 0);

	if (bit_nr != 0) {
		int nr;

		nr = find_next_zero_bit_in_byte(*p, bit_nr);

		if (nr < 8) return (byte_nr << 3) + nr;

		++ byte_nr;
	}

	while (byte_nr < max_byte_nr) {
		if (*(++p) != 0xFF) {
			return (byte_nr << 3) + find_next_zero_bit_in_byte(*p, 0);
		}

		++ byte_nr;
	}

	return size;
}

#endif

static int reiser4_find_next_set_bit (void * addr, int size, int start_offset)
{
	unsigned char * p = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr  = start_offset & 0x7;
	int max_byte_nr = (size - 1) >> 3;

	assert ("zam-387", size != 0);

	if (bit_nr != 0) {
		int nr;

		nr = find_next_zero_bit_in_byte(~ (unsigned int) (*p), bit_nr);

		if (nr < 8) return (byte_nr << 3) + nr;

		++ byte_nr;
	}

	while (byte_nr < max_byte_nr) {
		if (*(++p) != 0) {
			return (byte_nr << 3) + find_next_zero_bit_in_byte(~ (unsigned int) (*p), 0);
		}

		++ byte_nr;
	}

	return size;
}

static void reiser4_clear_bits (char * addr, int start, int end)
{
	int first_byte;
	int last_byte;

	unsigned char first_byte_mask = 0xFF;
	unsigned char last_byte_mask  = 0xFF;

	assert ("zam-410", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	if (last_byte > first_byte + 1) xmemset (addr + start + 1, 0, (unsigned)(last_byte - first_byte - 1));

	first_byte_mask >>= 8 - (start & 0x7);
	last_byte_mask  <<= (end - 1) & 0x7;

	if (first_byte == last_byte) {
		addr[first_byte] &= (first_byte_mask | last_byte_mask);
	} else {
		addr[first_byte] &= first_byte_mask;
		addr[last_byte]  &= last_byte_mask;
	}
}

static void reiser4_set_bits (char * addr, int start, int end)
{
	int first_byte;
	int last_byte;

	unsigned char first_byte_mask = 0xFF;
	unsigned char last_byte_mask = 0xFF;

	assert ("zam-386", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	if (last_byte > first_byte + 1) xmemset (addr + start + 1, 0xFF, 
						 (unsigned)(last_byte - first_byte - 1));

	first_byte_mask <<= 8 - (start & 0x7);
	last_byte_mask  >>= (end - 1) & 0x7;

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

/** calculate bitmap block number and offset within that bitmap block */
static void parse_blocknr (const reiser4_block_nr *block, int *bmap, int *offset)
{
	struct super_block * super = get_current_context()->super;

	*bmap   = *block / super->s_blocksize;
	*offset = *block % super->s_blocksize;
} 

/** A number of bitmap blocks for given fs. This number can be stored on disk
 * or calculated on fly; it depends on disk format.
 * FIXME-VS: number of blocks in a filesystem is taken from reiser4
 * super private data */
static int get_nr_bmap (struct super_block * super)
{
	assert ("zam-393", reiser4_block_count (super) != 0);

	return (reiser4_block_count (super) - 1) / (reiser4_blksize (super) * 8) + 1;
}

/* bnode structure initialization */
static void init_bnode (struct reiser4_bnode * bnode)
{
	bnode -> wpage = NULL;
	bnode -> cpage = NULL;

	spin_lock_init (& bnode -> guard); 
}

/** return a physical disk address for logical bitmap number @bmap */
/* FIXME-VS: this is somehow related to disk layout? */
#define REISER4_FIRST_BITMAP_BLOCK 100
void get_bitmap_blocknr (struct super_block * super, int bmap, reiser4_block_nr *bnr)
{

	assert ("zam-389", bmap >= 0);
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
int bitmap_init_allocator (reiser4_space_allocator * allocator,
			   struct super_block * super, void * arg UNUSED_ARG)
{
	struct bitmap_allocator_data * data = NULL;
	int bitmap_blocks_nr;
	int i;

	/* getting memory for bitmap allocator private data holder */
	data = reiser4_kmalloc (sizeof (struct bitmap_allocator_data), GFP_KERNEL);

	if (data == NULL) return -ENOMEM;

	/* allocation and initialization for the array of bnodes */
	bitmap_blocks_nr = get_nr_bmap(super); 

	data->bitmap = reiser4_kmalloc (
		sizeof(struct reiser4_bnode) * bitmap_blocks_nr, GFP_KERNEL);

	if (data->bitmap ==  NULL) {
		reiser4_kfree (data, sizeof(struct reiser4_bnode) * bitmap_blocks_nr);
		return -ENOMEM;
	}

	for (i = 0; i < bitmap_blocks_nr; i++) init_bnode(data -> bitmap + i);

	allocator->u.generic = data;

	return 0;
}


/* plugin->u.space_allocator.destroy_allocator
 * destructor. It is called on fs unmount */
int bitmap_destroy_allocator (reiser4_space_allocator * allocator,
			      struct super_block * super)
{
	int bitmap_blocks_nr;
	int i;

	struct bitmap_allocator_data * data = allocator->u.generic;

	assert ("zam-414", data != NULL);
	assert ("zam-376", data -> bitmap != NULL);

	bitmap_blocks_nr = get_nr_bmap(super);

	for (i = 0; i < bitmap_blocks_nr; i ++) {
		struct reiser4_bnode * bnode = data -> bitmap + i;

		assert ("zam-378", equi(bnode -> wpage == NULL, bnode -> cpage == NULL));

		/* FIXME: we need to release all pinned buffers/pages, it is
		 * not done because release_node isn't ready */
	}

	reiser4_kfree (data->bitmap,
		       sizeof(struct reiser4_bnode) * bitmap_blocks_nr);
	reiser4_kfree (data, sizeof (struct bitmap_allocator_data));
	allocator->u.generic = NULL;
	return 0;
}

/* construct a fake block number for shadow bitmap (WORKING BITMAP) block */
void get_working_bitmap_blocknr (int bmap, reiser4_block_nr *bnr)
{
	*bnr = (reiser4_block_nr) bmap | REISER4_BITMAP_BLOCKS_BIT_MASK;
}

/** Load node at given blocknr, update given pointer. This function should be
 * called under tree lock held */
static int load_bnode_half (struct reiser4_bnode * bnode, char ** data, reiser4_block_nr *block)
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

	if (ret) spin_lock_bnode(bnode);

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
static int load_bnode (struct reiser4_bnode * bnode)
{
	struct super_block * super = get_current_context()->super;
	int ret = 0;
	int bmap_nr = bnode - get_barray(super);
	reiser4_block_nr bnr;

	spin_lock_bnode(bnode);

	if (bnode->cpage == NULL) {
		get_bitmap_blocknr(super, bmap_nr, & bnr);
		ret = load_bnode_half(bnode, & bnode -> cpage, & bnr);

		if (ret < 0) goto out;
	}

	if (bnode->wpage == NULL) {
		get_working_bitmap_blocknr(bmap_nr, &bnr);
		ret = load_bnode_half(bnode, & bnode -> wpage, & bnr);

		if (ret < 0) goto out;

		if (ret == 0) {
			/* commit bitmap is initialized by on-disk bitmap
			 * content (working bitmap in this context) */
			xmemcpy(bnode -> wpage, bnode->cpage, super->s_blocksize);
		}

		ret = 0;
	}
	
 out:
	spin_unlock_bnode(bnode);

	return ret;
}

/** This function does all block allocation work but only for one bitmap
 * block.*/
/* FIXME_ZAM: It does not allow us to allocate block ranges across bitmap
 * block responsibility zone boundaries. This had no sense in v3.6 but may
 * have it in v4.x */

static int search_one_bitmap (int bmap, int *offset, int max_offset, 
			      int min_len, int max_len)
{
	struct super_block * super = get_current_context() -> super;
	struct reiser4_bnode * bnode = get_bnode (super, bmap);

	int search_end;
	int start;
	int end;

	int ret = 0;

	assert("zam-364", min_len > 0);
	assert("zam-365", max_len >= min_len);
	assert("zam-366", *offset < max_offset);

	ret = load_bnode (bnode);
	if (ret) return ret;
	/* ret = 0; */

	spin_lock_bnode(bnode);

	start = *offset;

	while (start + min_len < max_offset) {

		start = reiser4_find_next_zero_bit((long*) bnode -> wpage, max_offset, start);

		if (start >= max_offset) break;

		search_end = ((start + max_len) > max_offset) ? max_offset : start + max_len;
		end = reiser4_find_next_set_bit((long*) bnode->wpage, search_end, start);

		if (end >= start + min_len) {
			ret = end - start;
			*offset = start;
			reiser4_set_bits(bnode->wpage, start, end);

			break;
		}

		start = end + 1;
	}

	spin_unlock_bnode(bnode);
	/*release_bnode(bnode);*/

	return ret;
}

/** allocate contiguous range of blocks in bitmap */
int bitmap_alloc (reiser4_block_nr *start, const reiser4_block_nr *end, int min_len, int max_len)
{
	int bmap, offset;
	int end_bmap, end_offset;
	int len;

	struct super_block * super = get_current_context()->super;
	int max_offset = super->s_blocksize * 8;

	parse_blocknr(start, &bmap, &offset);
	parse_blocknr(end, &end_bmap, &end_offset);

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

#define WALK_ATOM_VARS \
        int    h; \
        jnode *node;

#define WALK_ATOM                                             \
        for (h = 0; h < REAL_MAX_ZTREE_HEIGHT; h ++)          \
        for (node = capture_list_front(&atom->dirty_nodes[h]); \
             capture_list_end(&atom->dirty_nodes[h], node);          \
             node = capture_list_next(node))

/* plugin->u.space_allocator.alloc_blocks */
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

	/* first, we use *(@start) as a search start and search from this
	 * @start to the end of the disk */

	search_start = hint->blk;
	search_end   = reiser4_block_count(super);

	actual_len = bitmap_alloc (&search_start, &search_end, 1, needed);

	if (actual_len != 0) goto out;

	/* next step is a scanning from 0 to search_start */
	if (search_start != 0) {
		search_end = search_start;
		search_start = 0;
		actual_len = bitmap_alloc (&search_start, &search_end, 1, needed);
	}

 out:
	if (actual_len <= 0) return actual_len;

	*len = actual_len;
	*start = search_start;

	return 0;
}

/* plugin->u.space_allocator.dealloc_block */
void bitmap_dealloc_blocks (reiser4_space_allocator * allocator UNUSED_ARG,
			    reiser4_block_nr start UNUSED_ARG,
			    reiser4_block_nr len UNUSED_ARG)
{
	struct super_block      * super = get_current_context()->super;
	reiser4_super_info_data * info_data = get_current_super_private();
	jnode *node;
	txn_atom * atom = node->atom;

	/*
	 * FIXME-ZAM: please fix it to deal with block number instead of jnode
	 * and with not only one node
	 */
	node = 0;
 	assert ("zam-399", node != NULL);

	spin_lock_jnode(node);

	atom = node->atom;
	assert ("zam-400", atom != NULL);

	if (blocknr_is_fake(&node->blocknr)) {
		/* deallocation of such not-yet-mapped-to-disk nodes does not
		 * cause putting them to atom's DELETED SET, but fs free block
		 * counter is changed immediately */

		spin_lock (&info_data->guard);

		reiser4_inc_free_blocks (super);

		assert ("zam-405", (reiser4_free_blocks (super) >
				    reiser4_data_blocks (super)));

		spin_unlock (&info_data->guard);
	}

#if 0
	/* with mapped nodes we mark them as DELETED and put them to
	 * atom->clean_nodes list. */

	JF_SET(node, ZNODE_DELETED);
#endif
	/* move jnode to a atom's clean list */
	capture_list_remove (node);
	capture_list_push_front(&atom->clean_nodes, node);

	/* we do not change free blocks count until transaction commits */

	spin_unlock_jnode(node);
}

/*
 * These functions are hooks from the journal code to manipulate COMMIT BITMAP
 * and WORKING BITMAP objects.
 */

/** It just applies transaction changes to fs-wide COMMIT BITMAP, hoping the
 * rest is done by transaction manager (allocate wandered locations for COMMIT
 * BITMAP blocks, copy COMMIT BITMAP blocks data). */
int bitmap_pre_commit_hook (txn_atom * atom)
{
	struct super_block * super          = reiser4_get_current_sb ();
	reiser4_super_info_data * info_data = get_super_private (super);
	reiser4_space_allocator * allocator = &info_data->space_allocator;
	int ret = 0;
	WALK_ATOM_VARS;

	spin_lock_atom(atom);

	WALK_ATOM {
		int bmap, offset;

		struct reiser4_bnode * bnode;

		if (!jnode_is_in_deleteset(node) && 
		    !JF_ISSET(node, ZNODE_ALLOC))
			continue;

		parse_blocknr(& node->blocknr, &bmap, &offset);

		assert("zam-370", !blocknr_is_fake(&node->blocknr));

		bnode = get_bnode(super, bmap);

		if (node->atom == NULL) { 
			/* capture a commit bitmap block */
			jnode * jnode;

		}

		/* apply DELETED SET */
		if (jnode_is_in_deleteset(node))
			reiser4_clear_bit(offset, bnode->cpage);

#if 0
		/* adjust blocks_free_committed counter -- a free blocks
		 * counter we write to disk */
		if (JF_ISSET(node, ZNODE_DELETED))
			reiser4_inc_free_committed_blocks (super);

		if (JF_ISSET(node, ZNODE_ALLOC)) {
			/* set bits for freshly allocated nodes */
			reiser4_set_bit(offset, bnode->cpage);
			/* count block which are allocated in the transaction, */
			reiser4_dec_free_committed_blocks (super);
		}
#endif
	}

	spin_unlock_atom(atom);

	return ret;
}

/** called after transaction commit, apply DELETE SET to WORKING BITMAP */
int bitmap_post_commit_hook (txn_atom * atom) {
	struct super_block      * super     = reiser4_get_current_sb ();
	reiser4_super_info_data * info_data = get_super_private (super); 
	reiser4_space_allocator * allocator = &info_data->space_allocator;
	int ret = 0;
	WALK_ATOM_VARS;

	assert ("zam-382", allocator->u.generic != NULL);
	assert ("zam-418", get_barray(super) != NULL);

	spin_lock_atom (atom);

	WALK_ATOM {
		int bmap, offset;
		struct reiser4_bnode * bnode;

		/* At this moment after successful commit we replay previously
		 * recorded in atom's deleted_nodes list changes to working
		 * bitmap and working free blocks counter ... */

#if 0
		/* ... count all blocks which are freed in a "working" free
		 * block counter */
		if (JF_ISSET(node, ZNODE_DELETED)) {
			spin_lock (&info_data->guard);
			
			reiser4_inc_free_blocks (super);
			spin_lock (&info_data->guard);
		}
#endif
		if (! jnode_is_in_deleteset(node))
			continue;

		/* ... apply DELETED_SET to the WORKING bitmap */
		assert ("zam-403", !blocknr_is_fake(&node->blocknr));

		parse_blocknr(& node->blocknr, &bmap, &offset);

		bnode = get_bnode (super, bmap);

		assert ("zam-383", bnode->wpage != NULL);
		assert ("zam-384", bnode->cpage != NULL);

		reiser4_clear_bit(offset, bnode->wpage);
	}

	spin_unlock_atom (atom);

	/* FIXME_ZAM: I think jnodes for DELETED_SET should disappear at this
	 * moment */

	return ret;
}

/** This function is called after write-back (writing blocks from OVERWRITE
 * SET to real locations) transaction stage completes. (clear WANDERED SET in
 * WORKING BITMAP) */
int bitmap_post_write_back_hook (txn_atom * atom)
{
	struct super_block      * super     = reiser4_get_current_sb ();
	reiser4_super_info_data * info_data = get_super_private (super);
	reiser4_space_allocator * allocator =  &info_data->space_allocator;
	int ret = 0;
	WALK_ATOM_VARS;

	assert ("zam-380", allocator->u.generic != NULL);
	assert ("zam-417", get_barray(super) != NULL);

	spin_lock_atom(atom);

	/* FIXME_ZAM: we do not know what dynamic objects will be used to keep
	 * informations about wandered blocks locations. This alg. is done
	 * under an assumption that those objects are jnodes. */
	WALK_ATOM {
		int bmap, offset;
		struct reiser4_bnode * bnode;

		if (!JF_ISSET(node, ZNODE_WANDER))
			continue;

		assert ("zam-404", !blocknr_is_fake(&node->blocknr));

		parse_blocknr(& node->blocknr, &bmap, &offset);
		bnode = get_bnode(super, bmap);

		assert ("zam-379", bnode->cpage != NULL);
		assert ("zam-381", bnode->wpage != NULL);

		reiser4_clear_bit(offset, bnode->wpage);

		spin_lock (&info_data->guard);
		reiser4_inc_free_blocks (super);
		spin_unlock (&info_data->guard);
	}

	spin_unlock_atom(atom);

	return ret;
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
