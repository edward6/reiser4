/* Copyright 2002, Hans Reiser */

/* A part of reiser4 block allocator. This code only allocates contiguous
 * chunks of blocks and does routine work for bitmap logging: prepare commit
 * bitmap blocks which are written to disk and make deleted/wandered blocks
 * available to allocator after an atom passes its commit/write-back stages.
 *  
 * A "smart" part of the block allocator is in block_alloc.[ch] files.
 */

#include "reiser4.h"
#include <asm/bitops.h>

/** find next zero bit in byte */
static inline int find_next_zero_bit_in_byte (unsigned int byte, int start)
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

static inline int reiser4_find_next_zero_bit (void * addr, int size, int start_offset)
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
	}

	while (++ byte_nr < max_byte_nr) {
		if (*(++p) != 0xFF) {
			return (byte_nr << 3) + find_next_zero_bit_in_byte(*p, 0);
		}
	}

	return size;
}

#endif

static inline int reiser4_find_next_set_bit (void * addr, int size, int start_offset)
{
	unsigned char * p = addr;
	int byte_nr = start_offset >> 3;
	int bit_nr  = start_offset & 0x7;
	int max_byte_nr = (size - 1) >> 3;

	assert ("zam-387", size != 0);

	if (bit_nr != 0) {
		int nr;

		nr = find_next_zero_bit_in_byte(~(*p), bit_nr);

		if (nr < 8) return (byte_nr << 3) + nr;
	}

	while (++ byte_nr < max_byte_nr) {
		if (*(++p) != 0) {
			return (byte_nr << 3) + find_next_zero_bit_in_byte(~(*p), 0);
		}
	}

	return size;
}

static inline void reiser4_set_bits (char * addr, int start, int end)
{
	int first_byte;
	int last_byte;
	int i;

	assert ("zam-386", start < end);

	first_byte = start >> 3;
	last_byte = (end - 1) >> 3;

	for (i = first_byte + 1; i < last_byte; i++) 
		addr[i] = 0xFF;

	{
		unsigned char first_byte_mask = 0xFF;
		unsigned char last_byte_mask = 0xFF;

		first_byte_mask >>= (start & 0x7);
		last_byte_mask <<= (8 - ((end - 1) & 0x7));
	
		if (first_byte == last_byte) {
			addr[first_byte] |= (first_byte_mask & last_byte_mask);
		} else {
			addr[first_byte] |= first_byte_mask;
			addr[last_byte]  |= last_byte_mask;
		}
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
static void parse_blocknr (block_nr block, int *bmap, int *offset)
{
	struct super_block * super = reiser4_get_current_context()->super;

	*bmap   = block / super->s_blocksize;
	*offset = block % super->s_blocksize;
} 

/* construct a fake block number for shadow bitmap (WORKING BITMAP) block */
block_nr get_working_bitmap_blocknr (int bmap)
{
	block_nr block = bmap;
	return block | 0xF0000000LL;
}

/** Load node at given blocknr, update given pointer. This function should be
 * called under tree lock held */
static inline int load_bnode_half (znode ** node_pp, block_nr block)
{
	reiser4_disk_addr addr;
	znode * node;
	int ret;

	spin_unlock_tree (current_tree);

	addr.blk = block;
	node = zget(current_tree, &addr, NULL, 0, GFP_KERNEL);

	if (IS_ERR(node)) {
		spin_lock_tree(current_tree);

		return PTR_ERR(node);
	}

	ret = zload(node);

	if (ret) { 
		zput(node);
		spin_lock_tree(current_tree);
		return ret;
	}

	spin_lock_tree(current_tree);

	if (*node_pp == NULL) {
		*node_pp = node;
	} else {
		spin_unlock_tree(current_tree);

		zrelse(node, 1);
		zput(node);

		spin_lock_tree(current_tree);

		/* return value above zero indicates that another thread has
		 * done our work */
		return 1;
	}

	return 0;
}

/* load bitmap blocks "on-demand" */
static int load_bnode (struct reiser4_bnode * bnode)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private(); 
	struct super_block * super = reiser4_get_current_context()->super;
	int ret = 0;
	int bmap_nr = info_data->bitmap - bnode;

	spin_lock_tree(current_tree);

	if (bnode->commit == NULL) {
		ret = load_bnode_half(&bnode->commit, reiser4_get_bitmap_blocknr(super, bmap_nr));

		if (ret < 0) goto out;
	}

	if (bnode->working == NULL) {
		ret = load_bnode_half(&bnode->working, get_working_bitmap_blocknr(bmap_nr));

		if (ret < 0) goto out;

		if (ret == 0) {
			/* commit bitmap is initialized by on-disk bitmap
			 * content (working bitmap in this context) */
			xmemcpy(bnode->working->data,
			       bnode->commit->data,
			       super->s_blocksize);
		}

		ret = 0;
	}
	
 out:
	spin_unlock_tree(current_tree);

	return ret;
}

#if 0

static void release_bnode(struct reiser4_bnode * bnode)
{
	assert("zam-362", bnode->working != NULL);
	assert("zam-363", bnode->commit != NULL);

	zrelse(bnode->working, 1);
	zrelse(bnode->commit, 1);

	zput(bnode->working);
	zput(bnode->commit);
}

#endif

/** find out real block number for given reiser4 node formatted or
 * unformatted */
static inline int reconstruct_blocknr (jnode * node, reiser4_disk_addr *da)
{
	if (!JF_ISSET(node, ZNODE_UNFORMATTED)) 
		*da = JZNODE(node)->blocknr;

	da->blk = 0;
	
	/* ??? */
	return 0;
}

/** find out wandered block number */
static inline int reconstruct_wandered_blocknr (jnode * node, reiser4_disk_addr * da)
{
	da->blk = 0;
	/* ??? */
	return 0;
}

/** This function does all block allocation work but only for one bitmap
 * block.*/
/* FIXME_ZAM: It does not allow us to allocate block ranges across bitmap
 * block responsibility zone boundaries. This had no sense in v3.6 but may
 * have it in v4.x */

static int search_one_bitmap (int bmap, int *offset, int max_offset, 
			      int min_len, int max_len)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private(); 
	struct reiser4_bnode * bnode = &info_data->bitmap[bmap];

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

	spin_lock_znode(bnode->working);

	start = *offset;

	while (start + min_len < max_offset) {

		start = reiser4_find_next_zero_bit((long*)bnode->working->data, max_offset, start);

		if (start >= max_offset) break;

		search_end = ((start + max_len) > max_offset) ? max_offset : start + max_len;
		end = reiser4_find_next_set_bit((long*)bnode->working->data, search_end, start);

		if (end >= start + min_len) {
			ret = end - start;
			*offset = start;
			reiser4_set_bits(bnode->working->data, start, end);

			break;
		}

		start = end + 1;
	}

	spin_unlock_znode(bnode->working);
	/*release_bnode(bnode);*/

	return ret;
}

