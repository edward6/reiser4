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

/* The jnode_ptr_lock is a global spinlock used to protect the struct_page to
 * jnode mapping (i.e., it protects all struct_page_private fields).  It could
 * be a per-txnmgr spinlock instead. */
spinlock_t    _jnode_ptr_lock = SPIN_LOCK_UNLOCKED;

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
	spin_lock_init (& node->guard);
	node->atom = NULL;
	capture_list_clean (node);
}

/* return already existing jnode of page */
jnode* 
jnode_by_page (struct page* pg)
{
	jnode *node;

	assert ("nikita-2066", pg != NULL);
	spin_lock (& _jnode_ptr_lock);
	assert ("nikita-2068", PagePrivate (pg));
	node = (jnode*) pg->private;
	assert ("nikita-2067", node != NULL);
	spin_unlock (& _jnode_ptr_lock);
	return node;
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
	
	assert("umka-176", pg != NULL);
	/* check that page is unformatted */
	assert ("nikita-2065", pg->mapping->host != 
		get_super_private (pg->mapping->host->i_sb)->fake);
	
 again:
	spin_lock (& _jnode_ptr_lock);

	if ((jnode*) pg->private == NULL) {
		if (jal == NULL) {
			spin_unlock (& _jnode_ptr_lock);
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

	spin_unlock (& _jnode_ptr_lock);

	if (jal != NULL) {
		jfree(jal);
	}

	/*
	 * FIXME:NIKITA->JMACD possible race here: page is released and
	 * allocated again. All jnode_of_page() callers have to protect
	 * against this.  Josh says: Huh? What?
	 */
	return (jnode*) pg->private;
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

/* Increment to the jnode's reference counter. */
/* FIXME:NIKITA->JMACD comment is not exactly correct, because there is no
 * reference counter in jnode. */
/* Audited by: umka (2002.06.13) */
jnode *jref( jnode *node )
{
	assert("umka-177", node != NULL);
	
	if (! JF_ISSET (node, ZNODE_UNFORMATTED)) {
		return ZJNODE (zref (JZNODE (node)));
	} else {
		/* FIXME_JMACD: What to do here? */
		return node;
	}
}

/* Decrement the jnode's reference counter. */
/* FIXME:NIKITA->JMACD comment is not exactly correct, because there is no
 * reference counter in jnode. */
/* Audited by: umka (2002.06.13) */
void   jput( jnode *node )
{
	assert("umka-178", node != NULL);
	
	if (! JF_ISSET (node, ZNODE_UNFORMATTED)) {
		zput (JZNODE (node));
	} else {
		/* FIXME: */
	}
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
	assert( "nikita-2047", node != NULL );
	assert( "nikita-2048", pg != NULL );

	trace_on( TRACE_PCACHE, "attach: node %p, page: %p\n", node, pg );

	spin_lock( &_jnode_ptr_lock );
	jnode_attach_page_nolock( node, pg );
	spin_unlock( &_jnode_ptr_lock );
}

/* Audited by: umka (2002.06.15) */
void break_page_jnode_linkage( struct page *page, jnode *node )
{
	assert( "nikita-2063", page != NULL );
	assert( "nikita-2064", node != NULL );

	trace_on( TRACE_PCACHE, "break page: %p\n", page );

	page -> private = 0ul;
	ClearPagePrivate( page );
	node -> pg = NULL;
}

/* Audited by: umka (2002.06.15) */
jnode *page_detach_jnode( struct page *page )
{
	jnode *node;

	assert( "nikita-2062", page != NULL );

	trace_on( TRACE_PCACHE, "detach page: %p\n", page );

	spin_lock( &_jnode_ptr_lock );
	node = ( jnode * ) page -> private;
	if( node != NULL )
		break_page_jnode_linkage( page, node );
	spin_unlock( &_jnode_ptr_lock );
	page_cache_release( page );
	return node;
}

/* Audited by: umka (2002.06.15) */
void jnode_detach_page( jnode *node )
{
	struct page *page;

	assert( "nikita-2052", node != NULL );

	trace_on( TRACE_PCACHE, "detach jnode: %p\n", node );

	spin_lock( &_jnode_ptr_lock );
	page = jnode_page( node );
	if( page == NULL ) {
		spin_unlock( &_jnode_ptr_lock );
		return;
	}
	break_page_jnode_linkage( page, node );
	spin_unlock( &_jnode_ptr_lock );
	page_cache_release( page );
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

	info( "%s: %p: state: %lu: [%s%s%s%s%s%s%s%s%s%s%s%s], level: %i, pg: %p, ",
	      prefix, node, node -> state, 

	      jnode_state_name( node, ZNODE_LOADED ),
	      jnode_state_name( node, ZNODE_HEARD_BANSHEE ),
	      jnode_state_name( node, ZNODE_LEFT_CONNECTED ),
	      jnode_state_name( node, ZNODE_RIGHT_CONNECTED ),
	      jnode_state_name( node, ZNODE_NEW ),
	      jnode_state_name( node, ZNODE_ALLOC ),
	      jnode_state_name( node, ZNODE_RELOC ),
	      jnode_state_name( node, ZNODE_WANDER ),
	      jnode_state_name( node, ZNODE_DIRTY ),
	      jnode_state_name( node, ZNODE_WRITEOUT ),
	      jnode_state_name( node, ZNODE_IS_DYING ),
	      jnode_state_name( node, ZNODE_UNFORMATTED ),
	      
	      jnode_get_level( node ), jnode_page( node ) );
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
