/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

DO NOT COMPILE THIS

#include "debug.h"
#include <linux/fs.h>

#define REISER4_FIRST_BITMAP_BLOCK 100


/** return a physical disk address for logical bitmap number @bmap */
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
