/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Memory pressure hooks. Fake inodes handling.
 */
/*
 * COMMENT BELOW IS OBSOLETE. Will be updated when design stabilizes.
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

static struct page *add_page( struct super_block *super, const jnode *node );

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
		fake -> i_rdev    = super -> s_dev;
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

	fake = get_super_private( super ) -> fake;
	iput( fake );
	return 0;
}

/**
 * grab page to back up new jnode.
 */
void *alloc_jnode_data( struct super_block *super, const jnode *node )
{
	struct page *page;

	page = add_page( super, node );
	if( page != NULL ) {
		kmap( page );
		jnode_attach_to_page( node, page );
		unlock_page( page );
		return page_address( page );
	} else
		return ERR_PTR( -ENOMEM );
}

/**
 * read @block into page cache and bind it to the formatted fake inode of
 * @super. Kmap the page. Return pointer to the data in @data.
 */
void *read_in_jnode_data( struct super_block *super, const jnode *node )
{
	struct page *page;

	/*
	 * FIXME-NIKITA: consider using read_cache_page() here.
	 */
	page = add_page( super, node );
	if( page != NULL ) {
		int result;

		if( !PageUptodate( page ) ) {
			result = page -> mapping -> a_ops -> readpage( NULL, 
								       page );
			if( result == 0 ) {
				wait_on_page_locked( page );
				if( PageUptodate( page ) )
					mark_page_accessed( page );
				else
					result = -EIO;
			}
		} else {
			result = 0;
			unlock_page( page );
		}
		if( result == 0 ) {
			kmap( page );
			jnode_attach_to_page( node, page );
			return page_address( page );
		} else
			return ERR_PTR( result );
	} else
		return ERR_PTR( -ENOMEM );
}

/** ->read_node method of page-cache based tree operations */
int page_cache_read_node( reiser4_tree *tree, jnode *node, char **data )
{
	void *area;

	assert( "nikita-2037", node != NULL );
	assert( "nikita-2038", tree != NULL );
	assert( "nikita-2039", data != NULL );

	area = read_in_jnode_data( tree -> super, node );
	if( !IS_ERR( area ) ) {
		*data = area;
		return 0;
	} else
		return PTR_ERR( area );
}

/** ->allocate_node method of page-cache based tree operations */
int page_cache_allocate_node( reiser4_tree *tree, jnode *node, char **data )
{
	void *area;

	assert( "nikita-2040", tree != NULL );
	assert( "nikita-2041", node != NULL );
	assert( "nikita-2042", data != NULL );

	area = alloc_jnode_data( tree -> super, node );
	if( !IS_ERR( area ) ) {
		*data = area;
		return 0;
	} else
		return PTR_ERR( area );
}

/** ->delete_node method of page-cache based tree operations */
int page_cache_delete_node( reiser4_tree *tree, jnode *node )
{
}

/** ->release_node method of page-cache based tree operations */
int page_cache_release_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
	kunmap( jnode_page( node ) );
	page_cache_release( jnode_page( node ) );
	return 0;
}

/** ->dirty_node method of page-cache based tree operations */
int page_cache_dirty_node( reiser4_tree *tree, jnode *node )
{
	assert( "nikita-2045", JF_ISSET( node, ZNODE_LOADED ) );
	SetPageDirty( jnode_page( node ) );
	return 0;
}

/** 
 * add or fetch page corresponding to jnode to/from the page cache.  Return
 * page locked.
 */
static struct page *add_page( struct super_block *super, const jnode *node )
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
	page_idx = *jnode_get_block( node ) >> ( PAGE_CACHE_SHIFT - blksizebits );
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


#if 1
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
#else
static int formatted_get_block( struct inode *inode, sector_t iblock,
				struct buffer_head *bh, int create UNUSED_ARG )
{
	unsigned long         page_idx;
	struct page          *page;
	struct address_space *mapping;
	jnode                *node;

	assert( "nikita-2032", !create );
	assert( "nikita-2033", inode != NULL );
	assert( "nikita-2034", bh != NULL );

	mapping = inode -> i_mapping;
	page_idx = iblock >> ( PAGE_CACHE_SHIFT - inode -> i_blkbits );

	read_lock( &mapping -> page_lock );
	page = radix_tree_lookup( & mapping -> page_tree, page_idx );
	read_unlock( & mapping -> page_lock );

	assert( "nikita-2030", page != NULL );
	assert( "nikita-2031", PageLocked( page ) );
	assert( "nikita-2035", jprivate( page ) != NULL );

	node = jnode_of_page( page );
	assert( "nikita-2036", jnode_is_formatted( node ) );
	map_bh( bh, inode -> i_sb, jnode_get_block( node ) );
	/*
	 * FIXME-NIKITA BH_Boundary optimizations should go here.
	 */
	return 0;
}
#endif

/** ->readpage() method for formatted nodes */
static int formatted_readpage( struct file *f UNUSED_ARG, 
			       struct page *page /* page to read */ )
{
	struct bio         *bio;

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

#if 1
	assert( "nikita-2025", page != NULL );
	bio = bio_alloc( GFP_NOIO, 1 );
	if( bio != NULL ) {
		jnode              *node;
		int                 blksz;
		struct super_block *super;

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

		/*
		 * submit_bio() is int. But what does return value mean?
		 */
		submit_bio( READ, bio );
		return 0;
	} else
		return -ENOMEM;
#else
	return mpage_readpage( page, formatted_get_block );
#endif
}

/**
 *
 */
static int formatted_fake_pressure_handler( struct page *page UNUSED_ARG, 
					    int *nr_to_write UNUSED_ARG )
{
	return -ENOSYS;
}

/**
 * stub for fake address space methods that should be never called
 */
int never_ever(void)
{
	warning( "nikita-1708", 
		 "Unexpected filemap operation was called for fake znode" );
	return -EIO;
}

#define NO_SUCH_OP ( ( void * ) never_ever )

/**
 * address space operations for the fake inode
 */
static struct address_space_operations formatted_fake_as_ops = {
	.writepage      = NULL,
	/* this is called to read formatted node */
	.readpage       = formatted_readpage,
	.sync_page      = NO_SUCH_OP,
	/* Write back some dirty pages from this mapping. Called from sync. */
	.writepages     = NO_SUCH_OP,
	/* Perform a writeback as a memory-freeing operation. */
	.vm_writeback   = formatted_fake_pressure_handler,
	/* Set a page dirty */
	.set_page_dirty = NULL,
	/* used for read-ahead. Not applicable */
	.readpages      = NO_SUCH_OP,
	.prepare_write  = NO_SUCH_OP,
	.commit_write   = NO_SUCH_OP,
	.bmap           = NO_SUCH_OP,
	.invalidatepage = NO_SUCH_OP,
	.releasepage    = NULL,
	.direct_IO      = NO_SUCH_OP
};

tree_operations page_cache_tops = {
	.read_node     = page_cache_read_node,
	.allocate_node = page_cache_allocate_node,
	.delete_node   = NULL,
	.release_node  = page_cache_release_node,
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
