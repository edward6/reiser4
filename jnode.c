/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Jnode manipulation functions.
 */
/*
 * Jnode is entity used to track blocks with data and meta-data in reiser4.
 *
 * In particular, jnodes are used to track transactional information
 * associated with each block. Each znode contains jnode as ->zjnode field.
 *
 * Jnode stands for either Josh or Journall node.
 *
 */

#include "reiser4.h"

static kmem_cache_t *_jnode_slab = NULL;

/* Initialize static variables in this file. */
int
jnode_init_static (void)
{
	assert ("umka-168", _jnode_slab == NULL);

	_jnode_slab = kmem_cache_create ("jnode", sizeof (jnode),
					 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if (_jnode_slab == NULL) {
		goto error;
	}
	
	return 0;

 error:

	if (_jnode_slab != NULL) { kmem_cache_destroy (_jnode_slab); }
	return -ENOMEM;	
}

int
jnode_done_static (void)
{
	int ret = 0;

	if (_jnode_slab != NULL) {
		ret = kmem_cache_destroy (_jnode_slab);
		_jnode_slab = NULL;
	}

	return ret;
}

/* Initialize a jnode. */
/* Audited by: umka (2002.06.13) */
void
jnode_init (jnode *node)
{
	assert("umka-175", node != NULL);
	
	node->state = 0;
	node->level = 0;
	atomic_set (&node->d_count, 0);
	spin_lock_init (& node->guard);
	node->atom = NULL;
	capture_list_clean (node);
}

/* return already existing jnode of page */
jnode* 
jnode_by_page (struct page* pg)
{
	jnode *node;
	spinlock_t *lock;

	assert ("nikita-2066", pg != NULL);
	lock = page_to_jnode_lock (pg);
	spin_lock (lock);
	assert ("nikita-2068", PagePrivate (pg));
	node = (jnode*) pg->private;
	assert ("nikita-2067", node != NULL);
	spin_unlock (lock);
	return node;
}

static unsigned int jnode_page_hash( const jnode *node )
{
	return ( ( ( unsigned long ) node ) / sizeof *node ) % REISER4_JNODE_TO_PAGE_HASH_SIZE;
}

spinlock_t *jnode_to_page_lock( const jnode *node )
{
	int spin_ind;

	spin_ind = jnode_page_hash( node );
	return &get_current_super_private() -> j_to_p[ spin_ind ];
}

spinlock_t *page_to_jnode_lock( const struct page *page )
{
	return &get_super_private( page -> mapping -> host -> i_sb ) ->
		j_to_p[ jnode_page_hash( ( jnode * ) page -> private ) ];
}

/* exported functions to allocate/free jnode objects outside this file */
jnode * jalloc (void)
{
	jnode * jal = kmem_cache_alloc (_jnode_slab, GFP_KERNEL);
	return jal;
}

void jfree (jnode * node)
{
	assert ("zam-449", node != NULL);
	kmem_cache_free (_jnode_slab, node);
}

jnode * jnew (void)
{
	jnode * jal;

	jal = jalloc();

	if (jal == NULL) return NULL;

	jnode_init (jal);

	/* FIXME: not a strictly correct, but should help in avoiding of
	 * looking to missing znode-only fields */
	JF_SET (jal, ZNODE_UNFORMATTED);

	return jal;
}

/* Holding the jnode_ptr_lock, check whether the page already has a jnode and
 * if not, allocate one. */
jnode*
jnode_of_page (struct page* pg)
{
	/* FIXME: Note: The following code assumes page_size == block_size.
	 * When support for page_size > block_size is added, we will need to
	 * add a small per-page array to handle more than one jnode per
	 * page. */
	jnode *jal = NULL;
	spinlock_t *lock;
	
	assert("umka-176", pg != NULL);
	/* check that page is unformatted */
	assert ("nikita-2065", pg->mapping->host != 
		get_super_private (pg->mapping->host->i_sb)->fake);
	
 again:
	lock = page_to_jnode_lock (pg);
	spin_lock (lock);

	if ((jnode*) pg->private == NULL) {
		if (jal == NULL) {
			spin_unlock (lock);
			jal = jalloc();

			if (jal == NULL) {
				return NULL;
			}

			goto again;
		}

		/* FIXME: jnode_init doesn't take struct page argument, so
		 * znodes aren't having theirs set. */
		jnode_init (jal);

		jal->level = LEAF_LEVEL;

		jnode_attach_page_nolock (jal, pg);

		JF_SET (jal, ZNODE_UNFORMATTED);

		jal = NULL;
	}
	assert ("nikita-2046", ((jnode*) pg->private)->pg == pg);


	/* FIXME: This may be called from page_cache.c, read_in_formatted, which
	 * does is already synchronized under the page lock, but I imagine
	 * this will get called from other places, in which case the
	 * jnode_ptr_lock is probably still necessary, unless...
	 *
	 * If jnodes are unconditionally assigned at some other point, then
	 * this interface and lock not needed? */

	spin_unlock (lock);

	if (jal != NULL) {
		jfree(jal);
	}

	/*
	 * FIXME:NIKITA->JMACD possible race here: page is released and
	 * allocated again. All jnode_of_page() callers have to protect
	 * against this.  Josh says: Huh? What?  Nikita, example?
	 */
	return jref ((jnode*) pg->private);
}


/* FIXME-VS: change next two functions switching to support of blocksize !=
 * page cache size */
jnode * nth_jnode (struct page * page, int block)
{
	assert ("vs-695", PagePrivate (page) && page->private);
	assert ("vs-696", current_blocksize == (unsigned)PAGE_CACHE_SIZE);
	assert ("vs-697", block == 0);
	return (jnode *)page->private;
}


/* get next jnode of a page.
 * FIXME-VS: update this when more than one jnode per page will be allowed */
/* Audited by: umka (2002.06.13) */
jnode * next_jnode (jnode * node UNUSED_ARG)
{
	return 0;
}

/**
 * jref() - increase counter of references to jnode/znode (x_count)
 */
/* Audited by: umka (2002.06.11) */
jnode *jref (jnode *node)
{
	assert ("jmacd-508", (node != NULL) && ! IS_ERR (node));
	add_x_ref( node );
	return node;
}

/** block number of node */
/* Audited by: umka (2002.06.11) */
const reiser4_block_nr *jnode_get_block( const jnode *node /* jnode to
							    * query */ )
{
	assert( "nikita-528", node  != NULL );

/* As soon as we implement accessing nodes not stored on block devices
   (e.g. distributed reiserfs), then we need to replace this line with
   a call to a node plugin.

   Josh replies: why not extent the block number to be
   node_id/device/block_nr.  I don't think the concept of a block number
   changes in a distributed setting, but you will need a node method to get
   the block: likely we already have that.
*/
	return & node -> blocknr;
}

/* Audited by: umka (2002.06.15) */
void jnode_attach_page_nolock( jnode *node, struct page *pg )
{
	assert( "nikita-2060", node != NULL );
	assert( "nikita-2061", pg != NULL );

	assert( "nikita-2050", 
		( pg -> private == 0ul ) || 
		( pg -> private == ( unsigned long ) node ) );
	if( !PagePrivate( pg ) ) {
		assert( "nikita-2059", pg -> private == 0ul );
		pg -> private = ( unsigned long ) node;
		node -> pg  = pg;
		SetPagePrivate( pg );
		/* add reference to page */
		page_cache_get( pg );
	}
}

/* Audited by: umka (2002.06.15) */
void jnode_attach_page( jnode *node, struct page *pg )
{
	spinlock_t *lock;

	assert( "nikita-2047", node != NULL );
	assert( "nikita-2048", pg != NULL );

	trace_on( TRACE_PCACHE, "attach: node %p, page: %p\n", node, pg );

	lock = jnode_to_page_lock( node );
	spin_lock( lock );
	jnode_attach_page_nolock( node, pg );
	assert( "nikita-2183", ergo( pg != NULL,
				     lock == page_to_jnode_lock( pg ) ) );
	spin_unlock( lock );
}

void break_page_jnode_linkage( struct page *page, jnode *node )
{
	assert( "nikita-2063", page != NULL );
	assert( "nikita-2064", node != NULL );
	assert( "jmacd-20642", !PageDirty( page ) );

	trace_on( TRACE_PCACHE, "break page: %p\n", page );

	page -> private = 0ul;
	ClearPagePrivate( page );
	node -> pg = NULL;
}

void page_detach_jnode_nolock( jnode *node, struct page *page, spinlock_t *lock )
{
	assert( "nikita-2256", lock != NULL );

	spin_lock( lock );
	if( likely( ( node != NULL ) && ( page != NULL ) ) ) {
		assert( "nikita-2184", lock == jnode_to_page_lock( node ) );
		assert( "nikita-2185", lock == page_to_jnode_lock( page ) );
		break_page_jnode_linkage( page, node );
		spin_unlock( lock );
		page_cache_release( page );
	} else
		spin_unlock( lock );
}

void page_detach_jnode( struct page *page )
{
	spinlock_t *lock;

	assert( "nikita-2062", page != NULL );

	trace_on( TRACE_PCACHE, "detach page: %p\n", page );

	lock = page_to_jnode_lock( page );
	page_detach_jnode_nolock( ( jnode * ) page -> private, page, lock );
}

void jnode_detach_page( jnode *node )
{
	spinlock_t  *lock;

	assert( "nikita-2052", node != NULL );

	trace_on( TRACE_PCACHE, "detach jnode: %p\n", node );

	lock = jnode_to_page_lock( node );
	page_detach_jnode_nolock( node, jnode_page( node ), lock );
}

/** bump data counter on @node */
/* Audited by: umka (2002.06.11) */
void add_d_ref( jnode *node /* node to increase d_count of */ )
{
	assert( "nikita-1962", node != NULL );

	atomic_inc( &node -> d_count );
	ON_DEBUG( ++ lock_counters() -> d_refs );
}

/* jload/jwrite/junload give a bread/bwrite/brelse functionality for jnodes */
/* jnode ref. counter is missing, it doesn't matter for us because this
 * journal writer uses those jnodes exclusively by only one thread */
/* load content of jnode into memory in all places except cases of unformatted
 * nodes access  */


/* load jnode's data into memory using tree->read_node method */
int jload_and_lock( jnode *node )
{
	int result;

	spin_lock_jnode( node );

	reiser4_stat_znode_add( zload );
	add_d_ref( node );
	if( !jnode_is_loaded( node ) ) {
		reiser4_tree *tree;

		spin_unlock_jnode( node );

		tree = current_tree;

		/* load data... */
		assert( "nikita-1097", tree != NULL );
		assert( "nikita-1098", tree -> ops -> read_node != NULL );

		/*
		 * ->read_node() reads data from page cache. In any case we
		 * rely on proper synchronization in the underlying
		 * transport. Page reference counter is incremented and page is
		 * kmapped, it will kunmapped in zunload
		 */
		result = tree -> ops -> read_node( tree, node );
		reiser4_stat_znode_add( zload_read );

		if( likely( result >= 0 ) ) {
			ON_SMP( assert( "nikita-2075", 
					spin_jnode_is_locked( node ) ) );
			JF_SET( node, ZNODE_LOADED );
		} else
			jrelse_nolock( node );
	} else {
		assert( "nikita-2136", atomic_read( &node -> d_count ) > 1 );
		result = 1;
	}
	assert( "nikita-2135", ergo( result >= 0,
				     JF_ISSET( node, ZNODE_KMAPPED ) ) );

	return result;
}

/** just like jrelse, but assume jnode is already spin-locked */
void jrelse_nolock( jnode *node /* jnode to release references to */ )
{
	assert( "nikita-487", node != NULL );
	assert( "nikita-489", atomic_read( &node -> d_count ) > 0 );
	ON_SMP( assert( "nikita-1906", spin_jnode_is_locked( node ) ) );

	ON_DEBUG( -- lock_counters() -> d_refs );
	if( atomic_dec_and_test( &node -> d_count ) ) {
		reiser4_tree *tree;

		tree = current_tree;
		tree -> ops -> release_node( tree, node );
		JF_CLR( node, ZNODE_LOADED );
	}
}


/* A wrapper around tree->ops->drop_node method */
int jdrop (jnode * node)
{
	reiser4_tree * tree = current_tree;

	assert ("zam-602", node != NULL);
	assert ("zam-603", tree->ops != NULL);
	assert ("zam-604", tree->ops->drop_node != NULL);

	return tree->ops->drop_node (tree, node);
}

int jwait_io (jnode * node, int rw)
{
	struct page * page;
	int           result;

	assert ("zam-447", node != NULL);
	assert ("zam-448", jnode_page (node) != NULL);

	page = jnode_page (node);

	result = 0;
	if (rw == READ) {
		wait_on_page_locked (page);
	} else {
		assert ("nikita-2227", rw == WRITE);
		wait_on_page_writeback (page);
	}
	if (PageError(page))
		result = -EIO;

	return result;
}

#if REISER4_DEBUG

#define jnode_state_name( node, flag )			\
	( JF_ISSET( ( node ), ( flag ) ) ? ((#flag ## "|")+6) : "" )

/** debugging aid: output human readable information about @node */
/* Audited by: umka (2002.06.11) */
void info_jnode( const char *prefix /* prefix to print */, 
		 const jnode *node /* node to print */ )
{
	assert( "umka-068", prefix != NULL );
	
	if( node == NULL ) {
		info( "%s: null\n", prefix );
		return;
	}

	info( "%s: %p: state: %lu: [%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s], level: %i, block: %llu, pg: %p, ",
	      prefix, node, node -> state, 

	      jnode_state_name( node, ZNODE_LOADED ),
	      jnode_state_name( node, ZNODE_HEARD_BANSHEE ),
	      jnode_state_name( node, ZNODE_LEFT_CONNECTED ),
	      jnode_state_name( node, ZNODE_RIGHT_CONNECTED ),
	      jnode_state_name( node, ZNODE_ORPHAN ),
	      jnode_state_name( node, ZNODE_UNFORMATTED ),
	      jnode_state_name( node, ZNODE_CREATED ),
	      jnode_state_name( node, ZNODE_RELOC ),
	      jnode_state_name( node, ZNODE_WANDER ),
	      jnode_state_name( node, ZNODE_DIRTY ),
	      jnode_state_name( node, ZNODE_IS_DYING ),
	      jnode_state_name( node, ZNODE_KMAPPED ),
	      jnode_state_name( node, ZNODE_MAPPED ),
	      jnode_state_name( node, ZNODE_FLUSH_BUSY ),
	      jnode_state_name( node, ZNODE_FLUSH_QUEUED ),
	      
	      jnode_get_level( node ), *jnode_get_block( node ), jnode_page( node ) );
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
 * End:
 */
