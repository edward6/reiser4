/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Memory pressure hooks. Fake inodes handling. See memory.c.
 */

#if !defined( __REISER4_MEMORY_H__ )
#define __REISER4_MEMORY_H__

extern int init_fakes( void );
extern int init_formatted_fake( struct super_block *super );
extern int done_formatted_fake( struct super_block *super );

#if REISER4_DEBUG
extern void *xmemcpy( void *dest, const void *src, size_t n );
extern void *xmemmove( void *dest, const void *src, size_t n );
extern void *xmemset( void *s, int c, size_t n );
#else
#define xmemcpy( d, s, n ) memcpy( ( d ), ( s ), ( n ) )
#define xmemmove( d, s, n ) memmove( ( d ), ( s ), ( n ) )
#define xmemset( s, c, n ) memset( ( s ), ( c ), ( n ) )
#endif

extern tree_operations page_cache_tops;

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
