/* Copyright 2002 Hans Reiser */

#include "reiser4.h"
#define REISER4_FIRST_BITMAP_BLOCK 100

/** A number of bitmap blocks for given fs. This number can be stored on disk
 * or calculated on fly; it depends on disk format. */
int get_nr_bmap (struct super_block * super)
{
	reiser4_super_info_data * info_data = reiser4_get_super_private(super); 

	assert ("zam-391", super->s_blocksize > 0);
	assert ("zam-392", info_data != NULL);
	assert ("zam-393", info_data->blocks_used != 0);

	return (info_data->blocks_used - 1) / super->s_blocksize + 1;
}


/** return a physical disk address for logical bitmap number @bmap */
block_nr get_bitmap_blocknr (struct super_block * super, int bmap)
{

	assert ("zam-389", bmap >= 0);
	assert ("zam-390", bmap < get_nr_bmap(super));

	/* FIXME_ZAM: before discussing of disk layouts and disk format
	 * plugins I implement bitmap location scheme which is close to scheme
	 * used in reiser 3.6 */
	if (bmap == 0) return REISER4_FIRST_BITMAP_BLOCK;

	return bmap * super->s_blocksize * 8;
}

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
