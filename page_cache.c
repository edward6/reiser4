/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Memory pressure hooks. Fake inodes handling.
 */
/*
 *
 * We store all file system meta data (and data, of course) in the page cache.
 *
 * What does this mean? In stead of using bread/brelse we create special
 * "fake" inode (one per super block) and store content of formatted nodes
 * into pages bound to this inode in the page cache. In newer kernels bread()
 * already uses inode attached to block device (bd_inode). Advantage of having
 * our own fake inode is that we can install appropriate methods in its
 * address_space operations. Such methods are called by VM on memory pressure
 * (or during background page flushing) and we can use them to react
 * appropriately.
 *
 * In initial version we only support one block per page. Support for multiple
 * blocks per page is complicated by relocation.
 *
 * To each page, used by reiser4, jnode is attached. jnode is analogous to
 * buffer head. Difference is that jnode is bound to the page permanently:
 * jnode cannot be removed from memory until its backing page is.
 *
 * jnode contain pointer to page (->pg field) and page contain pointer to
 * jnode in ->private field. These fields are protected by global
 * _jnode_ptr_lock spinlock. This is so, because we have to go in both
 * direction and jnode spinlock is useless when going from page to
 * jnode. Scalability can be improved by introducing array of spinlocks and
 * hashing by page ->offset. Then something similar to try-and-release
 * approach of transaction manager has to be used.
 *
 * Properties:
 *
 * 1. when jnode-to-page mapping is established (by jnode_attach_page()), page
 * reference counter is increased.
 *
 * 2. when jnode-to-page mapping is destroyed (by jnode_detach_page() and
 * page_detach_jnode()), page reference counter is decreased.
 *
 * 3. when znode is loaded (->d_count becomes larger than 0), page is kmapped.
 *
 * 4. when znode is unloaded (->d_count drops to zero), page is kunmapped.
 *
 * 5. kmapping/kunmapping of unformatted pages is done by read/write methods.
 *
 *
 *
 *
 *
 *
 * THIS COMMENT IS VALID FOR "MANY BLOCKS ON PAGE" CASE
 *
 * Fake inode is used to bound formatted nodes and each node is indexed within
 * fake inode by its block number. If block size of smaller than page size, it
 * may so happen that block mapped to the page with formatted node is occupied
 * by unformatted node or is unallocated. This lead to some complications,
 * because flushing whole page can lead to an incorrect overwrite of
 * unformatted node that is moreover, can be cached in some other place as
 * part of the file body. To avoid this, buffers for unformatted nodes are
 * never marked dirty. Also pages in the fake are never marked dirty. This
 * rules out usage of ->writepage() as memory pressure hook. In stead
 * ->releasepage() is used.
 *
 * Josh is concerned that page->buffer is going to die. This should not pose
 * significant problem though, because we need to add some data structures to
 * the page anyway (jnode) and all necessary book keeping can be put there.
 *
 */

#include "reiser4.h"

static struct page *add_page( struct super_block *super, jnode *node );
static void kmap_once( jnode *node, struct page *page );
static struct bio *page_bio( struct page *page, int gfp );

static struct address_space_operations formatted_fake_as_ops;

#define jprivate( page ) ( ( jnode * ) ( page ) -> private )

static const __u64 fake_ino = 0x1;

/**
 * one-time initialisation of fake inodes handling functions.
 */
int init_fakes()
{
	return 0;
}

/**
 * initialise fake inode to which formatted nodes are bound in the page cache.
 */
int init_formatted_fake( struct super_block *super )
{
	struct inode *fake;

	assert( "nikita-1703", super != NULL );

	fake = iget_locked( super, fake_ino );

	if( fake ) {
		assert( "nikita-2021", fake -> i_state & I_NEW );
		fake -> i_mapping -> a_ops = &formatted_fake_as_ops;
		fake -> i_blkbits = super -> s_blocksize_bits;
		fake -> i_size    = ~0ull;
		fake -> i_rdev    = to_kdev_t( super -> s_bdev -> bd_dev );
		fake -> i_bdev    = super -> s_bdev;
		get_super_private( super ) -> fake = fake;
		/*
		 * FIXME-NIKITA something else?
		 */
		unlock_new_inode( fake );
		return 0;
	} else
		return -ENOMEM;
}

/** release fake inode */
int done_formatted_fake( struct super_block *super )
{
	struct inode *fake;

	fake = get_super_private_nocheck( super ) -> fake;
	iput( fake );
	return 0;
}

