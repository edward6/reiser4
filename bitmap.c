/* Copyright 2002, Hans Reiser */

/*
 *
 */

#include "reiser4.h"

/* a disk format depended function which returns a physical disk address for
   logical bitmap number @bmap */
extern block_nr reiser4_get_bmap_blocknr (int bmap);

/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. We use them for now. */
struct reiser4_bnode {
	znode * working;	/* working bitmap block */
	znode * commit;		/* commit bitmap block */
};

/** calculate bitmap block number and offset within that bitmap block */
static void parse_blocknr (block_nr block, int *bmap, int *offset)
{
	struct super_block * super = reiser4_get_current_context()->super;

	*bmap   = block / super->s_blocksize;
	*offset = block % super->s_blocksize;
} 

/* construct a fake block number for shadow bitmap (COMMIT BITMAP) block */
block_nr get_commit_bitmap_blocknr (int bmap)
{
	block_nr block = bmap;
	return bmap | 0xF0000000LL;
}

/* FIXME_ZAM: bitmap dynamic loading/unloading needs a support from znode
 * destroying function which should set corresponding
 * sb->u.reiser4_i.bitmap[bmap].wb (cb) pointer to zero */

static int load_bmap_node (struct reiser4_bnode * bnode)
{
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 

	spin_lock_tree(current_tree);

	if (bnode->working == NULL) {
		znode * node;
		reiser4_disk_addr addr;

		spin_unlock_tree(current_tree);

		addr.blk = reiser4_get_bitmap_blocknr(bmap);
		node = zget(current_tree, &addr, NULL, 0, GFP_KERNEL);
		if (ERR(node)) return PTR_ERR(node);

		spin_lock_tree(current_tree);

		if (bnode->working) bnode->working = node;
		else           zput(node);
	} else {
		zref (bnode->working);
	}

	if (bnode->commit == NULL) {

		znode * node;
		reiser4_disk_addr addr;

		spin_unlock_tree(current_tree);

		addr.blk = get_commit_bitmap_blocknr(info->bitmap - bnode);
		node = zget(current_tree, &addr, NULL, 0, GFP_KERNEL);
		if (ERR(node)) return PTR_ERR(node);

		spin_lock_tree(current_tree);

		if (bnode->commit) bnode->commit = node;
		else           zput(node);
	} else {
		zref(bnode->commit);
	}
	

	spin_unlock_tree(current_tree);

	ret = zload (bnode->working);
	if (ret) return ret;

	ret = zload (bnode->commit);
	if (ret) return ret;

	return 0;
}

static void relse_bitmap_node(struct reiser4_bnode * bnode)
{
	assert("zam-362", bnode->working != NULL);
	assert("zam-363", bnode->commit != NULL);

	zrelse(bnode->working);
	zrelse(bnode->commit);

	zput(bnode-wb);
	zput(bnode->commit);
}

static void clear_bits (char * data, int start, int end)
{
	int first_byte = start / 8;
	int last_byte = (end - 1) / 8;

	assert ("zam-367", end >= start);
#if 0
	if (first_byte + 1 < last_byte - 1)
		memset(data + first_byte + 1, 0, last_byte - first_byte - 2);
#endif
	
	
}
static void set_bits (char * data, int start, int end)
{
	assert ("zam-368", end >= start);
}

/** This function does all block allocation work but only for one bitmap
 * block.*/
/* FIXME_ZAM: It does not allow us to allocate block ranges across bitmap
 * block responsibility zone boundaries. This had no sense in v3.6 but may
 * have it in v4.x */

static int search_one_bitmap (int bmap, int *offset, int max_offset, 
			      int min_len, int max_len)
{
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	struct reiser4_bitmap * bnode = &info->bitmap[bmap];

	int search_end;
	int start;
	int end;

	int ret;

	assert("zam-364", min_len > 0);
	assert("zam-365", max_len >= min_len);
	assert("zam-366", offset < max_offset);

	ret = load_bitmap_node (bnode);
	if (ret) return ret;
	/* ret = 0; */

	spin_lock_znode(bnode->working);

	start = *offset;

	if (start + min_len >= max_offset) goto out;

	start = find_first_zero_bit(bnode->data, start, max_offset);

	if (start >= max_offset) goto out;

	search_end = ((start + max_len) > max_offset) ? max_offset : start + max_len;
	end = find_first_nonzero_bit(bnode->data, start, search_end);

	if (end < start + min_len) goto out;

	ret = end - start;
	*offset = start;
	set_bits(bnode->working->data, start, end);

out:
	spin_unlock_znode(bnode->working);
	release_bitmap_node(bnode);
	return ret;
}

/** clear bits in bitmap(s) (working, commit or both) */
static int bitmap_clear (block_nr start, int len, int bitmap)
{
	
}

/** set bits in bitmap(s) */
static int bitmap_set (block_nr start, int len, int bitmap)
{

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

#define WALK_ATOM_VARS \
        int    h; \
        jnode *node;

#define WALK_ATOM \
        for (h = 0; h < REISER4_MAX_ZTREE_HEIGHT; h ++)       \
        for (node = capture_list_front(&atom->dirty_nodes[h]; \
             capture_list_end(&atom->dirty_nodes[h];          \
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
	int nr_bitmap_modified;
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	WALK_ATOM_VARS;

	spin_lock_atom(atom);

	WALK_ATOM {
		block_nr block;
		int bmap, offset;
		reiser4_bnode * bnode;

		if (! JF_ISSET(node, ZNODE_DELETESET | ZNODE_ALLOC))
			continue;

		block =  = reconstruct_blocknr(node);
		parse_blocknr(block, &bmap, &offset);

		assert("zam-370", !reiser4_blocknr_is_fake(block));

		bnode = &info->bitmap[bmap];

		if (node->atom == NULL) { 
			/* capture a commit bitmap block */

			/* Is there an interface to node capture another that
			   reiser4_lock_znode() ? */
			reiser4_lock_handle lh;

			reiser4_init_lh();

			ret = reiser4_lock_znode(&lh, bnode->commit, 
						 ZNODE_WRITE_LOCK, 
						 ZNODE_LOCK_NONBLOCK | ZNODE_LOCK_HIPRI);

			/* there should be no problem with adding of bitmap
			 * block to the transaction */
			assert ("zam-371", ret == 0);

			reiser4_unlock_znode(&lh);

			spin_lock_atom(atom);
		}

		/* apply DELETED SET */
		if (JF_ISSET(node, ZNODE_DELETESET))
			clear_le_bit(offset, bnode->commit->data);
		/* set bits for freshly allocated nodes */
		if (JF_ISSET(node, ZNODE_ALLOC))
			set_le_bit(offset, bnode->commit->data);
	}

	spin_unlock_atom(atom);

	return 0;
}

/** This function is called after write-back (writing blocks from OVERWRITE
 * SET to real locations) transaction stage completes. (clear WANDERED SET in
 * WORKING BITMAP) */
int reiser4_bitmap_done_flush ()
{
	reiser4_super_info_data * info = reiser4_get_current_super_private();
	WALK_ATOM_VARS;

	spin_lock_atom(atom);

	WALK_ATOM {
		int bmap, offset;
		if (!JF_ISSET(node, ZNODE_WANDER))
			continue;

		clear_le_bit();
	}

	spin_unlock_atom(atom);

	return 0;
}

/** constructor and destructor called on fs mount/unmount */
/** bitmap structure initialization */
int reiser4_init_bitmap ()
{
	
}

/** bitmap structure proper destroying */
int reiser4_done_bitmap ()
{
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
