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
TS_HASH_DEFINE( j, jnode, jnode_key_t, key, 
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
	jnode       **bucket;
	jnode        *node;
	jnode        *next;

	assert( "nikita-2360", tree != NULL );

	trace_if( TRACE_ZWEB, 
		  ({
			  spin_lock_tree( tree );
			  print_jnodes( "umount", tree );
			  spin_unlock_tree( tree );
		  }) );

	spin_lock_tree( tree );

	for_all_ht_buckets( &tree -> jhash_table, bucket ) {
		for_all_in_bucket( bucket, node, next, link.j ) {
			/*
			 * FIXME debugging output
			 */
			if( atomic_read( &node -> x_count ) != 0 )
				info_jnode( "busy on umount", node );
			assert( "nikita-2361", 
				atomic_read( &node -> x_count ) == 0 );
			jdrop( node );
		}
	}

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
	JF_SET (jal, ZNODE_UNFORMATTED);

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
	ON_SMP( assert( "nikita-2355", spin_tree_is_locked( tree ) ) );

	jkey.mapping = mapping;
	jkey.index   = index;
	node = j_hash_find( &tree -> jhash_table, &jkey );
	if( node != NULL )
		/*
		 * protect @in_hash from recycling
		 */
		jref( node );
	return node;
}

#define jprivate( page ) ( ( jnode * ) ( page ) -> private )

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
	spinlock_t *lock;
	
	assert("umka-176", pg != NULL);
	/* check that page is unformatted */
	assert ("nikita-2065", pg->mapping->host != 
		get_super_private (pg->mapping->host->i_sb)->fake);
	
 again:
	lock = page_to_jnode_lock (pg);
	spin_lock (lock);

	if (jprivate(pg) == NULL) {
		jnode *in_hash;
		/** check hash-table first */
		tree = &get_super_private (pg->mapping->host->i_sb)->tree;
		spin_lock_tree (tree);
		in_hash = jlook (tree, pg->mapping, pg->index);
		if (in_hash != NULL) {
			assert ("nikita-2358", jnode_page (in_hash) == NULL);
			jnode_attach_page_nolock (in_hash, pg);
			assert ("nikita-2356", JF_ISSET (in_hash, 
							 ZNODE_UNFORMATTED));
		} else {
			j_hash_table *jtable;

			if (jal == NULL) {
				spin_unlock_tree (tree);
				spin_unlock (lock);
				jal = jalloc();

				if (jal == NULL) {
					return ERR_PTR(-ENOMEM);
				}

				goto again;
			}

			jnode_init (jal);
			jref (jal);

			jnode_attach_page_nolock (jal, pg);

			JF_SET (jal, ZNODE_UNFORMATTED);

			jal->key.mapping = pg->mapping;
			jal->key.index   = pg->index;

			jtable = &tree->jhash_table;
			assert ("nikita-2357", 
				j_hash_find (jtable, &jal->key) == NULL);
			j_hash_insert (jtable, jal);
			jal = NULL;
		}
		spin_unlock_tree (tree);
	} else
		jref (jprivate(pg));

	assert ("nikita-2046", jprivate(pg)->pg == pg);
	assert ("nikita-2364", jprivate(pg)->key.index == pg -> index);
	assert ("nikita-2365", jprivate(pg)->key.mapping == pg -> mapping);

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
	return jprivate(pg);
}

jnode* jnode_of_page (struct page* pg)
{
	return jget (&get_super_private (pg->mapping->host->i_sb)->tree, pg);
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
		page_cache_get( pg );
		pg -> private = ( unsigned long ) node;
		node -> pg  = pg;
		SetPagePrivate( pg );
		/* add reference to page */
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
	jnode_attach_page( node, page );
	result = page -> mapping -> a_ops -> readpage( NULL, page );
	/*
	 * on error, detach jnode from page
	 */
	if( unlikely( result != 0 ) )
		jnode_detach_page( node );
	return result;
}


/* load jnode's data into memory using read_cache_page() */
int jload( jnode *node )
{
	int          result;
	struct page *page;

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
		page = read_cache_page( jplug -> mapping( node ),
					jplug -> index( node ), 
					page_filler, node );
		if( !IS_ERR( page ) ) {
			wait_on_page_locked( page );
			kmap( page );
			if( PageUptodate( page ) )
				/*
				 * here neither jnode nor page are protected
				 * by any kind of lock, so several parallel
				 * ->parse() calls are possible.
				 */
				result = jplug -> parse( node );
			else
				result = -EIO;
		} else
			result = PTR_ERR( page );

		if( likely( result == 0 ) ) {
			JF_SET( node, ZNODE_LOADED );
		} else
			jrelse_nolock( node );
	} else {
		struct page *page;

		page = jnode_page( node );
		assert( "nikita-2348", page != NULL );
		page_cache_get( page );
		kmap( page );
		result = 0;
	}
	return result;
}

