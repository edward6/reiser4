/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Znode manipulation functions.
 */
/*
 * Znode is the in-memory header for a tree node. It is stored
 * separately from the node itself so that it does not get written to
 * disk.  In this respect znode is like buffer head or page head. We
 * also use znodes for additional reiserfs specific purposes:
 *
 *  . they are organized into tree structure which is a part of whole
 *    reiser4 tree. 
 *  . they are used to implement node grained locking
 *  . they are used to keep additional state associated with a
 *    node 
 *  . they contain links to lists used by the transaction manager
 *
 * Znode is attached to some variable "block number" which is instance
 * of fs/reiser4/tree.h:reiser4_block_nr type. Znode can exist without
 * appropriate node being actually loaded in memory. Existence of znode
 * itself is regulated by reference count (->x_count) in it. Each time
 * thread acquires reference to znode through call to zget(),
 * ->x_count is incremented and decremented on call to zput().  Data
 * (content of node) are brought in memory through call to zload(),
 * which also increments ->d_count reference counter.  zload can block
 * waiting on IO.  Call to zrelse() decreases this counter. Thus,
 * ->x_count is never less than ->d_count. Also, ->c_count keeps track
 * of number of child znodes and prevents parent znode from being
 * recycled until all of its children are. ->c_count is decremented
 * whenever child goes out of existence (being actually recycled in
 * zdestroy()) which can be some time after last reference to this
 * child dies if we support some form of LRU cache for znodes.
 *
 */
/*
 * EVERY ZNODE'S STORY
 *
 * 1. His infancy.
 *
 * Once upon a time, the znode was born deep inside of zget() by call to
 * zalloc(). At the return from zget() znode had:
 *
 *  . reference counter (x_count) of 1
 *  . assigned block number, marked as used in bitmap
 *  . pointer to parent znode. Root znode parent pointer points 
 *    to its father: "fake" znode. This, in turn, has NULL parent pointer.
 *  . hash table linkage
 *  . no data loaded from disk
 *  . no node plugin
 *  . no sibling linkage
 *
 * 2. His childhood
 *
 * Each node is either brought into memory as a result of tree
 * traversal, or created afresh, creation of the root being a special
 * case of the latter. In either case it's inserted into sibling
 * list. This will typically require some ancillary tree traversing,
 * but ultimately both sibling pointers will exist and
 * ZNODE_LEFT_CONNECTED and ZNODE_RIGHT_CONNECTED will be true in zstate.
 *
 * 3. His youth.
 *
 * If znode is bound to already existing node in a tree, its content is read
 * from the disk by call to zload(). At that moment, ZNODE_LOADED bit is set
 * in zstate and zdata() function starts to return non null for this
 * znode. zload() further calls zparse() that determines which node layout
 * this node is rendered in, and sets ->nplug on success.
 *
 * If znode is for new node just created, memory for it is allocated
 * [this is not done yet] and zinit_new() function is called to
 * initialise data, according to selected node layout.
 *
 * 4. His maturity.
 *
 * After this point, znode lingers in memory for some time. Threads can
 * acquire references to znode either by blocknr through call to zget(), or by
 * following a pointer to unallocated znode from internal item [not
 * implemented yet]. Each time reference to znode is obtained, x_count is
 * increased. Thread can read/write lock znode. Znode data can be loaded
 * through calls to zload(), d_count will be increased appropriately. If all
 * references to znode are released (x_count drops to 0), znode is not
 * recycled immediately. Rather, it is still cached in the hash table in the
 * hope that it will be accessed shortly.
 *
 * There are two ways in which znode existence can be terminated: 
 *
 *  . sudden death: node bound to this znode is removed from the tree
 *  . overpopulation: znode is purged out of memory due to memory pressure
 *
 * 5. His death.
 *
 * Death is complex process.
 *
 * When we irrevocably commit ourselves to decision to remove node from
 * the tree, ZNODE_HEARD_BANSHEE bit is set in zstate of corresponding
 * znode. This is done either in ->kill_hook() of internal item or in
 * kill_root() function when tree root is removed.
 *
 * At this moment znode still has:
 *
 *  . locks held on it, necessary write ones
 *  . references to it
 *  . disk block assigned to it
 *  . data loaded from the disk
 *  . pending requests for lock
 *
 * But once ZNODE_HEARD_BANSHEE bit set, last call to unlock_znode() does node
 * deletion. Node deletion includes two phases. First all ways to get
 * references to that znode (sibling and parent links and hash lookup using
 * block number stored in parent node) should be deleted -- it is done through
 * sibling_list_remove(), also we assume that nobody uses down link
 * from parent node due to its nonexistence or proper parent node locking and
 * nobody uses parent pointers from children due to absence of them. Second we
 * invalidate all pending lock requests which still are on znode's lock
 * request queue, this is done by invalidate_lock(). Another
 * ZNODE_IS_DYING znode status bit is used to invalidate pending lock
 * requests. Once it set all requesters are forced to return -EINVAL from
 * longterm_lock_znode(). Future locking attempts are not possible because all
 * ways to get references to that znode are removed already.
 *
 * When last reference to the dying znode is just about to be released,
 * block number for this lock is released and znode is removed from the
 * hash table.
 *
 * Now znode can be recycled.
 *
 * [it's possible to free bitmap block and remove znode from the hash
 * table when last lock is released. This will result in having
 * referenced but completely orphaned znode]

 # Surely an attempt to access a node raises it from the dead (though
 # does not necessarily prevent death if it is in progress)....  surely
 # such an attempt at access blocks but does not return failure... yes? -Hans
 #
 # No, there is no way to get new reference on that znode because he is not on
 # sibling list, he has no children and he is not in hash table so he is not
 # accessible by its block number. -zam

 # So attempting to get a znode by blocknr does not create the znode
 # and perform IO for the case where no such znode exists?

 *
 * 6. Limbo
 *
 * As have been mentioned above znodes with reference counter 0 are
 * still cached in a hash table. Once memory pressure increases they are
 * purged out of there [this requires something like LRU list for
 * efficient implementation. LRU list would also greatly simplify
 * implementation of coord cache that would in this case morph to just
 * scanning some initial segment of LRU list]. Data loaded into
 * unreferenced znode are flushed back to the durable storage if
 * necessary and memory is freed. Znodes themselves can be recycled at
 * this point too.
 *
 */

