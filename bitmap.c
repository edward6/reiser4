/* Copyright 2002, Hans Reiser */

/*
 *
 */

#include "reiser4.h"

/* a disk format depended function which returns a physical disk address for
   logical bitmap number @bmap */
extern block_nr reiser4_get_bmap_blocknr (int bmap);

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
 * sb->u.reiser4_i.bitmap[bmap].working (commit) pointer to zero */

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
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	struct super_block * super = reiser4_get_current_context()->super;
	int ret = 0;

	spin_lock_tree(current_tree);

	if (bnode->working == NULL) {
		ret = load_bnode_half(&bnode->working, reiser4_get_bitmap_blocknr(bmap));

		if (ret < 0) goto out;
	}

	if (bnode->commit == NULL) {
		ret = load_bnode_half(&bnode->commit, 
				      get_commit_bitmap_blocknr(info->bitmap - bnode));

		if (ret < 0) goto out;

		if (ret == 0) {
			/* commit bitmap is initialized by on-disk bitmap
			 * content (working bitmap in this context) */
			memcpy(bnode->commit->data,
			       bnode->working->data,
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

	zput(bnode-wb);
	zput(bnode->commit);
}

#endif

/** find out real block number for given reiser4 node formatted or
 * unformatted */
static inline block_nr reconstruct_blocknr (jnode * node)
{
	if (!JF_ISSET(node, ZNODE_UNFORMATTED)) 
		return JZNODE(node)->blocknr.blk;
	/* ??? */
	return 0;
}

/** find out wandered block number */
static inline block_nr reconstruct_wandered_blocknr (jnode * node)
{
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

	/*release_bnode(bnode);*/

	return ret;
}

/** clear bits in bitmap(s) (working, commit or both) */
static int bitmap_clear (block_nr start, int len, int bitmap)
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

#define WALK_ATOM                                             \
        for (h = 0; h < REAL_MAX_ZTREE_HEIGHT; h ++)          \
        for (node = capture_list_front(&atom->dirty_nodes[h]); \
             capture_list_end(&atom->dirty_nodes[h]);          \
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

/** called after transaction commit, apply DELETE SET to WORKING BITMAP */
int reiser4_bitmap_done_commit (txn_atom * atom) {
	WALK_ATOM_VARS;
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 

	assert ("zam-382", info->bitmap != NULL);

	spin_lock_atom (atom);
	WALK_ATOM {
		block_nr block;
		int bmap, offset;
		struct reiser4_bnode * bnode;

		if (!JF_SET(node, ZNODE_DELETESET))
			continue;

		block = reconstruct_blocknr(node);
		parse_blocknr(block, &bmap, &offset);

		bnode = (struct reiser4_bnode*)(info->bitmap) + bmap;

		assert ("zam-383", bnode->working != NULL);
		assert ("zam-384", bnode->commit != NULL);

		clear_le_bit(offset, bnode->working->data);
	}
	spin_unlock_atom (atom);
}

/** This function is called after write-back (writing blocks from OVERWRITE
 * SET to real locations) transaction stage completes. (clear WANDERED SET in
 * WORKING BITMAP) */
int reiser4_bitmap_done_flush ()
{
	reiser4_super_info_data * info = reiser4_get_current_super_private();
	WALK_ATOM_VARS;

	assert ("zam-380", info->bitmap != NULL);

	spin_lock_atom(atom);

	/* FIXME_ZAM: we do not know what dynamic objects will be used to keep
	 * informations about wandered blocks locations. This alg. is done
	 * under an assumption that those objects are jnodes. */
	WALK_ATOM {
		int bmap, offset;
		struct reiser4_bnode * bnode;
		block_nr wandered;

		if (!JF_ISSET(node, ZNODE_WANDER))
			continue;

		/* FIXME-ZAM: there should be way to
		 * reconstruct wandered blocknr from jnode's
		 * page offset or something else.*/;
		wandered = reconstruct_wandered_blocknr(node);

		parse_blocknr(wandered, &bmap, &offset);
		bnode = (struct reiser4_bnode*)(info->bitmap) + bmap;

		assert ("zam-379", bnode->commit != NULL);
		assert ("zam-381", bnode->working != NULL);

		clear_le_bit(offset, bnode->working->data);
	}

	spin_unlock_atom(atom);

	return 0;
}

/** constructor and destructor called on fs mount/unmount */
/** bitmap structure initialization */
int reiser4_init_bitmap ()
{
	struct super_block * super = reiser4_get_current_context()->super;
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	int bitmap_blocks_nr;
	int i;
	int ret = 0;

	assert("zam-372", info->blocks_used != 0);
	assert ("zam-375", super->s_blocksize != 0);

	bitmap_blocks_nr = (info->blocks_used - 1) % super->s_blocksize + 1; 

	info->bitmap = reiser4_kmalloc (sizeof(struct reiser4_bnode) * bitmap_blocks_nr, 
					GFP_KERNEL);
	if (info->bitmap == NULL)
		return -ENOMEM;

	/* at fs mount time, no one bitmap block is loaded into memory */
	memset(info->bitmap, sizeof(struct reiser4_bnode) * bitmap_blocks_nr);

	return 0;
}

/** bitmap structure proper destroying */
void reiser4_done_bitmap ()
{
	struct super_block * super = reiser4_get_current_context()->super;
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	int bitmap_blocks_nr;
	int i;

	assert ("zam-376", info->bitmap != NULL);
	assert ("zam-373", info->block_used != 0);
	assert ("zam-374", super->s_blocksize != 0);

	bitmap_blocks_nr = (info->blocks_used - 1) % super->s_blocksize + 1;

	for (i = 0; i < bitmap_blocks_nr; i ++) {
		struct reiser4_bnode * bnode 
			= (struct reiser4_bnode *)(info->bitmap) + i;

		assert ("zam-378", equiv(bnode->working == NULL, bnode->commit == NULL));

		if (bnode->working != NULL) {
			zrelse(bnode->working, 1);
			zrelse(bnode->commit, 1);

			zput(bnode->working);
			zput(bnode->commit);
			
			zdestroy(bnode->commit);
			zdestroy(bnode->working);
		}
	}

	reiser4_kfree(info->bitmap, sizeof(struct reiser4_bnode) * bitmap_blocks_nr);
	info->bitmap = NULL;
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
