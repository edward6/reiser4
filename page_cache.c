/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Memory pressure hooks. Fake inodes handling.
 */
/*
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

typedef struct formatted_fake {
	struct inode vfs_inode;
} formatted_fake;

static struct inode *allocate_formatted_fake( struct super_block *sb );
static void destroy_formatted_fake( struct inode *inode );
static struct inode *fake_to_vfs( formatted_fake *fake );
static formatted_fake *vfs_to_fake( struct inode *inode );
static int fake_get_block( struct inode *inode, 
			   sector_t block, struct buffer_head *bh, int create );

static struct super_block dummy;
static struct super_operations dummy_ops;
static struct address_space_operations formatted_fake_as_ops;

/**
 * one-time initialisation of fake inodes handling functions.
 */
int init_fakes()
{
	xmemset( &dummy, 0, sizeof dummy );
	xmemset( &dummy_ops, 0, sizeof dummy_ops );

	/*
	 * FIXME-NIKITA This is remarkably clumsy, but new_inode() relies on
	 * s_ops->alloc_inode(), so we build up dummy super block and super
	 * ops. Having init_inode() function separated from new_inode() would
	 * be better.
	 *
	 * Another possible solution is to add some private field into super
	 * block telling ->alloc_inode() what to do, but this is also
	 * ugly. Let's confine this stupidity in one place and try to talk
	 * Viro into splitting new_inode().
	 */
	dummy_ops.alloc_inode = allocate_formatted_fake;
	dummy_ops.destroy_inode = destroy_formatted_fake;
	dummy.s_op = &dummy_ops;
	return 0;
}

/**
 * initialise fake inode to which formatted nodes are bound in the page cache.
 */
int init_formatted_fake( struct super_block *super )
{
	struct inode *fake;

	assert( "nikita-1703", super != NULL );

	fake = new_inode( &dummy );
	if( fake != NULL ) {
		fake -> i_mapping -> a_ops = &formatted_fake_as_ops;
		fake -> i_blkbits = super -> s_blocksize_bits;
		fake -> i_size    = ~0ull;
		fake -> i_rdev    = super -> s_dev;
		fake -> i_bdev    = super -> s_bdev;
		get_super_private( super ) -> fake = fake;
		/*
		 * FIXME-NIKITA something else?
		 */
		return 0;
	} else
		return -ENOMEM;
}

/**
 * read @block into page cache and bind it to the formatted fake inode of
 * @super. Return pointer to the data in @data.
 */