#include "reiser4.h"

/* hash table support */

/** compare two block numbers for equality. Used by hash-table macros */
/* Audited by: umka (2002.06.11) */
static inline int blknreq( const reiser4_block_nr *b1,
			   const reiser4_block_nr *b2 )
{
	assert( "nikita-534", b1 != NULL );
	assert( "nikita-535", b2 != NULL );

	return *b1 == *b2;
}

/** Hash znode by block number. Used by hash-table macros */
/* Audited by: umka (2002.06.11) */
static inline __u32 blknrhashfn( const reiser4_block_nr *b )
{
	assert( "nikita-536", b != NULL );

	return *b & ( REISER4_ZNODE_HASH_TABLE_SIZE - 1 );
}

/** The hash table definition */
#define KMALLOC( size ) reiser4_kmalloc( ( size ), GFP_KERNEL )
#define KFREE( ptr, size ) reiser4_kfree( ptr, size )
TS_HASH_DEFINE( z, znode, reiser4_block_nr, zjnode.blocknr, link, blknrhashfn, blknreq );
#undef KFREE
#undef KMALLOC

/** slab for znodes */
static kmem_cache_t *znode_slab;

/****************************************************************************************
				   ZNODE INITIALIZATION
 ****************************************************************************************/

/** call this once on reiser4 initialisation*/
/* Audited by: umka (2002.06.11) */
int znodes_init()
{
	znode_slab = kmem_cache_create( "znode_cache", sizeof( znode ),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL );
	if( znode_slab == NULL ) {
		return -ENOMEM;
	} else {
		return 0;
	}
}

/** call this before unloading reiser4 */
/* Audited by: umka (2002.06.11) */
int znodes_done()
{
	return kmem_cache_destroy( znode_slab );
}

/** call this to initialise tree of znodes */
/* Audited by: umka (2002.06.11) */
int znodes_tree_init( reiser4_tree *tree /* tree to initialise znodes for */ )
{
	assert( "umka-050", tree != NULL );
	
	spin_lock_init( & tree -> tree_lock );
	spin_lock_init( & tree -> dk_lock );

	return z_hash_init( &tree -> hash_table, REISER4_ZNODE_HASH_TABLE_SIZE );
}

/** call this to free tree of znodes */
/* Audited by: umka (2002.06.11) */
void znodes_tree_done( reiser4_tree *tree /* tree to finish with znodes of */ )
{
	assert( "nikita-795", tree != NULL );

	z_hash_done( &tree -> hash_table );
}

/****************************************************************************************
				   ZNODE STRUCTURES
 ****************************************************************************************/

/** free this znode */
/* Audited by: umka (2002.06.11) */
static void zfree( znode *node /* znode to free */ )
{
	trace_stamp( TRACE_ZNODES );
	assert( "nikita-465", node != NULL );
	kmem_cache_free( znode_slab, node );
}

/** allocate fresh znode */
/* Audited by: umka (2002.06.11) */
static znode *zalloc( int gfp_flag /* allocation flag */ )
{
	trace_stamp( TRACE_ZNODES );
	return kmem_cache_alloc( znode_slab, gfp_flag );
}

/** initialise fields of znode */
/* Audited by: umka (2002.06.11) */
static void zinit( znode *node /* znode to initialise */, 
		   znode *parent /* parent znode */ )
{
	assert( "nikita-466", node != NULL );
	
	xmemset( node, 0, sizeof *node );
	jnode_init( &node -> zjnode );
	reiser4_init_lock( &node -> lock );

	ncoord_init_parent_hint (&node -> ptr_in_parent_hint, parent);

	assert( "umka-051", current_tree != NULL );
	
	spin_lock_tree( current_tree );
	node -> version = ++ current_tree -> znode_epoch;
	spin_unlock_tree( current_tree );
}