/** allocate contiguous range of blocks in bitmap */
int reiser4_bitmap_alloc (block_nr *start, block_nr end, int min_len, int max_len)
{
	int bmap, offset;
	int end_bmap, end_offset;
	int len;

	struct super_block * super = reiser4_get_current_context()->super;
	int max_offset = super->s_blocksize;

	parse_blocknr(*start, &bmap, &offset);
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


TS_LIST_DEFINE(capture,jnode,capture_link);

#define WALK_ATOM_VARS \
        int    h; \
        jnode *node;

#define WALK_ATOM                                             \
        for (h = 0; h < REAL_MAX_ZTREE_HEIGHT; h ++)          \
        for (node = capture_list_front(&atom->dirty_nodes[h]); \
             capture_list_end(&atom->dirty_nodes[h], node);          \
             node = capture_list_next(node))

/*
 * These functions are hooks from the journal code to manipulate COMMIT BITMAP
 * and WORKING BITMAP objects.
 */

/** It just applies transaction changes to fs-wide COMMIT BITMAP, hoping the
 * rest is done by transaction manager (allocate wandered locations for COMMIT
 * BITMAP blocks, copy COMMIT BITMAP blocks data). */
int reiser4_bitmap_prepare_commit (txn_atom * atom)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private(); 
	int ret = 0;
	WALK_ATOM_VARS;

	spin_lock_atom(atom);

	WALK_ATOM {
		reiser4_disk_addr da;
		int bmap, offset;

		struct reiser4_bnode * bnode;

		if (! JF_ISSET(node, ZNODE_DELETESET | ZNODE_ALLOC))
			continue;

		ret = reconstruct_blocknr(node, &da);

		if (ret) break;

		parse_blocknr(da.blk, &bmap, &offset);

		/* assert("zam-370", !reiser4_blocknr_is_fake(block));*/

		bnode = &info_data->bitmap[bmap];

		if (node->atom == NULL) { 
			/* capture a commit bitmap block */

			/* Is there an interface to node capture another that
			   reiser4_lock_znode() ? */
			reiser4_lock_handle lh;

			reiser4_init_lh(&lh);

			ret = reiser4_lock_znode(&lh, bnode->commit, 
						 ZNODE_WRITE_LOCK, 
						 ZNODE_LOCK_NONBLOCK | ZNODE_LOCK_HIPRI);

			/* there should be no problem with adding of bitmap
			 * block to the transaction */
			assert ("zam-371", ret == 0);

			if (ret) break;

			reiser4_unlock_znode(&lh);

			spin_lock_atom(atom);
		}

		/* apply DELETED SET */
		if (JF_ISSET(node, ZNODE_DELETESET))
			reiser4_clear_bit(offset, bnode->commit->data);
		/* set bits for freshly allocated nodes */
		if (JF_ISSET(node, ZNODE_ALLOC))
			reiser4_set_bit(offset, bnode->commit->data);
	}

	spin_unlock_atom(atom);

	return ret;
}

