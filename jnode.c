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
 * Jnode stands for either Josh or Journal node.
 *
 */

#include "reiser4.h"

static kmem_cache_t *_jnode_slab = NULL;

/* hash table support */

/** compare two jnode keys for equality. Used by hash-table macros */
static inline int jnode_key_eq( const jnode_key_t *k1, const jnode_key_t *k2 )
{
	assert( "nikita-2350", k1 != NULL );
	assert( "nikita-2351", k2 != NULL );

	return !memcmp( k1, k2, sizeof *k1 );
}

/** Hash jnode by its key (inode plus offset). Used by hash-table macros */
static inline __u32 jnode_key_hashfn( const jnode_key_t *key )
{
	__u32 hash;

	assert( "nikita-2352", key != NULL );

	/*
	 * FIXME-NIKITA Stupid, primitive and dubious hash function. Improve
	 * it.
	 */
	hash  = ( __u32 ) key -> mapping;
	hash /= sizeof( struct inode );
	hash ^= ( __u32 ) ( key -> index );
	return hash & ( REISER4_JNODE_HASH_TABLE_SIZE - 1 );
}

/** The hash table definition */
#define KMALLOC( size ) reiser4_kmalloc( ( size ), GFP_KERNEL )
#define KFREE( ptr, size ) reiser4_kfree( ptr, size )
TS_HASH_DEFINE( j, jnode, jnode_key_t, key.j,
		link.j, jnode_key_hashfn, jnode_key_eq );
#undef KFREE
#undef KMALLOC

/** call this to initialise jnode hash table */
int jnodes_tree_init( reiser4_tree *tree /* tree to initialise jnodes for */ )
{
	assert( "nikita-2359", tree != NULL );
	
	return j_hash_init( &tree -> jhash_table, 
			    REISER4_JNODE_HASH_TABLE_SIZE );
}

/** call this to destroy jnode hash table */
int jnodes_tree_done( reiser4_tree *tree /* tree to destroy jnodes for */ )
{
	j_hash_table  *jtable;
	jnode         *node;
	jnode         *next;
	int            killed;

	assert( "nikita-2360", tree != NULL );

	trace_if( TRACE_ZWEB, 
		  UNDER_SPIN_VOID( tree, tree, print_jnodes( "umount", tree ) ) );

	jtable = &tree -> jhash_table;
	spin_lock_tree( tree );

	do {
		killed = 0;
		for_all_in_htable( jtable, j, node, next ) {
			assert( "nikita-2361", !atomic_read( &node -> x_count ) );
			jdrop( node );
			++ killed;
			break;
		}
	} while( killed > 0 );

	spin_unlock_tree( tree );

	j_hash_done( &tree -> jhash_table );
	return 0;
}

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

	memset (node, 0, sizeof (jnode));
	node->state = 0;
	atomic_set (&node->d_count, 0);
	atomic_set (&node->x_count, 0);
	spin_lock_init (& node->guard);
	node->atom = NULL;
	capture_list_clean (node);

#if REISER4_DEBUG
	UNDER_SPIN_VOID 
		(tree, current_tree,
		 list_add (&node->jnodes, 
			   &get_current_super_private()->all_jnodes));
#endif
}

#define jprivate( page ) ( ( jnode * ) ( page ) -> private )

/* return already existing jnode of page */
jnode* 
jnode_by_page (struct page* pg)
{
	assert ("nikita-2066", pg != NULL);
	assert ("nikita-2400", PageLocked (pg));
	assert ("nikita-2068", PagePrivate (pg));
	assert ("nikita-2067", jprivate (pg) != NULL);
	return jprivate (pg);
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

	assert ("nikita-2422", !list_empty (&node->jnodes));

	ON_DEBUG (list_del_init (&node->jnodes));
	/*
	 * poison memory.
	 */
	ON_DEBUG(xmemset (node, 0xad, sizeof *node));
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
	jnode_set_type (jal, JNODE_UNFORMATTED_BLOCK);

	return jal;
}

/** look for jnode with given mapping and offset within hash table */
jnode *jlook (reiser4_tree *tree, 
	      struct address_space *mapping, unsigned long index)
{
	jnode_key_t  jkey;
	jnode       *node;

	assert( "nikita-2353", tree != NULL );
	assert( "nikita-2354", mapping != NULL );
	assert( "nikita-2355", spin_tree_is_locked( tree ) );

	jkey.mapping = mapping;
	jkey.index   = index;
	node = j_hash_find( &tree -> jhash_table, &jkey );
	if( node != NULL )
		/*
		 * protect @node from recycling
		 */
		jref( node );
	return node;
}

/**
 * jget() (a la zget() but for unformatted nodes). Returns (and possibly
 * creates) jnode corresponding to page @pg. jnode is attached to page and
 * inserted into jnode hash-table.
 */
