/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Declaration of jnode.
 */

#ifndef __JNODE_H__
#define __JNODE_H__

/** 
 * declare hash table of jnodes (jnodes proper, that is, unformatted
 * nodes) 
 */
TS_HASH_DECLARE(j, jnode);

/** declare hash table of znodes */
TS_HASH_DECLARE(z, znode);

typedef struct {
	struct address_space *mapping;
	unsigned long         index;
} jnode_key_t;

struct jnode
{
	/* jnode's state: bitwise flags from the reiser4_znode_state enum. */
	unsigned long/*__u32*/        state;

	/* lock, protecting jnode's fields. */
	spinlock_t   guard;

	/**
	 * counter of references to jnode's data. Pin data page(s) in
	 * memory while this is greater than 0. Increased on jload().
	 * Decreased on jrelse().
	 */
	atomic_t               d_count;

	/**
	 * counter of references to jnode itself. Increased on jref().
	 * Decreased on jput().
	 */
	atomic_t               x_count;

	/** the real blocknr (where io is going to/from) */
	reiser4_block_nr blocknr;

	union {
		/* znodes are hashed by block number */
		reiser4_block_nr  z;
		/* unformatted nodes are hashed by mapping plus offset */
		jnode_key_t       j;
	} key;

	/* 
	 * pointer to jnode page. 
	 *
	 * FIXME-NIKITA: Page itself is not enough in a case where block size
	 * is smaller than page size. For initial version we are going to
	 * force blocksize == PAGE_CACHE_SIZE. 
	 */
	struct page *pg;

	union {
		/** pointers to maintain hash-table */
		z_hash_link    z;
		j_hash_link    j;
	} link;

	/* atom the block is in, if any */
	txn_atom    *atom;

	/* capture list */
	capture_list_link capture_link;
};

TS_LIST_DEFINE(capture,jnode,capture_link);

typedef enum {
       /** data are loaded from node */
       ZNODE_LOADED            = 0,
       /** node was deleted, not all locks on it were released. This
	   node is empty and is going to be removed from the tree
	   shortly. */
       /** Josh respectfully disagrees with obfuscated, metaphoric names
	   such as this.  He thinks it should be named ZNODE_BEING_REMOVED. */
       ZNODE_HEARD_BANSHEE     = 1,
       /** left sibling pointer is valid */
       ZNODE_LEFT_CONNECTED    = 2,
       /** right sibling pointer is valid */
       ZNODE_RIGHT_CONNECTED   = 3,

       /** znode was just created and doesn't yet have a pointer from
	   its parent */
       ZNODE_ORPHAN            = 4,

       /** this node was created by its transaction and has not been assigned
	* a block address. */
       ZNODE_CREATED           = 5,

       /** this node is currently relocated */
       ZNODE_RELOC             = 6,
       /** this node is currently wandered */
       ZNODE_WANDER            = 7,

       /** this znode has been modified */
       ZNODE_DIRTY             = 8,

       /* znode lock is being invalidated */
       ZNODE_IS_DYING          = 9,
       /* jnode of block which has pointer (allocated or unallocated) from
	* extent or something similar (indirect item, for example) */
       ZNODE_MAPPED            = 10,

       /* jnode is being flushed.  this implies that the node or its children are being
	* squeezed and allocated. */
       ZNODE_FLUSH_BUSY        = 11,

       /* jnode is queued for flushing. */
       ZNODE_FLUSH_QUEUED      = 12,

       /* The jnode is a unformatted node.  False for all znodes.  */
       ZNODE_TYPE_1            = 13,
       ZNODE_TYPE_2            = 14,
       ZNODE_TYPE_3            = 15
} reiser4_znode_state;

/* Macros for accessing the jnode state. */
static inline void JF_CLR (jnode *j, int f) { clear_bit (f, &j->state); }
static inline int JF_ISSET (const jnode *j, int f) { return test_bit (f, &((jnode*)j)->state); }
static inline void JF_SET (jnode *j, int f) { set_bit (f, &j->state); }