/** called after transaction commit, apply DELETE SET to WORKING BITMAP */
int reiser4_bitmap_done_commit (txn_atom * atom) {
	reiser4_super_info_data * info_data = reiser4_get_current_super_private(); 
	int ret = 0;
	WALK_ATOM_VARS;

	assert ("zam-382", info_data->bitmap != NULL);

	spin_lock_atom (atom);
	WALK_ATOM {
		reiser4_disk_addr da;
		int bmap, offset;
		struct reiser4_bnode * bnode;

		if (!JF_SET(node, ZNODE_DELETESET))
			continue;

		ret = reconstruct_blocknr(node, &da);

		if (ret) break;

		parse_blocknr(da.blk, &bmap, &offset);

		bnode = (struct reiser4_bnode*)(info_data->bitmap) + bmap;

		assert ("zam-383", bnode->working != NULL);
		assert ("zam-384", bnode->commit != NULL);

		reiser4_clear_bit(offset, bnode->working->data);
	}
	spin_unlock_atom (atom);

	return ret;
}

/** This function is called after write-back (writing blocks from OVERWRITE
 * SET to real locations) transaction stage completes. (clear WANDERED SET in
 * WORKING BITMAP) */
int reiser4_bitmap_done_writeback (txn_atom * atom)
{
	reiser4_super_info_data * info_data = reiser4_get_current_super_private();
	int ret = 0;
	WALK_ATOM_VARS;

	assert ("zam-380", info_data->bitmap != NULL);

	spin_lock_atom(atom);

	/* FIXME_ZAM: we do not know what dynamic objects will be used to keep
	 * informations about wandered blocks locations. This alg. is done
	 * under an assumption that those objects are jnodes. */
	WALK_ATOM {
		int bmap, offset;
		struct reiser4_bnode * bnode;
		reiser4_disk_addr wandered_da;

		if (!JF_ISSET(node, ZNODE_WANDER))
			continue;

		/* FIXME-ZAM: there should be way to
		 * reconstruct wandered blocknr from jnode's
		 * page offset or something else.*/;
		ret = reconstruct_wandered_blocknr(node, &wandered_da);

		parse_blocknr(wandered_da.blk, &bmap, &offset);
		bnode = (struct reiser4_bnode*)(info_data->bitmap) + bmap;

		assert ("zam-379", bnode->commit != NULL);
		assert ("zam-381", bnode->working != NULL);

		reiser4_clear_bit(offset, bnode->working->data);
	}

	spin_unlock_atom(atom);

	return ret;
}

/** constructor and destructor called on fs mount/unmount */
/** bitmap structure initialization */
int reiser4_init_bitmap (struct super_block * super)
{
	reiser4_super_info_data * info_data = reiser4_get_super_private(super); 
	int bitmap_blocks_nr;

	bitmap_blocks_nr = reiser4_get_nr_bmap(super); 

	info_data->bitmap = reiser4_kmalloc (sizeof(struct reiser4_bnode) * bitmap_blocks_nr, 
					GFP_KERNEL);
	if (info_data->bitmap == NULL)
		return -ENOMEM;

	/* at fs mount time, no one bitmap block is loaded into memory */
	xmemset(info_data->bitmap, 0, sizeof(struct reiser4_bnode) * bitmap_blocks_nr);

	return 0;
}

/** bitmap structure proper destroying */
void reiser4_done_bitmap (struct super_block * super)
{
	reiser4_super_info_data * info_data = reiser4_get_super_private(super); 
	int bitmap_blocks_nr;
	int i;

	assert ("zam-376", info_data->bitmap != NULL);

	bitmap_blocks_nr = reiser4_get_nr_bmap(super);

	for (i = 0; i < bitmap_blocks_nr; i ++) {
		struct reiser4_bnode * bnode 
			= (struct reiser4_bnode *)(info_data->bitmap) + i;

		assert ("zam-378", equi(bnode->working == NULL, bnode->commit == NULL));

		if (bnode->working != NULL) {
			zrelse(bnode->working, 1);
			zrelse(bnode->commit, 1);

			zput(bnode->working);
			zput(bnode->commit);
			
			zdestroy(bnode->commit);
			zdestroy(bnode->working);
		}
	}

	reiser4_kfree(info_data->bitmap, sizeof(struct reiser4_bnode) * bitmap_blocks_nr);
	info_data->bitmap = NULL;
}

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * End:
 */