/** zdestroy() -- Return a znode to the slab allocator.
 *
 * This is called from deallocate_znode() when last reference to the
 * znode removed from the tree is release.
 */
/* Audited by: umka (2002.06.11) */
void zdestroy( znode *node /* znode to finish with */ )
{
	trace_stamp( TRACE_ZNODES );
	assert( "nikita-467", node != NULL );
	assert( "nikita-1443", current_tree != NULL );

	spin_lock_tree( current_tree );

	if( atomic_read( &node -> x_count ) > 0 ) {
		spin_unlock_tree( current_tree );
		return;
	}

	assert( "nikita-468", atomic_read( &node -> d_count ) == 0 );
	assert( "nikita-469", atomic_read( &node -> x_count ) == 0 );
	assert( "nikita-470", atomic_read( &node -> c_count ) == 0 );

	/* remove reference to this znode from pbk cache */
	cbk_cache_invalidate( node );
	/* 
	 * while we were taking lock on pbk cache to remove us from
	 * there, some lucky parallel process could hit reference to
	 * this znode from pbk cache. Check for this. 
	 * 
	 * Tree lock, shared by the hash table protects us from another
	 * process taking reference to this node.
	 */

	if( ( znode_parent( node ) != NULL ) && 
	    !znode_above_root( znode_parent( node ) ) ) {
		/* father, onto your hands I forward my spirit... */
		atomic_dec( &znode_parent( node ) -> c_count );
		assert( "nikita-472",
			atomic_read( &znode_parent( node ) -> c_count ) >= 0 );
	} else {
		/* orphaned znode?! Root? */ 
	}

	/* remove znode from hash-table */
	z_hash_remove( & current_tree -> hash_table, node );
	spin_unlock_tree( current_tree );
	/*
	 * poison memory. Put this under REISER4_DEBUG once race is fixed.
	 */
	ON_DEBUG( xmemset( node, 0xde, sizeof *node ) );
	zfree( node );
}

/** put znode into right place in the hash table */
/* Audited by: umka (2002.06.11) */
int znode_rehash( znode *node /* node to rehash */, 
		  const reiser4_block_nr *new_block_nr /* new block number */ )
{
	z_hash_table *htable;

	assert( "nikita-2018", node != NULL );
	assert( "umka-052", current_tree != NULL );

	htable  = &current_tree -> hash_table;

	spin_lock_tree( current_tree );
	/* remove znode from hash-table */
	z_hash_remove( htable, node );

	assert( "nikita-2019", z_hash_find( htable, new_block_nr ) == NULL );

	/* update blocknr */
	znode_set_block( node, new_block_nr );
	/* insert it into hash */
	z_hash_insert( htable, node );
	spin_unlock_tree( current_tree );
	return 0;
}


/****************************************************************************************
				   ZNODE LOOKUP, GET, PUT
 ****************************************************************************************/

/**
 * zlook() - get znode with given block_nr in a hash table or return NULL
 *
 * If result is non-NULL then the znode's x_count is incremented.  Internal version
 * accepts pre-computed hash index.  The hash table is accessed under caller's
 * tree->hash_lock.
 */
/* Audited by: umka (2002.06.11) */
znode*
zlook (reiser4_tree *tree, const reiser4_block_nr *const blocknr)
{
	znode *result;

	trace_stamp (TRACE_ZNODES);

	assert ("jmacd-506", tree    != NULL);
	assert ("jmacd-507", blocknr != NULL);

	/* Precondition for call to zlook_internal: locked hash table */
	spin_lock_tree (tree);

	result = z_hash_find_index (& tree->hash_table, blknrhashfn (blocknr), blocknr);

	/* According to the current design, the hash table lock protects new znode
	 * references. */
	if (result != NULL) {
		add_x_ref (result);
	}

	/* Release hash table lock: non-null result now referenced. */
	spin_unlock_tree (tree);

	return result;
}

/** bump reference counter on @node */
/* Audited by: umka (2002.06.11) */
void add_x_ref( znode *node /* node to increase x_count of */ )
{
	assert( "nikita-1911", node != NULL );

	atomic_inc( &node -> x_count );
	ON_DEBUG( ++ lock_counters() -> x_refs );
}

/** bump data counter on @node */
/* Audited by: umka (2002.06.11) */
void add_d_ref( znode *node /* node to increase d_count of */ )
{
	assert( "nikita-1962", node != NULL );

	atomic_inc( &node -> d_count );
	ON_DEBUG( ++ lock_counters() -> d_refs );
}

/**
 * zref() - increase counter of references to znode (x_count)
 */
/* Audited by: umka (2002.06.11) */
znode *zref (znode *node)
{
	assert ("jmacd-508", (node != NULL) && ! IS_ERR (node));

	add_x_ref( node );
	
	return node;
}