/*
 * ordering constraint for znode spin lock: znode lock is weaker than 
 * tree lock and dk lock
 */
#define spin_ordering_pred_jnode( node )					\
	( ( lock_counters() -> spin_locked_tree == 0 ) &&			\
	  ( lock_counters() -> spin_locked_dk == 0 ) &&				\
	  /*									\
	   * in addition you cannot hold more than one jnode spin lock at a	\
	   * time.								\
	   */									\
	  ( lock_counters() -> spin_locked_jnode == 0 ) )

/** 
 * Define spin_lock_znode, spin_unlock_znode, and spin_znode_is_locked.
 * Take and release short-term spinlocks.  Don't hold these across
 * io. 
 */
SPIN_LOCK_FUNCTIONS(jnode,jnode,guard);

static inline int jnode_is_in_deleteset( const jnode *node )
{
	return JF_ISSET( node, ZNODE_RELOC );
}

extern jnode_plugin *jnode_ops( const jnode *node );

extern int jnode_init_static (void);
extern int jnode_done_static (void);

/**
 * Jnode routines
 */
extern jnode* jalloc          (void);
extern void   jfree           (jnode * node);
extern jnode* jnew            (void);
extern void   jnode_set_type  (jnode*, jnode_type);
extern jnode* jget            (reiser4_tree *tree, struct page *pg);
extern jnode *jfind           (struct page *pg);
extern jnode* jnode_by_page   (struct page* pg);
extern jnode* jnode_of_page   (struct page* pg);
extern jnode* page_next_jnode (jnode *node);
extern void   jnode_init      (jnode *node);
extern void   jnode_set_dirty (jnode *node);
extern void   jnode_set_clean (jnode *node);
extern const reiser4_block_nr* jnode_get_block( const jnode *node );
extern void   jnode_set_block (jnode *node, const reiser4_block_nr *blocknr);

extern struct page *jnode_lock_page (jnode *);

/**
 * Jnode flush interface.
 */
extern int    jnode_flush     (jnode *node, int *nr_to_flush, int flags);
extern int    flush_enqueue_unformatted (jnode *node, flush_position *pos);
extern reiser4_blocknr_hint* flush_pos_hint (flush_position *pos);
extern int    flush_pos_leaf_relocate (flush_position *pos);

extern int jnode_check_allocated (jnode *node);
extern int znode_check_allocated (znode *node);

extern jnode_type jnode_get_type( const jnode *node );
extern void jnode_set_type( jnode * node, jnode_type type );

/*
 * FIXME-VS: these are used in plugin/item/extent.c
 */

/* does extent_get_block have to be called */
#define jnode_mapped(node)     JF_ISSET (node, ZNODE_MAPPED)
#define jnode_set_mapped(node) JF_SET (node, ZNODE_MAPPED)
/* pointer to this block was just created (either by appending or by plugging a
 * hole), or zinit_new was called */
#define jnode_created(node)        JF_ISSET (node, ZNODE_CREATED)
#define jnode_set_created(node)    JF_SET (node, ZNODE_CREATED)
/* similar to buffer_uptodate */
#define jnode_loaded(node)     JF_ISSET (node, ZNODE_LOADED)
#define jnode_set_loaded(node) JF_SET (node, ZNODE_LOADED)

/* Macros to convert from jnode to znode, znode to jnode.  These are macros because C
 * doesn't allow overloading of const prototypes. */
#define ZJNODE(x) (& (x) -> zjnode)
#define JZNODE(x)						\
({								\
	typeof (x) __tmp_x;					\
								\
	__tmp_x = (x);						\
	assert ("jmacd-1300", jnode_is_znode (__tmp_x));	\
	(znode*) __tmp_x;					\
})


extern int jnodes_tree_init( reiser4_tree *tree );
extern int jnodes_tree_done( reiser4_tree *tree );

#if REISER4_DEBUG
extern int  znode_is_any_locked( const znode *node );
extern void info_jnode( const char *prefix, const jnode *node );
extern void print_jnodes( const char *prefix, reiser4_tree *tree );
#else
#define info_jnode( p, n ) noop
#define print_jnodes( p, t ) noop
#endif

