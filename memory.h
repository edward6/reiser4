/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Memory pressure hooks. Fake inodes handling. See memory.c.
 */

#if !defined( __REISER4_MEMORY_H__ )
#define __REISER4_MEMORY_H__

extern int init_fakes( void );
extern int init_formatted_fake( struct super_block *super );
extern int read_in_formatted( struct super_block *super, 
			      sector_t block, char **data );

/* __REISER4_MEMORY_H__ */
#endif

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