/**
 * zget() - get znode from hash table, allocating it if necessary.
 *
 * First a call to zlook, locating a x-referenced znode if one
 * exists.  If znode is not found, allocate new one and return.  Result
 * is returned with x_count reference increased.
 *
 * LOCKS TAKEN:   TREE_LOCK, ZNODE_LOCK
 * LOCK ORDERING: NONE
 */
/* Audited by: umka (2002.06.11) */
znode*
zget (reiser4_tree *tree,
      const reiser4_block_nr *const blocknr,
      znode        *parent,
      tree_level    level,
      int           gfp_flag)
{
	znode *result;
	znode *shadow;
	__u32  hashi;

	trace_stamp (TRACE_ZNODES);

	assert ("jmacd-512", tree    != NULL);
	assert ("jmacd-513", blocknr != NULL);
	assert ("jmacd-514", level < REISER4_MAX_ZTREE_HEIGHT);

	hashi = blknrhashfn (blocknr);

	/* Take the hash table lock. */
	spin_lock_tree (tree);

	/*
	 * FIXME-NIKITA address-as-unallocated-blocknr still is not
	 * implemented.
	 */
	if (0 && is_disk_addr_unallocated (blocknr)) {
		/*
		 * Asked for unallocated znode.
		 */
		result = unallocated_disk_addr_to_ptr (blocknr);
	} else {
		/* Find a matching BLOCKNR in the hash table.  If the znode is
		 * found, we obtain an reference (x_count) but the znode
		 * remains * unlocked.  Have to worry about race conditions
		 * later. */
		result = z_hash_find_index (& tree->hash_table, hashi, blocknr);
	}

	/* According to the current design, the hash table lock protects new znode
	 * references. */
	if (result != NULL) {
		add_x_ref (result);
	}

	/* Release the hash table lock. */
	spin_unlock_tree (tree);

	if (result != NULL) {

		/* ZGET HIT CASE: No locks are held at this point. */

		/* Arrive here after a race to insert a new znode: the shadow variable
		 * below became result. */
	retry_miss_race:

		/* Take the znode lock in order to test its blocknr and status. */
		if (REISER4_DEBUG) {
			spin_lock_znode (result);

			/* The block numbers must be equal. */
			assert ("jmacd-1160", blknreq (& ZJNODE(result)->blocknr, blocknr));

			spin_unlock_znode (result);
		}

 	} else {

		/* ZGET MISS CASE: No locks are held at this point. */
		result = zalloc (gfp_flag);
		
		if (result == NULL) {
			return ERR_PTR(-ENOMEM);
		}
		
		/* Initialize znode before checking for a race and inserting.  Since this
		 * is a freshly allocated znode there is no need to lock it here. */
		zinit (result, parent);

		ZJNODE(result)->blocknr = *blocknr;

		znode_set_level (result, level);

		add_x_ref (result);

		/* Repeat search in case of a race, first take the hash table lock. */
		spin_lock_tree (tree);

		shadow = z_hash_find_index (& tree->hash_table, hashi, blocknr);

		if (shadow != NULL) {

			/* Another process won: release hash lock, free result, retry as
			 * if it were an earlier hit. */
			spin_unlock_tree (tree);
			zfree (result);
			result = shadow;
			goto retry_miss_race;
		}
		
		/* Insert it into hash: no race. */
		z_hash_insert_index (& tree->hash_table, hashi, result);

		/* Release hash lock. */
		spin_unlock_tree (tree);

		if (parent != NULL) {
			atomic_inc (& parent->c_count);
		}

	}

	assert ("jmacd-503", (result != NULL) && ! IS_ERR (result));

	/* Check for invalid tree level, return -EIO */
	if (znode_get_level (result) != level) {
		warning ("jmacd-504",
			 "Wrong tree level for cached block %llu: level %i expecting %i",
			 *blocknr,
			 znode_get_level (result),
			 level);
		return ERR_PTR (-EIO);
	}

	assert ("nikita-1227", znode_invariant (result));
	return result;
}

/**
 * zput() - decrement x_count reference counter on znode.
 *
 * Count may drop to 0, znode stays in cache until memory pressure causes the eviction of
 * its page.  The c_count variable also ensures that children are pressured out of memory
 * before the parent.  The znode remains hashed as long as the VM allows its page to stay
 * in memory, and then we force its children out first?  There is no zcache_shrink().
 */
/* Audited by: umka (2002.06.11) */
void zput (znode *node)
{
	trace_stamp (TRACE_ZNODES);

	assert ("jmacd-509", node != NULL);
	assert ("jmacd-510", atomic_read (& node->x_count) > 0);
	assert ("jmacd-511", atomic_read (& node->d_count) >= 0);
	assert ("jmacd-572", atomic_read (& node->c_count) >= 0);

	/*
	 * FIXME-NIKITA nikita: handle releasing reference to the znode that is
	 * removed from the tree. Locking?
	 */
	if (atomic_dec_and_test (& node->x_count) && 
	    ZF_ISSET (node, ZNODE_HEARD_BANSHEE)) {

		/* FIXME_JMACD: Currently deallocate_znode just calls zdestroy(). */
		deallocate_znode (node);

		/* FIXME_JMACD: The atom has no reference because jnodes don't have
		 * an x_count... what to do? */
	}
	ON_DEBUG ( -- lock_counters() -> x_refs );
}