jnode* jget (reiser4_tree *tree, struct page *pg)
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
	assert ("nikita-2394", PageLocked (pg));
 again:
	if (jprivate(pg) == NULL) {
		jnode *in_hash;
		/** check hash-table first */
		tree = tree_by_page (pg);
		spin_lock_tree (tree);
		in_hash = jlook (tree, pg->mapping, pg->index);
		if (in_hash != NULL) {
			assert ("nikita-2358", jnode_page (in_hash) == NULL);
			spin_unlock_tree (tree);
			UNDER_SPIN_VOID (jnode, in_hash,
					 jnode_attach_page_nolock (in_hash, pg));
			assert ("nikita-2356", 
				jnode_get_type (in_hash) == JNODE_UNFORMATTED_BLOCK);
		} else {
			j_hash_table *jtable;

			if (jal == NULL) {
				spin_unlock_tree (tree);
				jal = jnew();

				if (jal == NULL) {
					return ERR_PTR(-ENOMEM);
				}

				goto again;
			}

			jref (jal);

			jal->key.j.mapping = pg->mapping;
			jal->key.j.index   = pg->index;

			jtable = &tree->jhash_table;
			assert ("nikita-2357", 
				j_hash_find (jtable, &jal->key.j) == NULL);

			j_hash_insert (jtable, jal);
			spin_unlock_tree (tree);

			UNDER_SPIN_VOID (jnode, jal,
					 jnode_attach_page_nolock (jal, pg));
			jal = NULL;
		}
	} else
		jref (jprivate(pg));

	assert ("nikita-2046", jprivate(pg)->pg == pg);
	assert ("nikita-2364", jprivate(pg)->key.j.index == pg -> index);
	assert ("nikita-2365", jprivate(pg)->key.j.mapping == pg -> mapping);

	if (jal != NULL) {
		jfree(jal);
	}
	return jnode_by_page(pg);
}

jnode* jnode_of_page (struct page* pg)
{
	return jget (tree_by_page (pg), pg);
}

/** return jnode associated with page, possibly creating it. */
jnode *jfind( struct page *page )
{
	jnode *node;

	assert( "nikita-2417", page != NULL );
	assert( "nikita-2418", PageLocked( page ) );

	if( PagePrivate( page ) )
		node = jref( jprivate( page ) );
	else {
		/*
		 * otherwise it can only be unformatted---znode is never
		 * detached from the page.
		 */
		node = jnode_of_page( page );
	}
	return node;
}

/* FIXME-VS: change next two functions switching to support of blocksize !=
 * page cache size */
