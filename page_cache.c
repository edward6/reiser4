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

/*
 * Life cycle of pages/nodes.
 *
 * jnode contains reference to page and page contains reference back to
 * jnode. This reference is counted in page ->count. Thus, page bound to jnode
 * cannot be released back into free pool.
 *
 *  1. Formatted nodes.
 *
 *    1. formatted node is represented by znode. When new znode is created its
 *    ->pg pointer is NULL initially.
 *
 *    2. when node content is loaded into znode (by call to zload()) for the
 *    first time following happens (in call to ->read_node() or
 *    ->allocate_node()):
 *
 *      1. new page is added to the page cache.
 *
 *      2. this page is attached to znode and its ->count is increased.
 *
 *      3. page is kmapped.
 *
 *    3. if more calls to zload() follow (without corresponding zrelses), page
 *    counter is left intact and in its stead ->d_count is increased in znode.
 *
 *    4. each call to zrelse decreases ->d_count. When ->d_count drops to zero
 *    ->release_node() is called and page is kunmapped as result.
 *
 *    5. at some moment node can be captured by a transaction. Its ->x_count
 *    is then increased by transaction manager.
 *
 *    6. if node is removed from the tree (empty node with ZNODE_HEARD_BANSHEE
 *    bit set) following will happen (also see comment at the top of znode.c):
 *
 *      1. when last lock is released, node will be uncaptured from
 *      transaction. This released reference that transaction manager acquired
 *      at the step 5.
 *
 *      2. when last reference is released, zput() detects that node is
 *      actually deleted and calls ->delete_node()
 *      operation. page_cache_delete_node() implementation detaches jnode from
 *      page and releases page.
 *
 *    7. otherwise (node wasn't removed from the tree), last reference to
 *    znode will be released after transaction manager committed transaction
 *    node was in. This implies squallocing of this node (see
 *    flush.c). Nothing special happens at this point. Znode is still in the
 *    hash table and page is still attached to it.
 *
 *    8. znode is actually removed from the memory because of the memory
 *    pressure, or during umount (znodes_tree_done()). Anyway, znode is
 *    removed by the call to zdrop(). At this moment, page is detached from
 *    znode and removed from the inode address space.
 *
 */

#include "reiser4.h"

static struct bio *page_bio( struct page *page, int rw, int gfp );

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
		assert( "nikita-2168", fake -> i_state & I_NEW );
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

#if REISER4_DEBUG_MEMCPY
struct mem_ops_table {
	void * ( *cpy ) ( void *dest, const void *src, size_t n );
	void * ( *move )( void *dest, const void *src, size_t n );
	void * ( *set ) ( void *s, int c, size_t n );
};

void *xxmemcpy( void *dest, const void *src, size_t n )
{
	return memcpy( dest, src, n );
}

void *xxmemmove( void *dest, const void *src, size_t n )
{
	return memmove( dest, src, n );
}

void *xxmemset( void *s, int c, size_t n )
{
	return memset( s, c, n );
}

struct mem_ops_table std_mem_ops = {
	.cpy  = xxmemcpy,
	.move = xxmemmove,
	.set  = xxmemset
};

struct mem_ops_table *mem_ops = &std_mem_ops;

/*
 * Our own versions of memcpy, memmove, and memset used to profile shifts of
 * tree node content. Coded to avoid inlining.
 */
void *xmemcpy( void *dest, const void *src, size_t n )
{
	return mem_ops -> cpy( dest, src, n );
}

void *xmemmove( void *dest, const void *src, size_t n )
{
	return mem_ops -> move( dest, src, n );
}

void *xmemset( void *s, int c, size_t n )
{
	return mem_ops -> set( s, c, n );
}

#endif


/** 
 * completion handler for single page bio-based read. 
 *
 * mpage_end_io_read() would also do. But it's static.
 *
 */
static void end_bio_single_page_read( struct bio *bio )
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

/** 
 * completion handler for single page bio-based write. 
 *
 * mpage_end_io_write() would also do. But it's static.
 *
 */
static void end_bio_single_page_write( struct bio *bio )
{
	struct page *page;

	page = bio -> bi_io_vec[ 0 ].bv_page;

	if( !test_bit( BIO_UPTODATE, &bio -> bi_flags ) )
		SetPageError( page );
	end_page_writeback( page );
	bio_put( bio );
}

/** ->readpage() method for formatted nodes */
static int formatted_readpage( struct file *f UNUSED_ARG, 
			       struct page *page /* page to read */ )
{
	return page_io( page, READ, GFP_KERNEL );
}

/** ->writepage() method for formatted nodes */
static int formatted_writepage( struct page *page /* page to write */ )
{
	return page_io( page, WRITE, GFP_NOFS | __GFP_HIGH );
}

int page_io( struct page *page, int rw, int gfp )
{
	struct bio *bio;
	int         result;

	assert( "nikita-2094", page != NULL );
	assert( "nikita-2226", PageLocked( page ) );

	bio = page_bio( page, rw, gfp );
	if( !IS_ERR( bio ) ) {
		if( rw == WRITE ) {
			SetPageWriteback( page );
			unlock_page(page);
		}
		submit_bio( rw, bio );
		result = 0;
	} else
		result = PTR_ERR( bio );
	return result;
}