/****************************************************************************************
				    ZNODE PLUGINS/DATA
 ****************************************************************************************/

/**
 * "guess" plugin for node loaded from the disk. Plugin id of node plugin is
 * stored at the fixed offset from the beginning of the node.
 */
/* Audited by: umka (2002.06.11) */
static node_plugin *znode_guess_plugin( const znode *node /* znode to guess
							   * plugin of */)
{
	assert( "nikita-1053", node != NULL );
	assert( "nikita-1055", znode_is_loaded( node ) );
	assert( "umka-053", current_tree != NULL );

	if( get_current_super_private() -> one_node_plugin ) {
		return current_tree -> nplug;
	} else {
		return node_plugin_by_disk_id
			( current_tree, 
			  &( ( common_node_header * ) zdata( node ) ) -> plugin_id );
#ifdef GUESS_EXISTS
		reiser4_plugin *plugin;

		/*
		 * FIXME-NIKITA add locking here when dynamic plugins will be implemented
		 */
		for_all_plugins( REISER4_NODE_PLUGIN_TYPE, plugin ) {
			if( ( plugin -> u.node.guess != NULL ) &&
			    plugin -> u.node.guess( node ) )
				return plugin;
		}
#endif
		warning( "nikita-1057", "Cannot guess node plugin" );
		print_znode( "node", node );
		return NULL;
	}
}

/** parse node header and install ->node_plugin */
/* Audited by: umka (2002.06.11) */
static int zparse( znode *node /* znode to parse */ )
{
	int result;

	assert( "nikita-1233", node != NULL );
	assert( "nikita-1904", znode_is_loaded( node ) );

	if( node -> nplug == NULL ) {
		node_plugin *nplug;

		nplug = znode_guess_plugin( node );
		if( nplug != NULL ) {
			node -> nplug = nplug;
			result = nplug -> init_znode/*parse*/( node );
			if( unlikely( result != 0 ) )
				node -> nplug = NULL;
		} else {
			result = -EIO;
		}
	} else
		result = 0;
	return result;
}

static int zrelse_nolock( znode *node );

/** load content of node into memory */
/* Audited by: umka (2002.06.11) */
int zload( znode *node /* znode to load */ )
{
	int result;

	assert( "nikita-484", node != NULL );
	assert( "nikita-1377", znode_invariant( node ) );

	result = 0;
	spin_lock_znode( node );
	reiser4_stat_znode_add( zload );
	if( ! ZF_ISSET( node, ZNODE_LOADED ) ) {
		reiser4_tree *tree;

		spin_unlock_znode( node );
		
		tree = current_tree;

		/* load data... */
		assert( "nikita-1097", tree != NULL );
		assert( "nikita-1098", tree -> ops -> read_node != NULL );

		/*
		 * ->read_node() reads data from page cache. In any case we
		 * rely on proper synchronization in the underlying
		 * transport. Page reference counter is incremented and page is
		 * kmapped, it will be decremented and kunmaped in zunload
		 */
		result = tree -> ops -> read_node( tree, ZJNODE( node ) );
		reiser4_stat_znode_add( zload_read );
		spin_lock_znode( node );

		if( result == 0 ) {
			ZF_SET( node, ZNODE_LOADED );
			add_d_ref( node );
			result = zparse( node );
			if( unlikely( result != 0 ) ) {
				zrelse_nolock( node );
			}
		}
	} else
		add_d_ref( node );
	spin_unlock_znode( node );
	assert( "nikita-1378", znode_invariant( node ) );
	return result;
}

/** call node plugin to initialise newly allocated node. */
/* Audited by: umka (2002.06.11) */
int zinit_new( znode *node /* znode to initialise */ )
{
	int   result;

	assert( "nikita-1234", node != NULL );
	assert( "nikita-1908", current_tree -> ops -> allocate_node != NULL );

	result = current_tree -> ops -> 
		allocate_node( current_tree, ZJNODE( node ) );
	if( result == 0 ) {
		ZF_SET( node, ZNODE_LOADED );
		ZF_SET( node, ZNODE_ALLOC );
		add_d_ref( node );
		assert( "nikita-1235", znode_is_loaded( node ) );
		assert( "nikita-1236", node_plugin_by_node( node ) != NULL );
		result = node_plugin_by_node( node ) -> init( node );
	}
	return result;
}

/**
 * unload node content from memory. Write it back to the durable storage, if
 * necessary.
 */
/* Audited by: umka (2002.06.11) */
int zunload( znode *node /* znode to unload */ )
{
	assert( "nikita-485", node != NULL );
	assert( "nikita-486", atomic_read( &node -> d_count ) == 0 );
	assert( "vs-660", current_tree -> ops -> release_node != NULL );

	current_tree -> ops -> release_node( current_tree, ZJNODE( node ) );
	ZF_CLR( node, ZNODE_LOADED );
	ZJNODE( node ) -> pg = NULL;
	return 0;
}

