/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */
/* Memory pressure hooks. Fake inodes handling. See memory.c. */

#if !defined( __REISER4_PAGE_CACHE_H__ )
#define __REISER4_PAGE_CACHE_H__

#include "forward.h"
#include "debug.h"

#include <linux/fs.h>		/* for struct super_block, address_space  */
#include <linux/mm.h>		/* for struct page  */

extern int init_fakes(void);
extern int init_formatted_fake(struct super_block *super);
extern int done_formatted_fake(struct super_block *super);

extern reiser4_tree *tree_by_page(const struct page *page);
extern void reiser4_lock_page(struct page *page);
extern void reiser4_unlock_page(struct page *page);

#if REISER4_TRACE_TREE || REISER4_STATS
extern char *jnode_short_info(const jnode *j, char *buf);
extern int reiser4_submit_bio_helper(const char *moniker, 
				     int rw, struct bio *bio);
#define reiser4_submit_bio(rw, bio)				\
	reiser4_submit_bio_helper(__FUNCTION__, (rw), (bio))
#else
#define reiser4_submit_bio(rw, bio) submit_bio((rw), (bio))
#endif

extern void reiser4_wait_page_writeback (struct page * page);
static inline void lock_and_wait_page_writeback (struct page * page)
{
	reiser4_lock_page(page);
	if (unlikely(PageWriteback(page)))
	    reiser4_wait_page_writeback(page);
}  

#define jprivate(page) ((jnode *) (page)->private)

extern int page_io(struct page *page, jnode * node, int rw, int gfp);
extern int page_common_writeback(struct page *page, struct writeback_control *wbc, int flush_flags);

#define define_never_ever_op( op )						\
static int never_ever_ ## op ( void )						\
{										\
	warning( "nikita-1708",							\
		 "Unexpected operation" #op " was called for fake znode" );	\
	return RETERR(-EIO);							\
}

extern void drop_page(struct page *page, jnode * node);

#if REISER4_DEBUG_OUTPUT
extern void print_page(const char *prefix, struct page *page);
extern void print_page_state(const char *prefix, struct page_state *ps);
extern void print_page_stats(const char *prefix);
#else
#define print_page(prf, p) noop
#define print_page_state(prefix, ps) noop
#define print_page_stats(prefix) noop
#endif

/* __REISER4_PAGE_CACHE_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