/** helper function to construct bio for page */
static struct bio *page_bio( struct page *page, int rw, int gfp )
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
		reiser4_block_nr    blocknr;

		trace_if( TRACE_BUG, print_page( __FUNCTION__, page ) );

		assert( "nikita-2172", jprivate( page ) != NULL );
		node = jprivate( page );
		super = page -> mapping -> host -> i_sb;
		assert( "nikita-2029", super != NULL );
		blksz = super -> s_blocksize;
		assert( "nikita-2028", blksz == ( int ) PAGE_CACHE_SIZE );

		blocknr = *jnode_get_block( node );
		assert( "nikita-2275", blocknr != ( reiser4_block_nr ) 0 );
		assert( "nikita-2276", !blocknr_is_fake( &blocknr ) );

		bio -> bi_sector = blocknr * ( blksz >> 9 );
		bio -> bi_bdev   = super -> s_bdev;
		bio -> bi_io_vec[ 0 ].bv_page   = page;
		bio -> bi_io_vec[ 0 ].bv_len    = blksz;
		bio -> bi_io_vec[ 0 ].bv_offset = 0;

		bio -> bi_vcnt = 1;
		bio -> bi_idx  = 0; /* FIXME: JMACD->NIKITA: can you explain why you set idx?  I don't think its needed. */
		bio -> bi_size = blksz;

		bio -> bi_end_io = ( rw == READ ) ? 
			end_bio_single_page_read : end_bio_single_page_write;

		return bio;
	} else
		return ERR_PTR( -ENOMEM );
}

static int formatted_vm_writeback( struct page *page, int *nr_to_write )
{
	return page_common_writeback( page, nr_to_write, JNODE_FLUSH_MEMORY_FORMATTED);
}

/**
 * Common memory pressure notification.
 */
int page_common_writeback( struct page *page, int *nr_to_write, int flush_flags )
{
	int result;
	jnode *node;
	reiser4_context *ctx;
	txn_handle *txnh;
	REISER4_ENTRY( page -> mapping -> host -> i_sb );

	assert( "vs-828", PageLocked( page ) );
	unlock_page( page );

	ctx  = get_current_context ();
	txnh = ctx->trans;

	if (! spin_trylock_txnh (txnh)) {
		REISER4_EXIT (0);
	}

	if (txnh->atom != NULL || ! lock_stack_isclean( & ctx->stack )) {
		/*
		 * Good Lord, we are called synchronously! What a shame.
		 *
		 * we got here by
		 * __alloc_pages->balance_classzone->...->shrink_cache
		 *
		 * no chance of working in such situation.
		 */
		spin_unlock_txnh (txnh);
		REISER4_EXIT (0);
	}

	node = jnode_by_page (page);

	/* Attach the txn handle to this node, preventing the atom from committing while
	 * this flush occurs. */ 
	result = txn_attach_txnh_to_node (txnh, node, ATOM_FORCE_COMMIT);

	spin_unlock_txnh (txnh);

	if (result == -ENOENT) {

		/* Txn committed during attach, jnode has no atom. */
		result = 0;

	} else if (result == 0) {
		/* And flush it... */
		result = jnode_flush (node, nr_to_write, flush_flags);
	}

	REISER4_EXIT (result);
}

static int formatted_set_page_dirty( struct page *page )
{
	assert( "nikita-2173", page != NULL );
	return __set_page_dirty_nobuffers( page );
}

define_never_ever_op( readpages )
define_never_ever_op( prepare_write )
define_never_ever_op( commit_write )
define_never_ever_op( bmap )
define_never_ever_op( direct_IO )

#define V( func ) ( ( void * ) ( func ) )

/** place holder for methods that doesn't make sense for fake inode */

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
	 * This is most annoyingly misnomered method. Actually it is called
	 * from wait_on_page_bit() and lock_page() and its purpose is to
	 * actually start io by jabbing device drivers.
	 */
	.sync_page      = block_sync_page,
	/* Write back some dirty pages from this mapping. Called from sync.
	   called during sync (pdflush) */
	.writepages     = reiser4_writepages,
	/* Perform a writeback as a memory-freeing operation. */
	.vm_writeback   = formatted_vm_writeback,
	/* Set a page dirty */
	.set_page_dirty = formatted_set_page_dirty,
	/* used for read-ahead. Not applicable */
	.readpages      = V( never_ever_readpages ),
	.prepare_write  = V( never_ever_prepare_write ),
	.commit_write   = V( never_ever_commit_write ),
	.bmap           = V( never_ever_bmap ),
	/* called just before page is being detached from inode mapping and
	 * removed from memory. Called on truncate, cut/squeeze, and
	 * umount. */
	.invalidatepage = reiser4_invalidatepage,
	/**
	 * this is called by shrink_cache() so that file system can try to
	 * release objects (jnodes, buffers, journal heads) attached to page
	 * and, may be made page itself free-able.
	 */
	.releasepage    = reiser4_releasepage,
	.direct_IO      = V( never_ever_direct_IO )
};

#if REISER4_DEBUG

#define page_flag_name( page, flag )			\
	( test_bit( ( flag ), &( page ) -> flags ) ? ((#flag ## "|")+3) : "" )

void print_page( const char *prefix, struct page *page )
{
	if( page == NULL ) {
		info( "null page\n" );
		return;
	}
	info( "%s: page index: %lu mapping: %p count: %i private: %lx\n",
	      prefix, page -> index, page -> mapping, 
	      atomic_read( &page -> count ), page -> private );
	info( "\tflags: %s%s%s%s %s%s%s%s %s%s%s%s %s%s%s\n",
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
	      page_flag_name( page,  PG_nosave ) );
	if( jprivate( page ) != NULL ) {
		info_znode( "\tpage jnode", ( znode * ) jprivate( page ) );
		info( "\n" );
	}
}


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