/** just like zrelse, but assume znode is already spin-locked */
/* Audited by: umka (2002.06.11) */
static int zrelse_nolock( znode *node /* znode to release references to */ )
{
	assert( "nikita-487", node != NULL );
	assert( "nikita-489", atomic_read( &node -> d_count ) > 0 );
	assert( "nikita-1906", spin_znode_is_locked( node ) );

	ON_DEBUG( -- lock_counters() -> d_refs );
	if( atomic_dec_and_test( &node -> d_count ) ) {
		/* unload data, or done some sort of lazy unloading */
		/* spinlock needed to protect another d_count increment while
		 * zunloading, thus semantically equivalent to
		 * atomic_dec_and_lock().  same implementation as
		 * atomic_dec_and_lock() as well, although comments in
		 * atomic.h suggest a more efficient implementation is
		 * possible on some architectures. */
		return zunload( node );
	}
	return 0;
}

/**
 * drop reference to node data. When last reference is dropped, data are
 * unloaded.
 */
/* Audited by: umka (2002.06.11) */
int zrelse( znode *node /* znode to release references to */ )
{
	int ret = 0;
	
	assert( "nikita-1963", node != NULL );
	assert( "nikita-1964", atomic_read( &node -> d_count ) > 0 );
//	assert( "nikita-1907", !spin_znode_is_locked( node ) );

	assert( "nikita-1381", znode_invariant( node ) );
	spin_lock_znode( node );
	ret = zrelse_nolock( node );
	spin_unlock_znode( node );
	assert( "nikita-1382", znode_invariant( node ) );
	return ret;
}

/** size of data in znode */
/* Audited by: umka (2002.06.11) */
unsigned znode_size( const znode *node /* znode to query */ )
{
	assert( "nikita-1416", node != NULL );
	return PAGE_CACHE_SIZE;
}

/** returns free space in node */
/* Audited by: umka (2002.06.11) */
unsigned znode_free_space( znode *node /* znode to query */ )
{
	assert( "nikita-852", node != NULL );
	return node_plugin_by_node( node ) -> free_space( node );
}

/** return non-0 iff data are loaded into znode */
/* Audited by: umka (2002.06.11) */
int znode_is_loaded( const znode *node /* znode to query */ )
{
	assert( "nikita-497", node != NULL );
	return ZF_ISSET( node, ZNODE_LOADED );
}

/* functions to maintain (partial) tree of znodes */

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

/* Audited by: umka (2002.06.11) */
void jnode_set_block( jnode *node /* jnode to update */,
		      const reiser4_block_nr *blocknr /* new block nr */ )
{
	assert( "nikita-2020", node  != NULL );
	assert( "umka-055", blocknr != NULL );
	
	node -> blocknr = *blocknr;
}
#if 0
/* this is used to assign block number to jnode of unformatted node */
void jnode_set_block (jnode * node, reiser4_block_nr block)
{
	assert ("vs-650", node != NULL);
	assert ("vs-672", node->blocknr);
	assert ("vs-651", blocknr_is_fake (&node->blocknr));
	node->blocknr = block;
}
#endif

/* return true if jnode has real blocknr */
int jnode_has_block (jnode * node)
{
	assert ("vs-673", node);
	assert ("vs-674", node->blocknr);
	return blocknr_is_fake (&node->blocknr) ? 0 : 1;
}


/** left delimiting key of znode */
/* Audited by: umka (2002.06.11) */
reiser4_key *znode_get_rd_key( znode *node /* znode to query */ )
{
	assert( "nikita-958", node != NULL );
	assert( "nikita-1661", spin_dk_is_locked( current_tree ) );

	return &node -> rd_key;
}

/** right delimiting key of znode */
/* Audited by: umka (2002.06.11) */
reiser4_key *znode_get_ld_key( znode *node /* znode to query */ )
{
	assert( "nikita-974", node != NULL );
	assert( "nikita-1662", spin_dk_is_locked( current_tree ) );

	return &node -> ld_key;
}


/** true if @key is inside key range for @node */
/* Audited by: umka (2002.06.11) */
int znode_contains_key( znode *node /* znode to look in */, 
			const reiser4_key *key /* key to look for */ )
{
	assert( "nikita-1237", node != NULL );
	assert( "nikita-1238", key != NULL );

	/* left_delimiting_key <= key <= right_delimiting_key */
	return 
		keyle( znode_get_ld_key( node ), key ) &&
		keyle( key, znode_get_rd_key( node ) );
}

/** same as znode_contains_key(), but lock dk lock */
/* Audited by: umka (2002.06.11) */
int znode_contains_key_lock( znode *node /* znode to look in */, 
			     const reiser4_key *key /* key to look for */ )
{
	int result;

	assert( "umka-056", node != NULL );
	assert( "umka-057", key != NULL );
	assert( "umka-058", current_tree != NULL );
	
	spin_lock_dk( current_tree );
	result = znode_contains_key( node, key );
	spin_unlock_dk( current_tree );
	return result;
}