int read_in_formatted( struct super_block *super, sector_t block, char **area )
{
	unsigned long page_idx;
	struct page  *page;
	int           result;
	int           blksizebits;

	assert( "nikita-1771", super != NULL );
	assert( "nikita-1772", area != NULL );

	blksizebits = super -> s_blocksize_bits;
	/*
	 * only blocks smaller or equal to page size are supported
	 */
	assert( "nikita-1773", PAGE_CACHE_SHIFT >= blksizebits );
	page_idx = block >> ( PAGE_CACHE_SHIFT - blksizebits );
	page = grab_cache_page( get_super_fake( super ) -> i_mapping, 
				page_idx );
	if( page != NULL ) {
		struct buffer_head *bh;

		/*
		 * we have page locked and referenced.
		 */
		assert( "nikita-1774", PageLocked( page ) );

		/*
		 * FIXME-NIKITA add reiser4 decorations to the page, if they
		 * aren't in place: pointer to jnode, whatever. 
		 * 
		 * We are under page lock now, so it can be used as
		 * synchronization.
		 *
		 */

		/*
		 * start io for all blocks on this page. They are close to
		 * each other on the disk, so this is cheap.
		 */
		result = block_read_full_page( page, fake_get_block );
		assert( "nikita-1775", page -> buffers != NULL );

		/*
		 * find buffer head for @block
		 */
		bh = page -> buffers;
		while( bh -> b_blocknr != block ) {
			bh = bh -> b_this_page;
			assert( "nikita-1776", bh != page -> buffers );
		}

		if( REISER4_FORMATTED_CLUSTER_READ )
			/*
			 * wait until all buffers on this page are read in.
			 */
			wait_on_page( page );
		else
			/*
			 * wait on the buffer we are really interested in. io
			 * on other blocks was started, but we don't wait
			 * until it finishes.
			 */
			wait_on_buffer( bh );

		/*
		 * check that buffer containing @block was read in
		 * successfully. We don't care if other blocks on this page
		 * got io error.
		 */
		if( buffer_uptodate( bh ) ) {
			*area = bh -> b_data;
			mark_page_accessed( page );
			result = 0;
		} else
			result = -EIO;
		/*
		 * it is possible that page is still locked. It will be
		 * unlocked by async io completion handler, when last buffer
		 * io finishes.
		 */
	} else
		result = -ENOMEM;
	return result;
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
 * Our memory pressure hook attached to the ->releasepage() method of fake
 * address space.
 *
 * Look carefully at the comments on the top of
 * fs/jbd/transaction.c:journal_try_to_free_buffers() which is ->releasepage()
 * method for ext3.
 *
 * This is called by try_to_release_page() either when we are short of memory
 * or by background memory scanning (kswapd).
 *
 */
static int formatted_fake_pressure_handler( struct page *page, int gfp )
{
	return -ENOSYS;
}

/**
 * helper function to allocate fake inode.
 */
static struct inode *allocate_formatted_fake( struct super_block *sb UNUSED_ARG )
{
	formatted_fake *result;

	assert( "nikita-1704", sb != NULL );
	/*
	 * don't bother to create slab. There is only one per file system.
	 *
	 * Cannot use reiser4_kmalloc(), because there is no context yet.
	 */
	result = kmalloc( sizeof( formatted_fake ), GFP_KERNEL );
	if( result != NULL ) {
		inode_init_once( fake_to_vfs( result ) );
		return fake_to_vfs( result );
	} else
		return NULL;
}

/**
 * ->destroy_inode() method for dummy super block
 */
static void destroy_formatted_fake( struct inode *inode )
{
	assert( "nikita-1707", inode != NULL );
	kfree( vfs_to_fake( inode ) );
}

/**
 * convert fake inode to vfs inode
 */
static struct inode *fake_to_vfs( formatted_fake *fake )
{
	assert( "nikita-1705", fake != NULL );
	return &fake -> vfs_inode;
}

/**
 * convert vfs inode to fake inode
 */
static formatted_fake *vfs_to_fake( struct inode *inode )
{
	assert( "nikita-1706", inode != NULL );
	return list_entry( inode, formatted_fake, vfs_inode );
}

/**
 * get_block for fake inode. 
 */
static int fake_get_block( struct inode *inode, 
			   sector_t block, struct buffer_head *bh, 
			   int create UNUSED_ARG )
{
	assert( "nikita-1777", inode != NULL );
	assert( "nikita-1778", bh != NULL );
	assert( "nikita-1779", !create );

	/*
	 * FIXME-NIKITA check that block < max_block( inode -> i_rdev ).
	 * But max_block is static in fs/block_dev.c
	 */

	bh -> b_dev     = inode -> i_rdev;
	bh -> b_bdev    = inode -> i_bdev;
	bh -> b_blocknr = block;
	bh -> b_state  |= 1UL << BH_Mapped;

	return 0;
}

/**
 * stub for fake address space methods that should be never called
 */
static int never_ever()
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
	.writepage     = NULL,
	.readpage      = NO_SUCH_OP,
	.sync_page     = NO_SUCH_OP,
	.prepare_write = NO_SUCH_OP,
	.commit_write  = NO_SUCH_OP,
	.bmap          = NO_SUCH_OP,
	.flushpage     = NO_SUCH_OP,
	.releasepage   = formatted_fake_pressure_handler,
	.direct_IO     = NO_SUCH_OP
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
