/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Memory pressure hooks. Fake inodes handling. See memory.c.
 */

#if !defined( __REISER4_PAGE_CACHE_H__ )
#define __REISER4_PAGE_CACHE_H__

extern int init_fakes( void );
extern int init_formatted_fake( struct super_block *super );
extern int done_formatted_fake( struct super_block *super );

extern reiser4_tree *tree_by_page( const struct page *page );
/*
extern struct page *reiser4_lock_page( struct address_space *mapping, 
				       unsigned long index );
*/
extern void reiser4_check_mem( reiser4_context *ctx );

#if REISER4_DEBUG_MEMCPY
extern void *xmemcpy( void *dest, const void *src, size_t n );
extern void *xmemmove( void *dest, const void *src, size_t n );
extern void *xmemset( void *s, int c, size_t n );
#else
#define xmemcpy( d, s, n ) memcpy( ( d ), ( s ), ( n ) )
#define xmemmove( d, s, n ) memmove( ( d ), ( s ), ( n ) )
#define xmemset( s, c, n ) memset( ( s ), ( c ), ( n ) )
#endif

extern int page_io( struct page *page, jnode *node, int rw, int gfp );
extern int page_common_writeback( struct page *page, 
				  struct writeback_control *wbc, 
				  int flush_flags );

#define define_never_ever_op( op )						\
static int never_ever_ ## op ( void )						\
{										\
	warning( "nikita-1708",							\
		 "Unexpected operation" #op " was called for fake znode" );	\
	return -EIO;								\
}

extern void set_page_clean_nolock (struct page *);

#if REISER4_DEBUG_OUTPUT
extern void print_page( const char *prefix, struct page *page );
#else
#define print_page( prf, p ) noop
#endif

/* __REISER4_PAGE_CACHE_H__ */
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