/** get parent pointer, assuming tree is not locked */
/* Audited by: umka (2002.06.11) */
znode *znode_parent_nolock( const znode *node /* child znode */ )
{
	assert( "nikita-1444", node != NULL );
	return node -> ptr_in_parent_hint.node;
}

/** get parent pointer of znode */
/* Audited by: umka (2002.06.11) */
znode *znode_parent( const znode *node /* child znode */ )
{
	assert( "nikita-1226", node != NULL );
	assert( "nikita-1406", lock_counters() -> spin_locked_tree > 0 );
	return znode_parent_nolock( node );
}

/** detect fake znode used to protect in-superblock tree root pointer */
/* Audited by: umka (2002.06.11) */
int znode_above_root (const znode *node /* znode to query */ )
{
	assert( "umka-059", node != NULL );
	
	return disk_addr_eq(&ZJNODE(node)->blocknr, &FAKE_TREE_ADDR);
}

/** 
 * check that @node is root---that its block number is recorder in the tree as
 * that of root node */
/* Audited by: umka (2002.06.11) */
int znode_is_true_root( const znode *node /* znode to query */ )
{
	assert( "umka-060", node != NULL );
	assert( "umka-061", current_tree != NULL );
	
	return disk_addr_eq( znode_get_block( node ), 
			     &current_tree -> root_block );
}

/** check that @node is root */
/* Audited by: umka (2002.06.11) */
int znode_is_root( const znode *node /* znode to query */ )
{
	int result;
	
	assert( "nikita-1206", node != NULL );
	assert( "umka-062", current_tree != NULL );
	
	result = znode_get_level (node) == current_tree -> height;
	spin_lock_tree( current_tree );
	assert( "nikita-1208", !result || znode_is_true_root( node ) );
	assert( "nikita-1209", !result ||
		znode_get_level( znode_parent( node ) ) == 0 );
	assert( "nikita-1212", !result ||
		( ( node -> left == NULL ) && ( node -> right == NULL ) ) );
	spin_unlock_tree( current_tree );
	return result;
}

void jnode_attach_to_page( jnode *node, struct page *pg )
{
	assert( "nikita-2047", node != NULL );
	assert( "nikita-2048", pg != NULL );

	spin_lock( &_jnode_ptr_lock );
	assert( "nikita-2050", 
		( pg -> private == 0ul ) || 
		( pg -> private == ( unsigned long ) node ) );
	pg -> private = ( unsigned long ) node;
	node -> pg  = pg;
	SetPagePrivate( pg );
	spin_unlock( &_jnode_ptr_lock );
}

void jnode_detach_page( jnode *node )
{
	assert( "nikita-2052", node != NULL );
	assert( "nikita-2053", jnode_page( node ) != NULL );

	spin_lock( &_jnode_ptr_lock );
	jnode_page( node ) -> private = 0ul;
	ClearPagePrivate( jnode_page( node ) );
	node -> pg  = NULL;
	spin_unlock( &_jnode_ptr_lock );
}

/** debugging aid: znode invariant */
/* Audited by: umka (2002.06.11) */
static int znode_invariant_f( const znode *node /* znode to check */, 
			      char const **msg /* where to store error
						* message, if any */ )
{
#define _ergo( ant, con ) ( (*msg) = "{" #ant "} ergo {" #con "}", ergo( ( ant ), ( con ) ) )
#define _equi( p1, p2 )   ( (*msg) = "{" #p1  "} equi {" #p2  "}", equi( ( p1 ),  ( p2 )  ) )
#define zergo( state, p ) _ergo( ZF_ISSET( node, ( state ) ), p )

	return REISER4_DEBUG &&
		/*
		 * Condition 1:
		 */
		( (*msg) = "node is NULL", node != NULL ) &&
		/*
		 * Condition 2-3: two checks specific for fake znode 
		 */
		_ergo( znode_get_level( node ) == 0, znode_parent( node ) == NULL ) &&
		_ergo( znode_get_level( node ) == 0, 
		      disk_addr_eq( znode_get_block( node ), 
				    &FAKE_TREE_ADDR ) ) &&
		_ergo( znode_is_true_root( node ), 
		       znode_above_root( znode_parent( node ) ) ) &&
		/*
		 * Condition 4-6: parent linkage
		 */
		_ergo( znode_above_root( node ), 
		       znode_parent( node ) == NULL ) &&
		_ergo( znode_parent( node ) && !znode_above_root( znode_parent( node ) ), 
		       znode_get_level( znode_parent( node ) ) == znode_get_level( node ) + 1 ) &&

		_ergo( znode_parent( node ) != NULL && 
		       !znode_above_root( znode_parent( node ) ), 
		      atomic_read( &znode_parent( node ) -> c_count ) > 0 ) &&

		/*
		 * Condition 7+: Flags
		 */
		_equi( ZF_ISSET( node, ZNODE_LOADED ), zdata( node ) != NULL ) &&

		_ergo( atomic_read( &node -> d_count ) > 0, 
		      ZF_ISSET( node, ZNODE_LOADED ) ) &&

		_ergo( !znode_above_root( node ) && 
		      ZF_ISSET( node, ZNODE_LOADED ), 
		      !disk_addr_eq( znode_get_block( node ), 
				     &FAKE_TREE_ADDR ) ) &&
		_ergo( znode_get_level( node ) == LEAF_LEVEL,
		       atomic_read( &node -> c_count ) == 0 ) &&
		zergo( ZNODE_NEW, znode_parent( node ) == NULL );
}

