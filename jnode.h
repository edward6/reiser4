/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Declaration of jnode.
 */

#ifndef __JNODE_H__
#define __JNODE_H__

struct jnode
{
	/* jnode's state: bitwise flags from the reiser4_znode_state enum. */
	unsigned long/*__u32*/        state;

	/* znode's tree level */
	__u16        level;

	/* lock, protecting jnode's fields. */
	spinlock_t   guard;

	/**
	 * counter of references to jnode's data. Pin data page(s) in
	 * memory while this is greater than 0. Increased on jload().
	 * Decreased on jrelse().
	 */
	atomic_t               d_count;

	/* the real blocknr (as far as the parent node is concerned) */
	reiser4_block_nr blocknr;

	/* 
	 * pointer to jnode page. 
	 *
	 * FIXME-NIKITA: Page itself is not enough in a case where block size
	 * is smaller than page size. For initial version we are going to
	 * force blocksize == PAGE_CACHE_SIZE. Later, when and if support for
	 * different block sizes will be added, some bits can be stealed from
	 * ->level to store number of block within page.
	 */
	struct page *pg;

	/* atom the block is in, if any */
	txn_atom    *atom;

	/* capture list */
	capture_list_link capture_link;
};

TS_LIST_DEFINE(capture,jnode,capture_link);

/* FIXME: NOTE: The members of this enum used to be logical bitmasks associated with each
 * state, but now they are shift values so that clear_bit, test_bit, and set_bit
 * primitives work properly.  The problem?  You can't write JF_ISSET (node, ZNODE_WANDER |
 * ZNODE_RELOC) any more.  Nikita, please comment.
 */
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

       /* The jnode is a unformatted node.  False for all znodes.  */
       ZNODE_UNFORMATTED       = 5,

       /** this node was created by its transaction and has not been assigned a block
	* address. */
       ZNODE_CREATED           = 6,

       /** this node is currently relocated */
       ZNODE_RELOC             = 7,
       /** this node is currently wandered */
       ZNODE_WANDER            = 8,

       /** this znode has been modified */
       ZNODE_DIRTY             = 9,

       /* znode lock is being invalidated */
       ZNODE_IS_DYING          = 10,
       /* znode data are mapped into memory */
       ZNODE_KMAPPED           = 11,
       /* jnode of block which has pointer (allocated or unallocated) from
	* extent or something similar (indirect item, for example) */
       ZNODE_MAPPED            = 12,

       /* jnode is being flushed.  this implies that the node or its children are being
	* squeezed and allocated. */
       ZNODE_FLUSH_BUSY        = 14,

       /* jnode is queued for flushing. */
       ZNODE_FLUSH_QUEUED      = 15,

} reiser4_znode_state;

/* Macros for accessing the jnode state. */
#define	JF_CLR(j,f)   clear_bit((f), &((jnode *)j)->state)
#define	JF_ISSET(j,f) test_bit ((f), &((jnode *)j)->state)
#define	JF_SET(j,f)   set_bit  ((f), &((jnode *)j)->state)

/*
 * ordering constraint for znode spin lock: znode lock is weaker than 
 * tree lock and dk lock
 */
#define spin_ordering_pred_jnode( node )			\
	( ( lock_counters() -> spin_locked_tree == 0 ) &&	\
	  ( lock_counters() -> spin_locked_dk == 0 ) )

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

extern int jnode_init_static (void);
extern int jnode_done_static (void);

/**
 * Jnode routines
 */
extern jnode* jalloc          (void);
extern void   jfree           (jnode * node);
extern jnode* jnew            (void);
extern jnode* jnode_by_page   (struct page* pg);
extern jnode* jnode_of_page   (struct page* pg);
extern jnode* page_next_jnode (jnode *node);
extern void   jnode_init      (jnode *node);
extern void   jnode_set_dirty (jnode *node);
extern void   jnode_set_clean (jnode *node);
extern const reiser4_block_nr* jnode_get_block( const jnode *node );
extern int                     jnode_has_block( jnode * );
extern void   jnode_set_block (jnode *node, const reiser4_block_nr *blocknr);

/**
 * Jnode flush interface.
 */
extern int    jnode_flush     (jnode *node, int *nr_to_flush, int flags);
extern int    flush_enqueue_unformatted (jnode *node, flush_position *pos);
extern reiser4_blocknr_hint* flush_pos_hint (flush_position *pos);
extern int    flush_pos_leaf_relocate (flush_position *pos);

