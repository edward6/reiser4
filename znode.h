/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Declaration of znode (Zam's node).
 */

#ifndef __ZNODE_H__
#define __ZNODE_H__

/** declare hash table of znodes */
TS_HASH_DECLARE(z, znode);

/* per-znode lock requests queue; list items are lock owner objects
   which want to lock given znode */
TS_LIST_DECLARE(requestors);
/* per-znode list of lock handles for this znode
 * 
 * Locking: protected by znode spin lock.
 */
TS_LIST_DECLARE(owners);
/* per-owner list of lock handles that point to locked znodes which
   belong to one lock owner 

   Locking: this list is only accessed by the thread owning lock stack this
   list is attached to. Hence, no locking is necessary.
*/
TS_LIST_DECLARE(locks);
  
/**
 * Per-znode lock object
 */
struct zlock {
        /**
	 * The number of readers if positive; the number of recursively taken
	 * write locks if negative */
	int nr_readers;
	/**
	 * A number of processes (lock_stacks) that have this object
	 * locked with high priority */
	unsigned nr_hipri_owners;
	/**
	 * A number of attempts to lock znode in high priority direction */
	unsigned nr_hipri_requests;
	/**
	 * A linked list of lock_handle objects that contains pointers
	 * for all lock_stacks which have this lock object locked */
	owners_list_head owners;
	/**
	 * A linked list of lock_stacks that wait for this lock */
	requestors_list_head requestors;
};

/* This structure is way too large.  Think for a moment.  There is one
   of these for every 4k formatted node.  That's a lot of bytes.
   Don't carelessly add bloat here (or anywhere, this is not user
   space office suite programming we are doing) .  */

/**
 * &znode - node in a reiser4 tree.
 *
 * FIXME-NIKITA fields in this struct have to be rearranged (later) to reduce
 * cacheline pressure.
 *
 * Locking: 
 *
 * Long term: data in a disk node attached to this znode are protected
 * by long term, deadlock aware lock ->lock;
 *
 * Spin lock: the following fields are protected by the spin lock:
 *
 *  (jnode fields:)
 *  ->state
 *  ->level
 *  ->atom
 *
 *  (znode fields:)
 *  ->tree
 *  ->blocknr
 *  ->node_plugin (see below)
 *  ->data (see below)
 *  ->size (see below)
 *
 * Following fields are protected by the global tree lock:
 *
 *  ->left
 *  ->right
 *  ->in_parent
 *  ->link
 *
 * Following fields are protected by the global delimiting key lock (dk_lock):
 *
 *  ->ld_key
 *  ->rd_key
 *
 * Atomic counters
 *
 *  ->x_count
 *  ->d_count 
 *  ->c_count
 *
 * can be accessed and modified without locking
 *
 * If you ever need to spin lock two nodes at once, do this in "natural"
 * memory order: lock znode with lower address first. (See
 * spin_lock_znode_pair() and spin_lock_znode_triple() functions, FIXME-NIKITA
 * TDB)
 *
 * ->node_plugin, ->data, and ->size are never changed once set. This
 * means that after code made itself sure (by checking ->zstate) that
 * fields are valid they can be accessed without any additional locking.
 *
 *
 */
struct jnode
{
	/* jnode's state: bitwise flags from the reiser4_znode_state enum. */
	unsigned long/*__u32*/        state;

	/* znode's tree level */
	__u16        level;

	/* lock, protecting jnode's fields. */
	/* 
	 * FIXME_JMACD: Can be probably combined with spinning atomic ops on
	 * STATE. 
	 *
	 * FIXME-NIKITA This means what? Reusing spare bits in spinlock_t for
	 * state? This is unportable.
	 *
	 * FIXME_JMACD: Yes, that's what we mean.  Surely it can be made
	 * portable?
	 */
	spinlock_t   guard;

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

/* The znode is an extension of jnode. */
struct znode {
	/* Embedded jnode. */
	jnode zjnode;
	
	/* design note: I think that tree traversal will be more
	   efficient because of these pointers, but there will be bugs
	   associated with these pointers.  We could simply record the
	   left and right delimiting keys, and that would be enough to
	   find the neighbors using search_by_key() from the root, but
	   I think this will reduce cache/memory bandwidth consumption
	   compared to doing that.  We could also just use
	   search_by_key(), and unfortunately this code looks
	   complicated enough to make that arguably correct to do.  */
	/* get lock on left znode before modifying this field, you don't
	   need a lock on this znode to modify this field. Conceptually,
	   these fields are properties not of this node, but of the
	   nodes they point to.  This field is zero if the neighbor node
	   is not in memory. */
	znode *left;
	/* get lock on right znode before modifying this field, you
	   don't need a lock on this znode to modify this
	   field. Conceptually, these fields are properties not of this
	   node, but of the nodes they point to.  This field is zero if
	   the neighbor node is not in memory.  */
	znode *right;
	/*  This is a lock that yields right of way to processes that
	    are locking in the rightward direction so as to ensure
	    deadlock avoidance, read the lock_left() and lock_right()
	    functions to understand this.  */
	/* locks this znode (and the node the znode points to) except
	   for the pointers from this znode to other znodes, and locks
	   the pointers from other znodes to this znode. */
	/* Some feel that this lock is too large grained and that for
	   some operations on the znode spinlocks should be used.  I
	   feel that we should optimize this later, if it turns out to
	   be significant. -Hans */

	/* znode lock object */
	zlock lock;

	/* what about when it is unallocated?  Did we ever resolve how
	   we were going to find buffers with unallocated blocknrs?
	   Negative blocknrs or what?  Also, is this consistent with
	   the bio paradigm?  Nikita?  Monstr?  -Hans */
	/** buffer head attached to this znode */
	/* 	struct buffer_head *buffer; */

	/**
	 * You cannot remove from memory a node that has children in
	 * memory. This is because we rely on the fact that parent of given
	 * node can always be reached without blocking for io. When reading a
	 * node into memory you must increase the c_count of its parent, when
	 * removing it from memory you must decrease the c_count.  This makes
	 * the code simpler, and the cases where it is suboptimal are truly
	 * obscure.
	 *
	 * All three znode reference counters ([cdx]_count) are atomic_t
	 * because we don't want to take and release spinlock for each
	 * reference addition/drop.
	 */
	atomic_t               c_count;
	/**
	 * counter of references to znode's data. Pin data page(s) in
	 * memory while this is greater than 0. Increased on zload().
	 * Decreased on zrelse().
	 */
	atomic_t               d_count;
	/**
	 * counter of references to znode itself. Increased on zref().
	 * Decreased on zput().
	 */
	atomic_t               x_count;
	/** pointers to maintain hash-table */
	z_hash_link            link;

	/** plugin of node attached to this znode. NULL if znode is not
	    loaded. */
	node_plugin           *nplug;

	/** version of znode data. This is increased on each modification. */
	__u64                  version;

	/** 
	 * size of node referenced by this znode. This is not necessary
	 * block size, because there znodes for extents.
	 */
	/* removed for now. We only support blocksize == PAGE_CACHE_SIZE
	unsigned      size;
	*/

	/* Let's review why we need delimiting keys other than in the
	   least common parent node.  It is so as to not have to get a
	   lock on the least common parent node? -Hans */
	/**
	 * left delimiting key. Necessary to efficiently perform
	 * balancing with node-level locking. Kept in memory only.
	 */
	reiser4_key            ld_key;
	/**
	 * right delimiting key.
	 */
	reiser4_key            rd_key;

	/**
	 * position of this node in a parent node. This is cached to
	 * speed up lookups during balancing. Not required to be up to
	 * date. Synched in find_child_ptr().
	 *
	 * This value allows us to avoid expensive binary searches.
	 *
	 * Also, parent pointer is stored here.  The parent pointer
	 * stored here is NOT a hint, only the position is.
	 */
	coord_t            ptr_in_parent_hint;

#if REISER4_DEBUG_MODIFY
	/**
	 * In debugging mode, used to detect loss of znode_set_dirty()
	 * notification.
	 */
	__u32                  cksum; 
#endif 
};

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
       ZNODE_NEW               = 4,

       /* The jnode is a unformatted node.  False for all znodes.  */
       ZNODE_UNFORMATTED       = 5,

       /** this node was allocated by its txn */
       ZNODE_ALLOC             = 6,
       /** this node is currently relocated */
       ZNODE_RELOC             = 7,
       /** this node is currently wandered */
       ZNODE_WANDER            = 8,
	   
       /** this node was deleted by its txn.  Eliminated because the
	* znode/jnode will be released as soon as possible.  The atom doesn't
	* need to keep track of deleted nodes, and this also allows us to
	* delete nodes that are not in memory (which will be common for
	* extents). */
       /*ZNODE_DELETED           = */

       /** this znode has been modified */
       ZNODE_DIRTY             = 9,
       /** this znode has been modified */
       ZNODE_WRITEOUT          = 10,

       /* znode lock is being invalidated */
       ZNODE_IS_DYING          = 11,
} reiser4_znode_state;

/* Macros for accessing the znode state. */
#define	ZF_CLR(p,f)		JF_CLR(ZJNODE(p), (f))
#define	ZF_ISSET(p,f)	        JF_ISSET(ZJNODE(p), (f))
#define	ZF_SET(p,f)		JF_SET(ZJNODE(p), (f))

/* Macros for accessing the jnode state. */
#define	JF_CLR(j,f)   clear_bit((f), &((jnode *)j)->state)
#define	JF_ISSET(j,f) test_bit ((f), &((jnode *)j)->state)
#define	JF_SET(j,f)   set_bit  ((f), &((jnode *)j)->state)
/**
 * Since we have R/W znode locks we need addititional `link' objects to
 * implement n<->m relationship between lock owners and lock objects. We call
 * them `lock handles'.
 */
struct lock_handle {
	/**
	 * This flag indicates that a signal to yield a lock was passed to
	 * lock owner and counted in owner->nr_signalled 
	 *
	 * Locking: this is accessed under spin lock on ->node.
	 */
	int signaled;
	/**
	 * A link to owner of a lock */
	lock_stack *owner;
	/**
	 * A link to znode locked */
	znode *node;
	/**
	 * A list of all locks for a process */
	locks_list_link locks_link;
	/**
	 * A list of all owners for a znode */
	owners_list_link owners_link;
};

/**
 * A lock stack structure for accumulating locks owned by a process
 */
struct lock_stack {
	/**
	 * A guard lock protecting a lock stack */
	spinlock_t sguard;
	/**
	 * number of znodes which were requested by high priority processes */
	atomic_t nr_signaled;
	/**
	 * Current priority of a process 
	 *
	 * This is only accessed by the current thread and thus requires no
	 * locking.
	 */
	int curpri;
	/**
	 * A list of all locks owned by this process */
	locks_list_head locks;
	/**
	 * When lock_stack waits for the lock, it puts itself on double-linked
	 * requestors list of that lock */
	requestors_list_link requestors_link;
	/**
	 * Current lock request info.
	 *
	 * This is only accessed by the current thread and thus requires no
	 * locking.
	 */
	struct {
		/**
		 * A pointer to uninitialized link object */
		lock_handle *handle;
		/*
		 * A pointer to the object we want to lock */
		znode *node;
		/**
		 * Lock mode (ZNODE_READ_LOCK or ZNODE_WRITE_LOCK) */
		znode_lock_mode mode;
	} request;
	/**
	 * It is a lock_stack's synchronization object for when process sleeps
	 * when requested lock not on this lock_stack but which it wishes to
	 * add to this lock_stack is not immediately available. It is used
	 * instead of wait_queue_t object due to locking problems (lost wake
	 * up). "lost wakeup" occurs when process is waken up before he actually
	 * becomes 'sleepy' (through sleep_on()). Using of semaphore object is
	 * simplest way to avoid that problem.
	 *
	 * A semaphore is used in the following way: only the process that is
	 * the owner of the lock_stack initializes it (to zero) and calls
	 * down(sema) on it. Usually this causes the process to sleep on the
	 * semaphore. Other processes may wake him up by calling up(sema). The
	 * advantage to a semaphore is that up() and down() calls are not
	 * required to preserve order. Unlike wait_queue it works when process
	 * is woken up before getting to sleep. 
	 *
	 * FIXME-NIKITA: Transaction manager is going to have condition variables
	 * (&kcondvar_t) anyway, so this probably will be replaced with
	 * one in the future.
	 *
	 * After further discussion, Nikita has shown me that Zam's implementation is
	 * exactly a condition variable.  The znode's {zguard,requestors_list} represents
	 * condition variable and the lock_stack's {sguard,semaphore} guards entry and
	 * exit from the condition variable's wait queue.  But the existing code can't
	 * just be replaced with a more general abstraction, and I think its fine the way
	 * it is. */
	struct semaphore sema;
};

/*****************************************************************************\
 * User-visible znode locking functions
\*****************************************************************************/

extern int longterm_lock_znode     (lock_handle *handle,
				   znode               *node,
				   znode_lock_mode      mode,
				   znode_lock_request   request);
extern void longterm_unlock_znode  (lock_handle *handle);

extern int check_deadlock ( void );

extern lock_stack *get_current_lock_stack (void);

extern void init_lock_stack (lock_stack * owner);
extern void reiser4_init_lock (zlock * lock);

extern void init_lh (lock_handle*);
extern void move_lh (lock_handle *new, lock_handle *old);
extern void copy_lh (lock_handle *new, lock_handle *old);
extern void done_lh (lock_handle*);

extern int  prepare_to_sleep (lock_stack *owner);
extern void go_to_sleep      (lock_stack *owner);
extern void __reiser4_wake_up          (lock_stack *owner);

extern void show_lock_stack    (reiser4_context    *owner);
extern int  lock_stack_isclean (lock_stack *owner);

/* zlock object state check macros: only used in assertions.  Both forms imply that the
 * lock is held by the current thread. */
#if REISER4_DEBUG
extern int znode_is_any_locked( const znode *node );
extern int znode_is_write_locked( const znode *node );
#endif

/* lock ordering is: first take znode spin lock, then lock stack spin lock */
#define spin_ordering_pred_stack(stack) (1)
/** Same for lock_stack */
SPIN_LOCK_FUNCTIONS(stack,lock_stack,sguard);

static inline void reiser4_wake_up (lock_stack *owner)
{
	spin_lock_stack(owner);
	__reiser4_wake_up(owner);
	spin_unlock_stack(owner);
}

extern void add_x_ref( znode *node );
extern void add_d_ref( znode *node );
extern znode *zref( znode *node );
extern znode *zget( reiser4_tree *tree, const reiser4_block_nr *const block,
		    znode *parent, tree_level level, int gfp_flag );
extern znode *zlook( reiser4_tree *tree, const reiser4_block_nr *const block );
extern void zput( znode *node );
extern int zload( znode *node );
extern int zinit_new( znode *node );
extern int zunload( znode *node );
extern int zrelse( znode *node );
extern void znode_change_parent( znode *new_parent, reiser4_block_nr *block );

extern unsigned znode_size( const znode *node );
extern unsigned znode_free_space( znode *node );
extern int znode_is_loaded( const znode *node );
extern int znode_is_loaded_nolock( const znode *node );

extern reiser4_key *znode_get_rd_key( znode *node );
extern reiser4_key *znode_get_ld_key( znode *node );

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

/* Macros to convert from jnode to znode, znode to jnode.  These are macros because C
 * doesn't allow overloading of const prototypes. */
#define ZJNODE(x) (& (x) -> zjnode)
#define JZNODE(x) 							\
({									\
	assert ("jmacd-1300", !JF_ISSET ((x), ZNODE_UNFORMATTED));	\
	(znode*) (x);							\
})

/** `connected' state checks */
static inline int znode_is_right_connected (const znode * node)
{
	return ZF_ISSET (node, ZNODE_RIGHT_CONNECTED);
}

static inline int znode_is_left_connected (const znode * node)
{
	return ZF_ISSET (node, ZNODE_LEFT_CONNECTED);
}

static inline int znode_is_connected (const znode * node)
{
	return znode_is_right_connected (node) && znode_is_left_connected (node);
}

static inline int jnode_is_in_deleteset( const jnode *node )
{
	/* FIXME: Zam, this needs to actually do a lookup in the delete set, right? */
	return JF_ISSET( node, ZNODE_RELOC )  /*|| JF_ISSET( node, ZNODE_DELETED )*/;
}

extern int znode_rehash( znode *node, const reiser4_block_nr *new_block_nr );
extern znode *znode_parent( const znode *node );
extern znode *znode_parent_nolock( const znode *node );
extern int znode_above_root (const znode *node);
extern int znode_is_root( const znode *node );
extern int znode_is_true_root( const znode *node );
extern void zdestroy( znode *node );
extern int  znodes_init( void );
extern int  znodes_done( void );
extern int  znodes_tree_init( reiser4_tree *ztree );
extern void znodes_tree_done( reiser4_tree *ztree );
extern int znode_contains_key( znode *node, const reiser4_key *key );
extern int znode_contains_key_lock( znode *node, const reiser4_key *key );
extern int znode_invariant( const znode *node );
extern unsigned znode_save_free_space( znode *node );
extern unsigned znode_recover_free_space( znode *node );

#if REISER4_DEBUG_MODIFY
extern void znode_pre_write( znode *node );
extern void znode_post_write( const znode *node );
#endif

const char *lock_mode_name( znode_lock_mode lock );

#if REISER4_DEBUG
void print_znode( const char *prefix, const znode *node );
void info_znode( const char *prefix, const znode *node );
#else
#define print_znode( p, n ) noop
#define info_znode( p, n ) noop
#endif

/**
 * Jnode routines
 */
extern jnode* jnode_by_page   (struct page* pg);
extern jnode* jnode_of_page   (struct page* pg);
extern jnode* page_next_jnode (jnode *node);
extern void   jnode_init      (jnode *node);
extern void   jnode_set_dirty (jnode *node);
extern void   jnode_set_clean (jnode *node);
extern const reiser4_block_nr* jnode_get_block( const jnode *node );
extern int                     jnode_has_block( jnode * );
extern void   jnode_set_block (jnode *node, const reiser4_block_nr *blocknr);
extern int    jnode_flush     (jnode *node, int flags);

#if REISER4_DEBUG
void info_jnode( const char *prefix, const jnode *node );
#else
#define info_jnode( p, n ) noop
#endif

/* Similar to zref() and zput() for jnodes, calls those routines if the node is formatted. */
extern jnode *jref( jnode *node );
extern void   jput( jnode *node );

/* Make it look like various znode functions exist instead of treating znodes as
 * jnodes in znode-specific code. */
#define znode_page(x)               jnode_page ( ZJNODE(x) )
#define zdata(x)                    jdata ( ZJNODE(x) )
#define znode_get_level(x)          jnode_get_level ( ZJNODE(x) )
#define znode_set_level(x,l)        jnode_set_level ( ZJNODE(x), (l) )
#define znode_get_block(x)          jnode_get_block ( ZJNODE(x) )

#define znode_is_dirty(x)           jnode_is_dirty    ( ZJNODE(x) )
#define znode_check_dirty(x)        jnode_check_dirty ( ZJNODE(x) )
#define znode_set_dirty(x)          jnode_set_dirty   ( ZJNODE(x) )
#define znode_set_clean(x)          jnode_set_clean   ( ZJNODE(x) )
#define znode_set_block(x, b)       jnode_set_block ( ZJNODE(x), (b) )

#define spin_lock_znode(x)          spin_lock_jnode ( ZJNODE(x) )
#define spin_unlock_znode(x)        spin_unlock_jnode ( ZJNODE(x) )
#define spin_trylock_znode(x)       spin_trylock_jnode ( ZJNODE(x) )
#define spin_znode_is_locked(x)     spin_jnode_is_locked ( ZJNODE(x) )
#define spin_znode_is_not_locked(x) spin_jnode_is_not_locked ( ZJNODE(x) )

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
	return JF_ISSET (node, ZNODE_UNFORMATTED);
}

/* returns true if node is formatted, i.e, it's a znode */
static inline int jnode_is_formatted( const jnode *node)
{
	return ! JF_ISSET (node, ZNODE_UNFORMATTED);
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
extern jnode *page_detach_jnode( struct page *page );
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

/** return true if "node" is the root */
static inline int jnode_is_root (const jnode *node)
{
	return jnode_is_formatted (node) && znode_is_root (JZNODE (node));
}

/* __ZNODE_H__ */
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