/** 
 * ->read_node method of page-cache based tree operations 
 *
 * read @block into page cache and bind it to the formatted fake inode of
 * @super. Kmap the page. Return pointer to the data in @data.
 */
static int page_cache_read_node( reiser4_tree *tree, jnode *node )
{
	struct page *page;

	assert( "nikita-2037", node != NULL );
	assert( "nikita-2038", tree != NULL );

	/*
	 * FIXME-NIKITA: consider using read_cache_page() here.
	 */
	page = add_page( tree -> super, node );
	if( page != NULL ) {
		if( !PageUptodate( page ) ) {
			int result;

			result = page -> mapping -> a_ops -> readpage( NULL,
								       page );
			if( result == 0 ) {
				wait_on_page_locked( page );
				if( PageUptodate( page ) )
					mark_page_accessed( page );
				else
					return -EIO;
			} else
				return result;
		} else
			unlock_page( page );
		kmap_once( node, page );
		/* return with jnode spin-locked */
		return 0;
	} else
		return -ENOMEM;
}

/** 
 * ->allocate_node method of page-cache based tree operations 
 *
 * grab page to back up new jnode.
 */
static int page_cache_allocate_node( reiser4_tree *tree, jnode *node )
{
	struct page *page;

	assert( "nikita-2040", tree != NULL );
	assert( "nikita-2041", node != NULL );

	page = add_page( tree -> super, node );
	if( page != NULL ) {
		/*
		 * FIXME-NIKITA *dubious*
		 */
		SetPageUptodate( page );
		unlock_page( page );
		kmap_once( node, page );
		/* return with jnode spin-locked */
		return 0;
	} else
		return -ENOMEM;
}

/** ->release_node method of page-cache based tree operations */
static int page_cache_release_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
	kunmap( jnode_page( node ) );
	assert( "nikita-2072", JF_ISSET( node, ZNODE_KMAPPED ) );
	JF_CLR( node, ZNODE_KMAPPED );
	return 0;
}

/** ->delete_node method of page-cache based tree operations */
static int page_cache_delete_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
	struct page *page;

	page = jnode_page( node );

	/* FIXME-NIKITA locking? */
	ClearPageDirty( page );
	ClearPageUptodate( page );
	remove_inode_page( page );
 	jnode_detach_page( node );
	return 0;
}

/** ->drop_node method of page-cache based tree operations */
static int page_cache_drop_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
 	jnode_detach_page( node );
	return 0;
}

/** ->dirty_node method of page-cache based tree operations */
static int page_cache_dirty_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
	assert( "nikita-2045", JF_ISSET( node, ZNODE_LOADED ) );
	set_page_dirty( jnode_page( node ) );
	return 0;
}

/** helper function to perform kmap  */
static void kmap_once( jnode *node, struct page *page )
{
	assert( "nikita-2073", node != NULL );
	assert( "nikita-2074", page != NULL );

	kmap( page );
	spin_lock_jnode( node );
	/*
	 * FIXME-NIKITA use test_and_set here.
	 */
	if( likely( !JF_ISSET( node, ZNODE_KMAPPED ) ) )
		JF_SET( node, ZNODE_KMAPPED );
	else
		kunmap( page );
}

/** 
 * add or fetch page corresponding to jnode to/from the page cache.  Return
 * page locked.
 */
static struct page *add_page( struct super_block *super, jnode *node )
{
	unsigned long page_idx;
	struct page  *page;
	int           blksizebits;

	assert( "nikita-2023", super != NULL );

	blksizebits = super -> s_blocksize_bits;
	/*
	 * only blocks smaller or equal to page size are supported
	 */
	assert( "nikita-1773", PAGE_CACHE_SHIFT >= blksizebits );
	/* page_idx = *jnode_get_block( node ) >> ( PAGE_CACHE_SHIFT - blksizebits ); */