extern spinlock_t *jnode_to_page_lock( const jnode *node );
extern spinlock_t *page_to_jnode_lock( const struct page *page );

extern int jnode_check_allocated (jnode *node);
extern int znode_check_allocated (znode *node);

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
#define JZNODE(x) 							\
({									\
	assert ("jmacd-1300", !JF_ISSET ((x), ZNODE_UNFORMATTED));	\
	(znode*) (x);							\
})


#if REISER4_DEBUG
extern int znode_is_any_locked( const znode *node );
void info_jnode( const char *prefix, const jnode *node );
#else
#define info_jnode( p, n ) noop
#endif

extern int znode_is_root( const znode *node );

/* Similar to zref() and zput() for jnodes, calls those routines if the node is formatted. */
extern jnode *jref( jnode *node );
extern void   jput( jnode *node );

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

/** get the level field for a jnode */
static inline tree_level jnode_get_level (const jnode *node)
{
	return node->level;
}

/** set the level field for a jnode */
static inline void jnode_set_level (jnode      *node,
				    tree_level  level)
{
	assert ("jmacd-1161", level < 32);
	node->level = level;
}

/* Get the index of a block. */
static inline unsigned long jnode_get_index (jnode *node)
{
	return jnode_page (node)->index;
}

/* returns true if node is formatted, i.e, it's not a znode */
static inline int jnode_is_unformatted( const jnode *node)
{
	assert ("jmacd-0123", node != NULL);
	return JF_ISSET (node, ZNODE_UNFORMATTED);
}

/* returns true if node is formatted, i.e, it's a znode */
static inline int jnode_is_formatted( const jnode *node)
{
	return ! JF_ISSET (node, ZNODE_UNFORMATTED);
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
	assert( "jmacd-1800", spin_jnode_is_locked (node) || (jnode_is_formatted (node) && znode_is_any_locked (JZNODE (node))));
	return JF_ISSET( node, ZNODE_DIRTY );
}

extern void jnode_attach_page_nolock( jnode *node, struct page *pg );
extern void jnode_attach_page( jnode *node, struct page *pg );
extern void page_detach_jnode( struct page *page );
extern void jnode_detach_page( jnode *node );
extern void break_page_jnode_linkage( struct page *page, jnode *node );

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
	assert( "jmacd-78212", node != NULL );
	assert ("jmacd-71276", spin_jnode_is_locked (node));
	return ! jnode_is_dirty (node) || JF_ISSET (node, ZNODE_RELOC) || JF_ISSET (node, ZNODE_WANDER);
}

/** return true if "node" is the root */
static inline int jnode_is_root (const jnode *node)
{
	return jnode_is_formatted (node) && znode_is_root (JZNODE (node));
}

/** operations to access jnodes */
typedef struct node_operations {
	/** read given tree node from persistent storage. This is called from
	 * zload() */
	int ( *read_node )( reiser4_tree *tree, jnode *node );
	/** allocate memory for newly created znode. This is called from
	 * zinit_new() */
	int ( *allocate_node )( reiser4_tree *tree, jnode *node );
	/** called when node is deleted from the tree. This is called from
	 * zdestroy(). */
	int ( *delete_node )( reiser4_tree *tree, jnode *node );
	/** called when node's data are no longer needed. This is called from
	 * zunload(). */
	int ( *release_node )( reiser4_tree *tree, jnode *node );
	/** called when node is removed from the memory */
	int ( *drop_node )( reiser4_tree *tree, jnode *node );
	/** mark node dirty. This is called from jnode_set_dirty(). */
	int ( *dirty_node )( reiser4_tree *tree, jnode *node );
	/** mark node clean. This is called from jnode_set_clean(). */
	int ( *clean_node )( reiser4_tree *tree, jnode *node );
} node_operations;

extern void add_d_ref( jnode *node );

/* jload/jwrite/junload give a bread/bwrite/brelse functionality for jnodes */

/* load jnode data, leave jnode spin-locked return positive number if
 * cached data was used */
extern int  jload_and_lock    (jnode* node);

/* same as jload_and_lock(), but unlock jnode */
static inline int jload (jnode * node)
{
	int ret;

	ret = jload_and_lock (node);
	spin_unlock_jnode (node);
	return ret;
}

extern int  jdrop             (jnode* node);
extern int  jwait_io          (jnode* node, int rw);

extern void jrelse_nolock     (jnode* node);

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

static inline void junlock_and_relse (jnode * node)
{
	assert ("zam-509", node != NULL);
	assert ("zam-510", spin_jnode_is_locked (node));
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