/** debugging aid: check znode invariant and panic if it doesn't hold */
/* Audited by: umka (2002.06.11) */
int znode_invariant( const znode *node /* znode to check */ )
{
	char const *failed_msg;
	int         result;

	assert( "umka-063", node != NULL );
	assert( "umka-064", current_tree != NULL );
	
	spin_lock_znode( ( znode * ) node );
	spin_lock_tree( current_tree );
	result = znode_invariant_f( node, &failed_msg );
	if( !result ) {
		print_znode( "corrupted node", node );
		warning( "jmacd-555", "Condition %s failed", failed_msg );
	}
	spin_unlock_tree( current_tree );
	spin_unlock_znode( ( znode * ) node );
	return result;
}

#if REISER4_DEBUG_MODIFY
/* Audited by: umka (2002.06.11) */
static __u32 znode_checksum( const znode *node )
{
	int i, size = node->size;
	__u32 l = 0;
	__u32 h = 0;
	const char *data = node->data;;

	assert( "umka-065", node != NULL );
	assert ("jmacd-1080", ZF_ISSET (node, ZNODE_LOADED));

	/* Checksum is similar to adler32... */
	for (i = 0; i < size; i += 1) {
		l += data[i];
		h += l;
	}

	return (h << 16) | (l & 0xffff);
}

/* Audited by: umka (2002.06.11) */
void znode_pre_write( znode *node )
{
	assert( "umka-066", node != NULL );
	
	if ( ! znode_is_dirty( node ) ) {
		node->cksum = znode_checksum( node );
	}
}

/* Audited by: umka (2002.06.11) */
void znode_post_write( const znode *node )
{
	__u32 cksum;
	
	assert( "umka-067", node != NULL );
	
	cksum = znode_checksum (node);

	if ( ! (znode_is_dirty (node) || cksum == node->cksum))
		rpanic ("jmacd-1081", "changed znode is not dirty: %llu", 
			node->zjnode.blocknr);

	if (znode_is_dirty (node) && cksum == node->cksum) {
		warning ("jmacd-1082", "dirty node %llu was not actually modified (or cksum collision)", node->zjnode.blocknr);
	}
}
#endif

/** return pointer to static storage with name of lock_mode. For
    debugging */
/* Audited by: umka (2002.06.11) */
const char *lock_mode_name( znode_lock_mode lock /* lock mode to get name of */ )
{
	if( lock == ZNODE_READ_LOCK )
		return "read";
	else if( lock == ZNODE_WRITE_LOCK )
		return "write";
	else {
		static char buf[ 30 ];

		sprintf( buf, "unknown: %i", lock );
		return buf;
	}
}

#if REISER4_DEBUG

/** debugging aid: output more human readable information about @node that
 * info_znode(). */
/* Audited by: umka (2002.06.11) */
void print_znode( const char *prefix /* prefix to print */, 
		  const znode *node /* node to print */ )
{
	if( node == NULL ) {
		info( "%s: null\n", prefix );
		return;
	}

	info_znode( prefix, node );
	info_znode( "\tparent", znode_parent_nolock( node ) );
	info_znode( "\tleft", node -> left );
	info_znode( "\tright", node -> right );
	print_plugin( "\tnode_plugin", node_plugin_to_plugin ( node -> nplug ) );
	print_key( "\tld", &node -> ld_key );
	print_key( "\trd", &node -> rd_key );
	info( "\treaders: %i\n", node -> lock.nr_readers );
}

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

	info( "%s: %p: state: %lu: [%s%s%s%s%s%s%s%s%s%s%s], level: %i, ",
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
	      
	      jnode_get_level( node ));
}

/** debugging aid: output human readable information about @node */
/* Audited by: umka (2002.06.11) */
void info_znode( const char *prefix /* prefix to print */, 
		 const znode *node /* node to print */)
{
	info_jnode (prefix, ZJNODE (node));

	if( node == NULL ) {
		return;
	}

	info( "c_count: %i, d_count: %i, x_count: %i readers: %i, ", 
	      atomic_read( &node -> c_count ),
	      atomic_read( &node -> d_count ),
	      atomic_read( &node -> x_count ),
	      node -> lock.nr_readers );

	print_address( "blocknr", znode_get_block( node ) );
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
