/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

DO NOT COMPILE THIS
#include "debug.h"
#include <linux/fs.h>
#define REISER4_FIRST_BITMAP_BLOCK 100
/* return a physical disk address for logical bitmap number @bmap */
    void
get_bitmap_blocknr(struct super_block *super, int bmap, reiser4_block_nr * bnr)
{

	assert("zam-389", bmap >= 0);
	assert("zam-390", bmap < get_nr_bmap(super));

	/* This introduces a disk layout for bitmaps blocks. */
	if (bmap == 0)
		*bnr = REISER4_FIRST_BITMAP_BLOCK;
	else
		*bnr = bmap * super->s_blocksize * 8;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