	/*
	 * Our initial design was to index pages with formatted data by their
	 * block numbers. One disadvantage of this is that such setup makes
	 * relocation harder to implement: when tree node is relocated we need
	 * to re-index its data in a page cache. To avoid data copying during
	 * this re-indexing it was decided that first version of reiser4 will
	 * only support block size equal to PAGE_CACHE_SIZE.
	 *
	 * But another problem came up: our block numbers are 64bit and pages
	 * are indexed by 32bit ->index. Moreover:
	 *
	 *  - there is strong opposition for enlarging ->index field (and for
	 *  good reason: size of struct page is critical, because there are so
	 *  many of them).
	 *
	 *  - our "unallocated" block numbers have highest bit set, which
	 *  makes 64bit block number support essential independently of device
	 *  size.
	 *
	 * Code below uses jnode _address_ as page index. This has following
	 * advantages:
	 *
	 *  - relocation is simplified
	 *
	 *  - if ->index is jnode address, than ->private is free for use. It
	 *  can be used to store some jnode data making it smaller (not yet
	 *  implemented). Pointer to atom?
	 *
	 */
	page_idx = ( unsigned long ) node;
	page = grab_cache_page( get_super_fake( super ) -> i_mapping, page_idx );
	if( unlikely( page == NULL ) )
		return NULL;

	/*
	 * we have page locked and referenced.
	 */
	assert( "nikita-1774", PageLocked( page ) );

	/*
	 * add reiser4 decorations to the page, if they aren't in place:
	 * pointer to jnode, whatever.
	 * 
	 * We are under page lock now, so it can be used as synchronization.
	 *
	 */
	jnode_attach_page( node, page );
	page_cache_release( page );
	return page;
}

#if REISER4_DEBUG
void *xmemcpy( void *dest, const void *src, size_t n )
{
	return memcpy( dest, src, n );
}

void *xmemmove( void *dest, const void *src, size_t n )
{
	return memmove( dest, src, n );
}

void *xmemset( void *s, int c, size_t n )
{
	return memset( s, c, n );
}
#endif


/** 
 * completion handler for single page bio-based io. 
 *
 * mpage_end_io_read() would also do. But it's static.
 *
 */
static void end_bio_single_page_io_sync( struct bio *bio )
{
	struct page *page;

	page = bio -> bi_io_vec[ 0 ].bv_page;

	if( test_bit( BIO_UPTODATE, &bio -> bi_flags ) )
		SetPageUptodate( page );
	else {
		ClearPageUptodate( page );
		SetPageError( page );
	}
	unlock_page( page );
	bio_put( bio );
}

/** ->readpage() method for formatted nodes */
static int formatted_readpage( struct file *f UNUSED_ARG, 
			       struct page *page /* page to read */ )
{
	return page_io( page, READ, GFP_NOIO );
}

/** ->writepage() method for formatted nodes */
static int formatted_writepage( struct page *page /* page to write */ )
{
	return page_io( page, WRITE, GFP_NOIO );
}

int page_io( struct page *page, int rw, int gfp )
{
	struct bio *bio;
	int         result;
	REISER4_ENTRY( page -> mapping -> host -> i_sb );
	
	assert( "nikita-2094", page != NULL );

	bio = page_bio( page, gfp );
	if( !IS_ERR( bio ) ) {
		submit_bio( rw, bio );
		result = 0;
	} else
		result = PTR_ERR( bio );
	REISER4_EXIT( result );
}

