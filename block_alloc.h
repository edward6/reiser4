/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */


int reiser4_free_block (block_nr block);
int allocate_new_blocks (block_nr * hint, block_nr * count);
int free_blocks (block_nr hint, block_nr count);

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