extern int znode_is_root( const znode *node );

/* Similar to zref() and zput() for jnodes, calls those routines if the node is formatted. */
extern jnode *jref( jnode *node );
extern void   jput( jnode *node );
extern int    jdelete( jnode *node );

/** get the page of jnode */
static inline char *jdata (const jnode *node)
{
	assert ("nikita-1415", node != NULL);
	return node->pg ? page_address (node->pg) : NULL;
}

/** get the page of jnode */
static inline struct page *jnode_page (const jnode *node)
{
	return node->pg;
}

/* Get the index of a block. */
static inline unsigned long jnode_get_index (jnode *node)
{
	return jnode_page (node)->index;
}

/* returns true if node is formatted, i.e, it's not a znode */
static inline int jnode_is_unformatted( const jnode *node)
{
	assert( "jmacd-0123", node != NULL );
	return jnode_get_type( node ) == JNODE_UNFORMATTED_BLOCK;
}

/* returns true if node is a znode */
static inline int jnode_is_znode( const jnode *node )
{
	return jnode_get_type( node ) == JNODE_FORMATTED_BLOCK;
}

static inline int jnode_is_loaded (const jnode * node)
{
	assert ("zam-506", node != NULL);
	return JF_ISSET (node, ZNODE_LOADED);
}


/** return true if "node" is dirty */
static inline int jnode_is_dirty( const jnode *node )
{
	assert( "nikita-782", node != NULL );
	assert( "jmacd-1800", spin_jnode_is_locked (node) || 
		(jnode_is_znode (node) && 
		 znode_is_any_locked (JZNODE (node))));
	return JF_ISSET( node, ZNODE_DIRTY );
}

extern void jnode_attach_page_nolock( jnode *node, struct page *pg );
extern void jnode_attach_page( jnode *node, struct page *pg );
extern void page_detach_jnode_lock( struct page *page, 
				    struct address_space *mapping, 
				    unsigned long index );
extern void page_clear_jnode( struct page *page );
extern void page_detach_jnode( struct page *page );
extern void jnode_detach_page( jnode *node );

/** return true if "node" is dirty, node is unlocked */
static inline int jnode_check_dirty( jnode *node )
{
	int is_dirty;
	assert( "jmacd-7798", node != NULL );
	assert( "jmacd-7799", spin_jnode_is_not_locked (node) );
	spin_lock_jnode (node);
	is_dirty = jnode_is_dirty (node);
	spin_unlock_jnode (node);
	return is_dirty;
}

static inline int jnode_is_allocated (jnode *node)
{
	assert ("jmacd-78212", node != NULL );
	assert ("jmacd-71276", spin_jnode_is_locked (node));
	return ! jnode_is_dirty (node) || JF_ISSET (node, ZNODE_RELOC) || JF_ISSET (node, ZNODE_WANDER);
}

/** return true if "node" is the root */
static inline int jnode_is_root (const jnode *node)
{
	return jnode_is_znode (node) && znode_is_root (JZNODE (node));
}

extern void add_d_ref( jnode *node );

/* jload/jwrite/junload give a bread/bwrite/brelse functionality for jnodes */

extern int jload(jnode * node);
extern int jinit_new( jnode *node );

extern void jdrop             (jnode* node);
extern int  jwait_io          (jnode* node, int rw);

extern void jrelse_nolock     (jnode* node);

extern jnode * alloc_io_head (const reiser4_block_nr * block);
extern void    drop_io_head  (jnode * node);

/**
 * drop reference to node data. When last reference is dropped, data are
 * unloaded.
 */
static inline void jrelse( jnode *node)
{
	assert( "zam-507", node != NULL );
	assert( "zam-508", atomic_read( &node -> d_count ) > 0 );

	spin_lock_jnode (node);
	jrelse_nolock(node);
	spin_unlock_jnode( node );
}

#endif /* __JNODE_H__ */

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
