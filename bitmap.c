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
struct reiser4_bitmap {
	int state;		/* */
	znode * wb;		/* working bitmap block */
	znode * cb;		/* commit bitmap block */
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

static int load_bmap_node (int bmap)
{
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	struct reiser4_bitmap * bnode = &info->bitmap[bmap];

	spin_lock(&info->bitmap_lock);
	if (bnode->wb == NULL) {
		znode * wb_node, * cb_node;
		reiser4_disk_addr addr;

		spin_unlock(&info->bitmap_lock);
		assert("zam-361", bnode->cb == NULL);

		addr.blk = reiser4_get_bitmap_blocknr(bmap);
		wb_node = zget(current_tree, &addr, NULL, 0, GFP_KERNEL);
		if (ERR(wb_node)) return PTR_ERR(wb_node);

		addr.blk = get_commit_bitmap_blocknr(bmap);
		cb_node = zget(current_tree, &addr, NULL, 0, GFP_KERNEL);
		if (ERR(cb_node)) return PTR_ERR(cb_node);

		spin_lock(&info->bitmap_lock);

		if (info->wb == NULL) info->wb = wb_node;
		else                  zput(wb_node);

		if (info->cb == NULL) info->cb = cb_node;
		else                  zput(cb_node);
	}

	spin_unlock(&info->bitmap_lock);

	ret = zload (info->wb);
	if (ret) return ret;

	ret = zload (info->cb);
	if (ret) return ret;

	return 0;
}

static void relse_bitmap_node(bmap)
{
	reiser4_super_info_data * info = reiser4_get_current_super_private(); 
	struct reiser4_bitmap * bnode = &info->bitmap[bmap];

	assert("zam-362", bnode->wb != NULL);
	assert("zam-363", bnode->cb != NULL);

	zrelse(bnode->wb);
	zrelse(bnode->cb);
}

/** This function does all block allocation work but only for one bitmap
 * block.*/
/* FIXME_ZAM: It does not allow us to allocate block ranges across bitmap
 * block responsibility zone boundaries. This had no sense in v3.6 but may
 * have it in v4.x */

static int search_one_bitmap (int bmap, int offset, int max_offset, int min_len, int max_len)
{
	int ret;
	znode * node;
	block_br bmap_block = reiser4_get_bmap_blocknr(bmap);


	ret = load_bitmap_node (bmap);
	if (ret) return ret;

	node = zget(current_tree, bmap_block, NULL, 0, GFP_KERNEL);
	if (ERR(node)) return PTR_ERR(node);


 out:
	release_bitmap_node(bmap);
	return ret;
}

/** clear bits in  bitmap(s) (working, commit or both) */
int reiser4_bitmap_clear (block_nr start, int len, int bitmap)
{
}

/** set bits in bitmap(s) */
int reiser4_bitmap_set (block_nr start, int len, int bitmap)
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

/** bitmap structure initialization; called at fs mount */
int reiser4_init_bitmap ()
{
}

/** bitmap structure proper destroying; called at fs unmount */
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