#define page_flag_name( page, flag )			\
	( test_bit( ( flag ), &( page ) -> flags ) ? ((#flag ## "|")+3) : "" )

void __tmp_print_page( struct page *page )
{
	if( page == NULL ) {
		info( "null page\n" );
		return;
	}
	info( "page index: %lu virtual: %p mapping: %p count: %i private: %lx kmap_count %d\n",
	      page -> index, page -> virtual, page -> mapping, page -> count,
	      page -> private, page -> kmap_count );
	info( "flags: %s%s%s%s %s%s%s%s %s%s%s%s %s%s%s%s\n",
	      page_flag_name( page,  PG_locked ),
	      page_flag_name( page,  PG_error ),
	      page_flag_name( page,  PG_referenced ),
	      page_flag_name( page,  PG_uptodate ),

	      page_flag_name( page,  PG_dirty_dontuse ),
	      page_flag_name( page,  PG_lru ),
	      page_flag_name( page,  PG_active ),
	      page_flag_name( page,  PG_slab ),

	      page_flag_name( page,  PG_highmem ),
	      page_flag_name( page,  PG_checked ),
	      page_flag_name( page,  PG_arch_1 ),
	      page_flag_name( page,  PG_reserved ),

	      page_flag_name( page,  PG_private ),
	      page_flag_name( page,  PG_writeback ),
	      page_flag_name( page,  PG_nosave ),
	      page_flag_name( page,  PG_kmapped ) );
}


/** helper function to construct bio for page */
static struct bio *page_bio( struct page *page, int gfp )
{
	struct bio *bio;
	assert( "nikita-2092", page != NULL );

	/*
	 * Simple implemenation in the assumption that blocksize == pagesize.
	 *
	 * We only have to submit one block, but submit_bh() will allocate bio
	 * anyway, so lets use all the bells-and-whistles of bio code.
	 *
	 * This is roughly equivalent to mpage_readpage() for one
	 * page. mpage_readpage() is not used, because it depends on
	 * get_block() to obtain block number and get_block() gets everything,
	 * but page---and we need page to obtain block number from jnode. One
	 * line change to mpage_readpage() (bh.b_page = page;) and it can be
	 * used. Other problem is the do_mpage_readpage() checks
	 * page_has_buffers().
	 *
	 */

	bio = bio_alloc( gfp, 1 );
	if( bio != NULL ) {
		jnode              *node;
		int                 blksz;
		struct super_block *super;

		trace_if( TRACE_BUG, 
			  info_jnode( "page", ( jnode * ) page -> index ) );
		trace_if( TRACE_BUG, __tmp_print_page( page ) );

		assert( "nikita-2026", jprivate( page ) != NULL );
		node = jprivate( page );
		assert( "nikita-2027", jnode_is_formatted( node ) );
		super = page -> mapping -> host -> i_sb;
		assert( "nikita-2029", super != NULL );
		blksz = super -> s_blocksize;
		assert( "nikita-2028", blksz == PAGE_CACHE_SIZE );

		bio -> bi_sector = *jnode_get_block( node ) * ( blksz >> 9 );
		bio -> bi_bdev   = super -> s_bdev;
		bio -> bi_io_vec[ 0 ].bv_page   = page;
		bio -> bi_io_vec[ 0 ].bv_len    = blksz;
		bio -> bi_io_vec[ 0 ].bv_offset = 0;

		bio -> bi_vcnt = 1;
		bio -> bi_idx  = 0;
		bio -> bi_size = blksz;

		bio -> bi_end_io = end_bio_single_page_io_sync;

		return bio;
	} else
		return ERR_PTR( -ENOMEM );
}

/**
 * memory pressure notification. Flush transaction, etc.
 */
static int formatted_fake_pressure_handler( struct page *page UNUSED_ARG, 
					    int *nr_to_write UNUSED_ARG )
{
	return 0;
}

static int formatted_set_page_dirty( struct page *page )
{
	assert( "nikita-2095", page != NULL );
	return __set_page_dirty_nobuffers( page );
}

define_never_ever_op( readpages )
define_never_ever_op( prepare_write )
define_never_ever_op( commit_write )
define_never_ever_op( bmap )
define_never_ever_op( direct_IO )

#define V( func ) ( ( void * ) ( func ) )

/** place holder for methods that doesn't make sense for fake inode */
static int ok( void )
{
	return 0;
}

/**
 * address space operations for the fake inode
 */
static struct address_space_operations formatted_fake_as_ops = {
	.writepage      = formatted_writepage,
	/* this is called to read formatted node */
	.readpage       = formatted_readpage,
	/**
	 * ->sync_page() method of fake inode address space operations. Called
	 * from wait_on_page() and lock_page().
	 *
	 * FIXME-NIKITA not sure what to do.
	 */
	.sync_page      = NULL,
	/* Write back some dirty pages from this mapping. Called from sync. */
	.writepages     = NULL,
	/* Perform a writeback as a memory-freeing operation. */
	.vm_writeback   = formatted_fake_pressure_handler,
	/* Set a page dirty */
	.set_page_dirty = formatted_set_page_dirty,
	/* used for read-ahead. Not applicable */
	.readpages      = V( never_ever_readpages ),
	.prepare_write  = V( never_ever_prepare_write ),
	.commit_write   = V( never_ever_commit_write ),
	.bmap           = V( never_ever_bmap ),
	.invalidatepage = V( ok ),
	.releasepage    = NULL,
	.direct_IO      = V( never_ever_direct_IO )
};

node_operations page_cache_tops = {
	.read_node     = page_cache_read_node,
	.allocate_node = page_cache_allocate_node,
	.delete_node   = page_cache_delete_node,
	.release_node  = page_cache_release_node,
	.drop_node     = page_cache_drop_node,
	.dirty_node    = page_cache_dirty_node
};


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