jnode * nth_jnode (struct page * page, int block)
{
	assert ("vs-695", PagePrivate (page) && page->private);
	assert ("vs-696", current_blocksize == (unsigned)PAGE_CACHE_SIZE);
	assert ("vs-697", block == 0);
	return jprivate (page);
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

/*
 * FIXME-NIKITA jnode<-{attach,detach}->page API is a mess (or, rather, a maze
 * of little different functions all alike). I'll clear it up once it
 * stabilizes.
 */

/* Audited by: umka (2002.06.15) */
void jnode_attach_page_nolock( jnode *node, struct page *pg )
{
	assert( "nikita-2060", node != NULL );
	assert( "nikita-2061", pg != NULL );

	assert( "nikita-2050", pg -> private == 0ul );
	assert( "nikita-2393", !PagePrivate( pg ) );

	assert( "nikita-2396", PageLocked( pg ) );
	assert( "nikita-2397", spin_jnode_is_locked( node ) );

	page_cache_get( pg );
	pg -> private = ( unsigned long ) node;
	node -> pg  = pg;
	SetPagePrivate( pg );
}

/* Audited by: umka (2002.06.15) */
void jnode_attach_page( jnode *node, struct page *page )
{
	assert( "nikita-2047", node != NULL );
	assert( "nikita-2048", page != NULL );

	assert( "nikita-2398", spin_jnode_is_not_locked( node ) );

	trace_on( TRACE_PCACHE, "attach: node %p, page: %p\n", node, page );

	lock_page( page );
	spin_lock_jnode( node );
	jnode_attach_page_nolock( node, page );
}

void page_detach_jnode_nolock( jnode *node, struct page *page )
{
	assert( "nikita-2256", node != NULL );
	assert( "nikita-2391", page != NULL );
	assert( "nikita-2389", spin_jnode_is_locked( node ) );
	assert( "nikita-2390", PageLocked( page ) );
	assert( "jmacd-20642", !PageDirty( page ) );

	page_clear_jnode_nolock( page, node );

	spin_unlock_jnode( node );
	unlock_page( page );
	page_cache_release( page );
}

void page_clear_jnode( struct page *page )
{
	assert( "nikita-2410", page != NULL );
	assert( "nikita-2411", PageLocked( page ) );

	if( PagePrivate( page ) ) {
		jnode *node;

		node = jnode_by_page( page );
		UNDER_SPIN_VOID( jnode, node,
				 page_clear_jnode_nolock( page, node ) );
		page_cache_release( page );
	}
}

void page_clear_jnode_nolock( struct page *page, jnode *node )
{
	assert( "nikita-2424", page != NULL );
	assert( "nikita-2425", PageLocked( page ) );
	assert( "nikita-2426", node != NULL );
	assert( "nikita-2427", spin_jnode_is_locked( node ) );
	assert( "nikita-2428", PagePrivate( page ) );

	page -> private = 0ul;
	ClearPagePrivate( page );
	node -> pg = NULL;
}

void page_detach_jnode( struct page *page )
{
	assert( "nikita-2062", page != NULL );
	assert( "nikita-2392", PageLocked( page ) );

	trace_on( TRACE_PCACHE, "detach page: %p\n", page );
	if( PagePrivate( page ) ) {
		jnode *node;

		node = jprivate( page );
		assert( "nikita-2399", spin_jnode_is_not_locked( node ) );
		spin_lock_jnode( node );
		page_detach_jnode_nolock( node, page );
	} else
		unlock_page( page );
}

void page_detach_jnode_lock( struct page *page, 
			     struct address_space *mapping, unsigned long index )
{
	assert( "nikita-2395", page != NULL );

	lock_page( page );
	if( ( page -> mapping == mapping ) && ( page -> index == index ) )
		page_detach_jnode( page );
	else
		unlock_page( page );
}

/**
 * return @node page locked.
 *
 * Locking ordering requires that one first takes page lock and afterwards
 * spin lock on node attached to this page. Sometimes it is necessary to go in
 * the opposite direction. This is done through standard trylock-and-release
 * loop.
 */
struct page *jnode_lock_page( jnode *node )
{
	struct page *page;

	assert( "nikita-2052", node != NULL );
	assert( "nikita-2401", spin_jnode_is_not_locked( node ) );

	while( 1 ) {

		spin_lock_jnode( node );
		page = node -> pg;
		if( page == NULL ) {
			break;
		}

		/*
		 * no need to page_cache_get( page ) here, because page cannot
		 * be evicted from memory without detaching it from jnode and
		 * this requires spin lock on jnode that we already hold.
		 */
		if( !TestSetPageLocked( page ) ) {
			/*
			 * We won a lock on jnode page, proceed.
			 */
			break;
		}

		/*
		 * Page is locked by someone else.
		 */
		page_cache_get( page );
		spin_unlock_jnode( node );
		wait_on_page_locked( page );
		/*
		 * it is possible that page was detached from jnode and
		 * returned to the free pool, or re-assigned while we were
		 * waiting on locked bit. This will be rechecked on the next
		 * loop iteration.
		 */
		page_cache_release( page );

		/*
		 * try again
		 */
	}
	return page;
}

/** bump data counter on @node */
/* Audited by: umka (2002.06.11) */
void add_d_ref( jnode *node /* node to increase d_count of */ )
{
	assert( "nikita-1962", node != NULL );

	atomic_inc( &node -> d_count );
	ON_DEBUG_CONTEXT( ++ lock_counters() -> d_refs );
}

/* jload/jwrite/junload give a bread/bwrite/brelse functionality for jnodes */
/* load content of jnode into memory in all places except cases of unformatted
 * nodes access  */

static int page_filler( void *arg, struct page *page )
{
	jnode *node;
	int    result;

	node = arg;

	assert( "nikita-2369", 
		page -> mapping == jnode_ops( node ) -> mapping( node ) );

	/*
	 * add reiser4 decorations to the page, if they aren't in place:
	 * pointer to jnode, whatever.
	 * 
	 * We are under page lock now, so it can be used as synchronization.
	 */
	UNDER_SPIN_VOID( jnode, node,
			 jnode_attach_page_nolock( node, page ) );
	result = page -> mapping -> a_ops -> readpage( NULL, page );
	/*
	 * on error, detach jnode from page
	 */
	if( unlikely( result != 0 ) ) {
		warning( "nikita-2416", "->readpage failed: %i", result );
		page_detach_jnode( page );
	}
	return result;
}

static inline int jparse( jnode *node )
{
	int result;

	assert( "nikita-2466", node != NULL );

	result = 0;
	spin_lock_jnode( node );
	if( !jnode_is_loaded( node ) ) {
		result = jnode_ops( node ) -> parse( node );
		if( likely( result == 0 ) )
			JF_SET( node, JNODE_LOADED );
	}
	spin_unlock_jnode( node );
	return result;
}

/** helper function used by jload() */
static inline void load_page( struct page *page )
{
	page_cache_get( page );
	mark_page_accessed( page );
	kmap( page );
}

/* load jnode's data into memory using read_cache_page() */
int jload( jnode *node )
{
	int          result;
	struct page *page;

	result = 0;
	reiser4_stat_znode_add( zload );
	add_d_ref( node );
	if( !jnode_is_loaded( node ) ) {
		jnode_plugin *jplug;

		/*
		 * read data from page cache. Page reference counter is
		 * incremented and page is kmapped, it will kunmapped in
		 * zrelse
		 */
		trace_on( TRACE_PCACHE, "read node: %p\n", node );

		jplug = jnode_ops( node );

		/*
		 * Our initial design was to index pages with formatted data
		 * by their block numbers. One disadvantage of this is that
		 * such setup makes relocation harder to implement: when tree
		 * node is relocated we need to re-index its data in a page
		 * cache. To avoid data copying during this re-indexing it was
		 * decided that first version of reiser4 will only support
		 * block size equal to PAGE_CACHE_SIZE.
		 *
		 * But another problem came up: our block numbers are 64bit
		 * and pages are indexed by 32bit ->index. Moreover:
		 *
		 *  - there is strong opposition for enlarging ->index field
		 *  (and for good reason: size of struct page is critical,
		 *  because there are so many of them).
		 *
		 *  - our "unallocated" block numbers have highest bit set,
		 *  which makes 64bit block number support essential
		 *  independently of device size.
		 *
		 * Code below uses jnode _address_ as page index. This has
		 * following advantages:
		 *
		 *  - relocation is simplified
		 *
		 *  - if ->index is jnode address, than ->private is free for
		 *  use. It can be used to store some jnode data making it
		 *  smaller (not yet implemented). Pointer to atom?
		 *
		 */
		page = UNDER_SPIN( jnode, node, node -> pg );
		/*
		 * subtle locking point: ->pg pointer is protected by jnode
		 * spin lock, but it is safe to release spin lock here,
		 * because page can be detached from jnode only when ->d_count
		 * is 0, and JNODE_LOADED is not set.
		 */
		if( page != NULL ) {
			JF_SET( node, JNODE_LOADED );
			load_page( page );
		} else {
			page = read_cache_page( jplug -> mapping( node ),
						jplug -> index( node ), 
						page_filler, node );
			if( !IS_ERR( page ) ) {
				wait_on_page_locked( page );
				kmap( page );
				/*
				 * It is possible (however unlikely) that page
				 * was concurrently released (by flush or
				 * shrink_cache()), page is still in a page
				 * cache and up-to-date, but jnode was already
				 * detached from it.
				 */
				if( unlikely( node -> pg == NULL ) ) {
					jnode_attach_page( node, page );
					spin_unlock_jnode( node );
					unlock_page( page );
				}
				if( PageUptodate( page ) ) {
					result = jparse( node );
					/*
					 * if parsing failed, detach jnode
					 * from page.
					 */
					if( unlikely( result != 0 ) ) {
						page = jnode_lock_page( node );
						assert( "nikita-2467", page );
						page_detach_jnode_nolock( node, 
									  page );
					}
				} else
					result = -EIO;
			} else
				result = PTR_ERR( page );

			if( unlikely( result != 0 ) )
				jrelse( node );
		}
	} else {
		struct page *page;

		page = jnode_page( node );
		assert( "nikita-2348", page != NULL );
		load_page( page );
	}
	return result;
}

/** call node plugin to initialise newly allocated node. */
int jinit_new( jnode *node /* jnode to initialise */ )
{
	int           result;
	struct page  *page;
	jnode_plugin *jplug;

	assert( "nikita-1234", node != NULL );

	add_d_ref( node );
	jplug = jnode_ops( node );
	page = grab_cache_page( jplug -> mapping( node ), 
				jplug -> index( node ) );
	if( page != NULL ) {
		SetPageUptodate( page );
		UNDER_SPIN_VOID( jnode, node,
				 jnode_attach_page_nolock( node, page ) );
		unlock_page( page );
		kmap( page );
		result = 0;
		spin_lock_jnode( node );
		if( likely( !jnode_is_loaded( node ) ) ) {
			JF_SET( node, JNODE_LOADED );
			JF_SET( node, JNODE_CREATED );
			assert( "nikita-1235", jnode_is_loaded( node ) );
			result = jplug -> init( node );
			if( unlikely( result != 0 ) ) {
				JF_CLR( node, JNODE_LOADED );
				JF_CLR( node, JNODE_CREATED );
			}
		}
		spin_unlock_jnode( node );
	} else
		result = -ENOMEM;

	if( unlikely( result != 0 ) )
		jrelse( node );
	return result;
}

/** just like jrelse, but assume jnode is already spin-locked */
void jrelse_nolock( jnode *node /* jnode to release references to */ )
{
	struct page *page;

	assert( "nikita-487", node != NULL );
	assert( "nikita-489", atomic_read( &node -> d_count ) > 0 );
	assert( "nikita-1906", spin_jnode_is_locked( node ) );

	ON_DEBUG_CONTEXT( -- lock_counters() -> d_refs );

	trace_on( TRACE_PCACHE, "release node: %p\n", node );

	page = jnode_page( node );
	if( page != NULL ) {
		kunmap( page );
		page_cache_release( page );
	}

	if( atomic_dec_and_test( &node -> d_count ) )
		/*
		 * FIXME it is crucial that we first decrement ->d_count and
		 * only later clear JNODE_LOADED bit. I hope that
		 * atomic_dec_and_test() implies memory barrier (and
		 * optimization barrier, of course).
		 */
		JF_CLR( node, JNODE_LOADED );
}

int jnode_try_drop( jnode *node )
{
	assert( "nikita-2491", node != NULL );

	/*
	 * first do lazy check without taking tree lock.
	 */
	if( ( atomic_read( &node -> x_count ) > 0 ) ||
	    ( jnode_is_znode( node ) && 
	      atomic_read( &JZNODE( node ) -> c_count ) > 0 ) ||
	    UNDER_SPIN( jnode, node, node -> pg ) )
		return -EBUSY;
	/*
	 * FIXME-NIKITA not finished
	 */
}

/**
 * jput() - decrement x_count reference counter on znode.
 *
 * Count may drop to 0, jnode stays in cache until memory pressure causes the
 * eviction of its page. The c_count variable also ensures that children are
 * pressured out of memory before the parent. The jnode remains hashed as
 * long as the VM allows its page to stay in memory, and then we force its
 * children out first? There is no jcache_shrink() yet.
 */
void jput (jnode *node)
{
	trace_stamp (TRACE_ZNODES);

	assert ("jmacd-509", node != NULL);
	assert ("jmacd-510", atomic_read (& node->x_count) > 0);
	assert ("jmacd-511", atomic_read (& node->d_count) >= 0);
	assert ("jmacd-572", ergo (jnode_is_znode (node),
				   atomic_read (& JZNODE (node)->c_count) >= 0));
	ON_DEBUG_CONTEXT (-- lock_counters() -> x_refs);

	if (atomic_dec_and_test (& node->x_count)) {
		if (JF_ISSET (node, JNODE_HEARD_BANSHEE))
			/*
			 * node is removed from the tree.
			 */
			jdelete (node);
	}
}

/** 
 * jdelete() -- Remove jnode from the tree
 */
int jdelete( jnode *node /* jnode to finish with */ )
{
	struct page  *page;
	int           result;
	reiser4_tree *tree;

	trace_stamp( TRACE_ZNODES );
	assert( "nikita-467", node != NULL );
	assert( "nikita-2123", JF_ISSET( node, JNODE_HEARD_BANSHEE ) );

	trace_on( TRACE_PCACHE, "delete node: %p\n", node );

	page = jnode_lock_page( node );
	assert( "nikita-2402", spin_jnode_is_locked( node ) );
	if( atomic_read( &node -> d_count ) > 0 ) {
		/*
		 * see comment in jdrop().
		 */
		spin_unlock_jnode( node );
		if( page != NULL )
			unlock_page( page );
		return -EAGAIN;
	}

	tree = current_tree;

	spin_lock_tree( tree );
	if( atomic_read( &node -> x_count ) > 0 ) {
		spin_unlock_tree( tree );
		return -EAGAIN;
	}

	if( page != NULL ) {
		assert( "nikita-2181", PageLocked( page ) );
		ClearPageDirty( page );
		ClearPageUptodate( page );
		remove_inode_page( page );
		page_detach_jnode_nolock( node, page );
		page_cache_release( page );
	} else
		spin_unlock_jnode( node );

	result = jnode_ops( node ) -> delete( node, tree );
	spin_unlock_tree( tree );
	return result;
}

/**
 * drop jnode on the floor.
 *
 * Return value:
 *
 *  -EBUSY:  failed to drop jnode, because there are still references to it
 *
 *  0:       successfully dropped jnode
 *
 */
int jdrop_in_tree( jnode *node, reiser4_tree *tree, int drop_page_p )
{
	struct page  *page;

	assert( "zam-602", node != NULL );
	assert( "nikita-2362", spin_tree_is_locked( tree ) );
	assert( "nikita-2403", !JF_ISSET( node, JNODE_HEARD_BANSHEE ) );

	/* still in use? */
	if( atomic_read( &node -> x_count ) > 0 )
		return -EBUSY;

	spin_unlock_tree( tree );

	trace_on( TRACE_PCACHE, "drop node: %p\n", node );

	page = jnode_lock_page( node );
	assert( "nikita-2405", spin_jnode_is_locked( node ) );

	if( atomic_read( &node -> d_count ) > 0 ) {
		/*
		 * updates to ->d_count are not protected by any lock by
		 * themselves, but change of ->d_count from 0 to 1 (in jload)
		 * passes through acquiring of page lock and jnode spin
		 * lock. At this moment we hold both locks.
		 */
		spin_unlock_jnode( node );
		if( page != NULL )
			unlock_page( page );
		spin_lock_tree( tree );
		return -EBUSY;
	}

	spin_lock_tree( tree );

	/* reference was acquired by other thread. */
	if( atomic_read( &node -> x_count ) > 0 )
		return -EBUSY;

	assert( "nikita-2488", page == node -> pg );
	if( page != NULL ) {
		if( drop_page_p ) {
			assert( "nikita-2126", !PageDirty( page ) );
			assert( "nikita-2127", PageUptodate( page ) );
			assert( "nikita-2181", PageLocked( page ) );
			remove_inode_page( page );
			page_detach_jnode_nolock( node, page );
			page_cache_release( page );
		} else {
			spin_unlock_jnode( node );
			unlock_page( page );
			return -EBUSY;
		}
	} else
		spin_unlock_jnode( node );

	return jnode_ops( node ) -> remove( node, tree );
}

/**
 * This function frees jnode "if possible". In particular, [dcx]_count has to
 * be 0 (where applicable). 
 */
void jdrop (jnode * node)
{
	jdrop_in_tree (node, current_tree, 1);
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

#define DEATH_QUEUE_SIZE (32)

/**
 * shrink jnode (and znode) hash tables. This is not very important as of now,
 * because reiser4_releasepage() would drop jnode when releasing page.
 */
int prune_jcache( int goal, int to_scan )
{
	int            recycled;
	int            killed;
	j_hash_table  *jtable;
	jnode         *node;
	jnode         *next;
	reiser4_tree  *tree;

	assert( "nikita-2484", goal >= 0 );

	tree = current_tree;
	jtable = &tree -> jhash_table;
	recycled = 0;

	do {
		killed = 0;
		spin_lock_tree( tree );

		for_all_in_htable( jtable, j, node, next ) {
			if( ! -- to_scan )
				break;

			if( next != NULL )
				jref( next );

			if( !JF_ISSET( node, JNODE_HEARD_BANSHEE ) ) {
				/*
				 * jdrop_in_tree() might schedule and release
				 * tree spin lock, but @next is safe, because
				 * of jref().
				 */
				killed += !jdrop_in_tree( node, tree, 0 );
			} else {
				spin_unlock_tree( tree );
				killed += !jdelete( node );
				spin_lock_tree( tree );
			}
			/*
			 * don't want to use jput(), because @next may already
			 * heard banshee
			 */
			if( next != NULL )
				atomic_dec( &next -> x_count );
		}
		spin_unlock_tree( tree );
		recycled += killed;
	} while( ( recycled < goal ) && ( killed > 0 ) && to_scan );
	return recycled;
}

jnode_type jnode_get_type( const jnode *node )
{
	static const unsigned long state_mask = 
		( 1 << JNODE_TYPE_1 ) | 
		( 1 << JNODE_TYPE_2 ) | ( 1 << JNODE_TYPE_3 );

	static jnode_type mask_to_type[] = {
		/*  JNODE_TYPE_3 : JNODE_TYPE_2 : JNODE_TYPE_1 */
		
		/* 000 */
		[ 0 ] = JNODE_FORMATTED_BLOCK,
		/* 001 */
		[ 1 ] = JNODE_UNFORMATTED_BLOCK,
		/* 010 */
		[ 2 ] = JNODE_BITMAP,
		/* 011 */
		[ 3 ] = JNODE_LAST_TYPE, /* invalid */
		/* 100 */
		[ 4 ] = JNODE_LAST_TYPE, /* invalid */
		/* 101 */
		[ 5 ] = JNODE_LAST_TYPE, /* invalid */
		/* 110 */
		[ 6 ] = JNODE_IO_HEAD,
		/* 111 */
		[ 7 ] = JNODE_LAST_TYPE, /* invalid */
	};

	/*
	 * FIXME-NIKITA atomicity?
	 */
	return mask_to_type[ ( node -> state & state_mask ) >> JNODE_TYPE_1 ];
}

void jnode_set_type( jnode * node, jnode_type type )
{
	static unsigned long type_to_mask[] = {
		[JNODE_UNFORMATTED_BLOCK] = 1,
		[JNODE_FORMATTED_BLOCK]   = 0,
		[JNODE_BITMAP]            = 2,
		[JNODE_IO_HEAD]           = 6
	};

	assert ("zam-647", type < JNODE_LAST_TYPE);

	node -> state &= ((1UL << JNODE_TYPE_1) - 1);
	node -> state |= (type_to_mask[type] << JNODE_TYPE_1);
}


static int noparse( jnode *node UNUSED_ARG)
{
	return 0;
}

static struct address_space *jnode_mapping( const jnode *node )
{
	return node -> key.j.mapping;
}

static unsigned long jnode_index( const jnode *node )
{
	return node -> key.j.index;
}

static int jnode_remove_op( jnode *node, reiser4_tree *tree )
{
	/* remove jnode from hash-table */
	j_hash_remove( &tree -> jhash_table, node );
	jfree( node );
	return 0;
}

static struct address_space *znode_mapping( const jnode *node UNUSED_ARG )
{
	return get_super_fake( reiser4_get_current_sb() ) -> i_mapping;
}

static unsigned long znode_index( const jnode *node )
{
	return ( unsigned long ) node;
}

extern int zparse( znode *node );

static int znode_parse( jnode *node )
{
	return zparse( JZNODE( node ) );
}

extern void znode_remove( znode *node, reiser4_tree *tree );
static int znode_delete_op( jnode *node, reiser4_tree *tree )
{
	znode *z;

	assert( "nikita-2128", spin_tree_is_locked( tree ) );
	assert( "vs-898", JF_ISSET( node, JNODE_HEARD_BANSHEE ) );

	z = JZNODE( node );
	assert( "vs-899", atomic_read( &z -> c_count ) == 0 );

	/*
	 * delete znode from sibling list.
	 */
	sibling_list_remove( z );

	znode_remove( z, tree );
	zfree( z );
	return 0;
}

static int znode_remove_op( jnode *node, reiser4_tree *tree )
{
	znode *z;

	assert( "nikita-2128", spin_tree_is_locked( tree ) );
	z = JZNODE( node );

	if( atomic_read( &z -> c_count ) == 0 ) {
		/*
		 * detach znode from sibling list.
		 */
		sibling_list_drop( z );
		/*
		 * this is called with tree spin-lock held, so call
		 * znode_remove() directly (rather than znode_lock_remove()).
		 */
		znode_remove( z, tree );
		zfree( z );
		return 0;
	}
	return -EBUSY;
}

static int znode_init( jnode *node )
{
	znode *z;

	z = JZNODE( node );
	return node_plugin_by_node( z ) -> init( z );
}

static int no_hook( jnode *node UNUSED_ARG, 
		    struct page *page UNUSED_ARG, int rw UNUSED_ARG )
{
	return 1;
}

static int other_remove_op( jnode *node, reiser4_tree *tree UNUSED_ARG )
{
	jfree( node );
	return 0;
}

extern int znode_io_hook( jnode *node, struct page *page, int rw );

reiser4_plugin jnode_plugins[ JNODE_LAST_TYPE ] = {
	[ JNODE_UNFORMATTED_BLOCK ] = {
		.jnode = {
			.h = {
				.type_id = REISER4_JNODE_PLUGIN_TYPE,
				.id      = JNODE_UNFORMATTED_BLOCK,
				.pops    = NULL,
				.label   = "unformatted",
				.desc    = "unformatted node",
				.linkage = TS_LIST_LINK_ZERO
			},
			.init    = noparse,
			.parse   = noparse,
			.remove  = jnode_remove_op,
			.delete  = jnode_remove_op,
			.mapping = jnode_mapping,
			.index   = jnode_index,
			.io_hook = no_hook
		}
	},
	[ JNODE_FORMATTED_BLOCK ] = {
		.jnode = {
			.h = {
				.type_id = REISER4_JNODE_PLUGIN_TYPE,
				.id      = JNODE_FORMATTED_BLOCK,
				.pops    = NULL,
				.label   = "formatted",
				.desc    = "formatted tree node",
				.linkage = TS_LIST_LINK_ZERO
			},
			.init    = znode_init,
			.parse   = znode_parse,
			.remove  = znode_remove_op,
			.delete  = znode_delete_op,
			.mapping = znode_mapping,
			.index   = znode_index,
			.io_hook = znode_io_hook
		}
	},
	[ JNODE_BITMAP ] = {
		.jnode = {
			.h = {
				.type_id = REISER4_JNODE_PLUGIN_TYPE,
				.id      = JNODE_BITMAP,
				.pops    = NULL,
				.label   = "bitmap",
				.desc    = "bitmap node",
				.linkage = TS_LIST_LINK_ZERO
			},
			.init    = noparse,
			.parse   = noparse,
			.remove  = other_remove_op,
			.delete  = other_remove_op,
			.mapping = znode_mapping,
			.index   = znode_index,
			.io_hook = no_hook
		}
	},
	[ JNODE_IO_HEAD ] = {
		.jnode = {
			.h = {
				.type_id = REISER4_JNODE_PLUGIN_TYPE,
				.id      = JNODE_IO_HEAD,
				.pops    = NULL,
				.label   = "io head",
				.desc    = "io head",
				.linkage = TS_LIST_LINK_ZERO
			},
			.init    = noparse,
			.parse   = noparse,
			.remove  = other_remove_op,
			.delete  = other_remove_op,
			.mapping = znode_mapping,
			.index   = znode_index,
			.io_hook = no_hook
		}
	}
};

jnode_plugin *jnode_ops_of( const jnode_type type )
{
	assert( "nikita-2367", type < JNODE_LAST_TYPE );
	return jnode_plugin_by_id( ( reiser4_plugin_id ) type );
}

jnode_plugin *jnode_ops( const jnode *node )
{
	assert( "nikita-2366", node != NULL );

	return jnode_ops_of( jnode_get_type( node ) );
}

/*
 * IO head jnode implementation; The io heads are simple j-nodes with limited
 * functionality (these j-nodes are not in any hash table) just for reading
 * from and writing to disk.
 */

jnode * alloc_io_head (const reiser4_block_nr * block)
{
	jnode * jal = jalloc();

	if (jal != NULL) {
		jnode_init      (jal);
		jnode_set_type  (jal, JNODE_IO_HEAD);
		jnode_set_block (jal, block);
	}

	jref (jal);

	return jal;
}

void drop_io_head (jnode * node)
{
	reiser4_tree * tree = current_tree;

	assert ("zam-648", jnode_get_type(node) == JNODE_IO_HEAD);

	jput (node);

	UNDER_SPIN_VOID (tree, tree, jdrop(node));
}

/* protect keep jnode data from reiser4_releasepage()  */
void pin_jnode_data (jnode * node)
{
	assert ("zam-671", jnode_page (node) != NULL);
	page_cache_get (jnode_page(node));
}

/* make jnode data free-able again */
void unpin_jnode_data (jnode * node)
{
	assert ("zam-672", jnode_page (node) != NULL);
	page_cache_release (jnode_page (node));
}

#if REISER4_DEBUG

const char *jnode_type_name( jnode_type type )
{
	switch( type ) {
	case JNODE_UNFORMATTED_BLOCK:
		return "unformatted";
	case JNODE_FORMATTED_BLOCK:
		return "formatted";
	case JNODE_BITMAP:
		return "bitmap";
	case JNODE_IO_HEAD:
		return "io head";
	case JNODE_LAST_TYPE:
		return "last";
	default: {
		static char unknown[ 30 ];

		sprintf( unknown, "unknown %i", type );
		return unknown;
	}
	}
}

#define jnode_state_name( node, flag )			\
	( JF_ISSET( ( node ), ( flag ) ) ? ((#flag "|")+6) : "" )

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

	info( "%s: %p: state: %lx: [%s%s%s%s%s%s%s%s%s%s%s%s%s], level: %i, block: %llu, d_count: %d, x_count: %d, pg: %p, type: %s, ",
	      prefix, node, node -> state, 

	      jnode_state_name( node, JNODE_LOADED ),
	      jnode_state_name( node, JNODE_HEARD_BANSHEE ),
	      jnode_state_name( node, JNODE_LEFT_CONNECTED ),
	      jnode_state_name( node, JNODE_RIGHT_CONNECTED ),
	      jnode_state_name( node, JNODE_ORPHAN ),
	      jnode_state_name( node, JNODE_CREATED ),
	      jnode_state_name( node, JNODE_RELOC ),
	      jnode_state_name( node, JNODE_WANDER ),
	      jnode_state_name( node, JNODE_DIRTY ),
	      jnode_state_name( node, JNODE_IS_DYING ),
	      jnode_state_name( node, JNODE_MAPPED ),
	      jnode_state_name( node, JNODE_FLUSH_QUEUED ),
	      jnode_state_name( node, JNODE_DROP ),

	      jnode_get_level( node ), *jnode_get_block( node ),
	      atomic_read( &node -> d_count ), atomic_read( &node -> x_count ),
	      jnode_page( node ), jnode_type_name( jnode_get_type( node ) ) );
	if( jnode_is_unformatted( node ) ) {
		info( "inode: %li, index: %lu, ", 
		      node -> key.j.mapping -> host -> i_ino, 
		      node -> key.j.index );
	}
}

/** this is cut-n-paste replica of print_znodes() */
void print_jnodes( const char *prefix, reiser4_tree *tree )
{
	jnode        *node;
	jnode        *next;
	j_hash_table *htable;
	int           tree_lock_taken;

	if( tree == NULL )
		tree = current_tree;

	/*
	 * this is debugging function. It can be called by reiser4_panic()
	 * with tree spin-lock already held. Trylock is not exactly what we
	 * want here, but it is passable.
	 */
	tree_lock_taken = spin_trylock_tree( tree );
	htable = &tree -> jhash_table;

	for_all_in_htable( htable, j, node, next ) {
		info_jnode( prefix, node );
		info( "\n" );
	}
	if( tree_lock_taken )
		spin_unlock_tree( tree );
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