/** just like jrelse, but assume jnode is already spin-locked */
void jrelse_nolock( jnode *node /* jnode to release references to */ )
{
	struct page *page;

	assert( "nikita-487", node != NULL );
	assert( "nikita-489", atomic_read( &node -> d_count ) > 0 );
	ON_SMP( assert( "nikita-1906", spin_jnode_is_locked( node ) ) );

	ON_DEBUG( -- lock_counters() -> d_refs );

	trace_on( TRACE_PCACHE, "release node: %p\n", node );

	page = jnode_page( node );
	if( page != NULL ) {
		kunmap( page );
		page_cache_release( page );
	}

	if( atomic_dec_and_test( &node -> d_count ) )
		JF_CLR( node, ZNODE_LOADED );
}


/* A wrapper around tree->ops->drop_node method */
void jdrop (jnode * node)
{
	reiser4_tree * tree = current_tree;
	int result;

	assert ("zam-602", node != NULL);
	assert ("zam-603", tree->ops != NULL);
	assert ("zam-604", tree->ops->drop_node != NULL);
	ON_SMP (assert ("nikita-2362", spin_tree_is_locked (tree)));

	/* reference was acquired by other thread. */
	if (atomic_read (& node->x_count) > 0)
		return;

	/* remove jnode from hash-table */
	j_hash_remove (&tree->jhash_table, node);

	result = tree->ops->drop_node (tree, node);
	if (result != 0)
		warning ("nikita-2363", "Failed to drop jnode: %llx: %i",
			 *jnode_get_block (node), result);
	jfree (node);
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

jnode_type jnode_get_type( const jnode *node )
{
	static const unsigned long state_mask = 
		( 1 << ZNODE_UNFORMATTED ) | 
		( 1 << ZNODE_UNUSED_1 ) | ( 1 << ZNODE_UNUSED_2 );

	static jnode_type mask_to_type[] = {
		/*  ZNODE_UNUSED_2 : ZNODE_UNUSED_1 : ZNODE_UNFORMATTED */
		
		/* 000 */
		[ 0 ] = JNODE_FORMATTED_BLOCK,
		/* 001 */
		[ 1 ] = JNODE_UNFORMATTED_BLOCK,
		/* 010 */
		[ 2 ] = JNODE_BITMAP,
		/* 011 */
		[ 3 ] = JNODE_LAST_TYPE, /* invalid */
		/* 100 */
		[ 4 ] = JNODE_JOURNAL_RECORD,
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
	return mask_to_type[ ( node -> state & state_mask ) >> ZNODE_UNFORMATTED ];
}

static int noparse( jnode *node )
{
	return 0;
}

static struct address_space *jnode_mapping( const jnode *node )
{
	return node -> key.mapping;
}

static unsigned long jnode_index( const jnode *node )
{
	return node -> key.index;
}

static struct address_space *znode_mapping( const jnode *node )
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
			.parse   = noparse,
			.mapping = jnode_mapping,
			.index   = jnode_index
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
			.parse   = znode_parse,
			.mapping = znode_mapping,
			.index   = znode_index
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
			.parse   = noparse,
			.mapping = znode_mapping,
			.index   = znode_index
		}
	},
	[ JNODE_JOURNAL_RECORD ] = {
		.jnode = {
			.h = {
				.type_id = REISER4_JNODE_PLUGIN_TYPE,
				.id      = JNODE_JOURNAL_RECORD,
				.pops    = NULL,
				.label   = "journal record",
				.desc    = "journal record",
				.linkage = TS_LIST_LINK_ZERO
			},
			.parse   = noparse,
			.mapping = NULL,
			.index   = NULL
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
			.parse   = noparse,
			.mapping = NULL,
			.index   = NULL
		}
	}
};

jnode_plugin *jnode_ops_of( const jnode_type type )
{
	assert( "nikita-2367", ( 0 <= type ) && ( type < JNODE_LAST_TYPE ) );
	return jnode_plugin_by_id( type );
}

jnode_plugin *jnode_ops( const jnode *node )
{
	assert( "nikita-2366", node != NULL );

	return jnode_ops_of( jnode_get_type( node ) );
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

	info( "%s: %p: state: %lu: [%s%s%s%s%s%s%s%s%s%s%s%s%s%s], level: %i, block: %llu, d_count: %d, x_count: %d, pg: %p, ",
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
	      jnode_state_name( node, ZNODE_MAPPED ),
	      jnode_state_name( node, ZNODE_FLUSH_BUSY ),
	      jnode_state_name( node, ZNODE_FLUSH_QUEUED ),

	      jnode_get_level( node ), *jnode_get_block( node ),
	      atomic_read( &node -> d_count ), atomic_read( &node -> x_count ),
	      jnode_page( node ) );
	if( jnode_is_unformatted( node ) ) {
		info( "inode: %li, index: %lu, ", 
		      node -> key.mapping -> host -> i_ino, node -> key.index );
	}
}

/** this is cut-n-paste replica of print_znodes() */
void print_jnodes( const char *prefix, reiser4_tree *tree )
{
	jnode       **bucket;
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

	for_all_ht_buckets( htable, bucket ) {
		for_all_in_bucket( bucket, node, next, link.j ) {
			info_jnode( prefix, node );
			info( "\n" );
		}
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
