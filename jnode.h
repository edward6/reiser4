/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of jnode. */

#ifndef __JNODE_H__
#define __JNODE_H__

#include "forward.h"
#include "tshash.h"
#include "tslist.h"
#include "txnmgr.h"
#include "key.h"
#include "debug.h"
#include "dformat.h"
#include "spin_macros.h"
#include "emergency_flush.h"

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/bitops.h>
#include <linux/list.h>

/* declare hash table of jnodes (jnodes proper, that is, unformatted
   nodes)  */
TS_HASH_DECLARE(j, jnode);

/* declare hash table of znodes */
TS_HASH_DECLARE(z, znode);

typedef struct {
	__u64 objectid;
	unsigned long index;
	struct address_space *mapping;
} jnode_key_t;

/*
   Jnode is the "base class" of other nodes in reiser4. It is also happens to
   be exactly the node we use for unformatted tree nodes.

   Jnode provides following basic functionality:

   . reference counting and indexing.

   . integration with page cache. Jnode has ->pg reference to which page can
   be attached.

   . interface to transaction manager. It is jnode that is kept in transaction
   manager lists, attached to atoms, etc. (NOTE-NIKITA one may argue that this
   means, there should be special type of jnode for inode.)

   Locking: 
  
   Spin lock: the following fields are protected by the per-jnode spin lock:
  
    ->state
    ->atom
    ->capture_link

   Following fields are protected by the global tree lock:
  
    ->link
    ->key.z (content of ->key.z is only changed in znode_rehash())
    ->key.j

   Atomic counters
  
    ->x_count
    ->d_count 

    ->pg, and ->data are protected by spin lock for unused jnode and are
    immutable for used jnode (one for which fs/reiser4/vfs_ops.c:releasable()
    is false).

    ->tree is immutable after creation

   Unclear

    ->blocknr: should be under jnode spin-lock, but current interface is based
    on passing of block address.

   If you ever need to spin lock two nodes at once, do this in "natural"
   memory order: lock znode with lower address first. (See
   spin_lock_znode_pair() and spin_lock_znode_triple() functions, NOTE-NIKITA
   TDB)
  
   Invariants involving this data-type:

      [jnode-refs]
      [jnode-queued]
*/
struct jnode {
	/* jnode's state: bitwise flags from the reiser4_znode_state enum. */
	/*   0 */ unsigned long state;

	/* lock, protecting jnode's fields. */
	/*   4 */ reiser4_spin_data guard;

	/* counter of references to jnode's data. Pin data page(s) in
	   memory while this is greater than 0. Increased on jload().
	   Decreased on jrelse().
	*/
	/*   8 */ int d_count;

	/* counter of references to jnode itself. Increased on jref().
	   Decreased on jput().
	*/
	/*  12 */ atomic_t x_count;

	/* the real blocknr (where io is going to/from) */
	/*  16 */ reiser4_block_nr blocknr;

	/*  24 */ union {
		/* znodes are hashed by block number */
		reiser4_block_nr z;
		/* unformatted nodes are hashed by mapping plus offset */
		jnode_key_t j;
	} key;

	/* pointer to jnode page.  */
	/*  32 */ struct page *pg;
	/* pointer to node itself. This is page_address(node->pg) when page is
	   attached to the jnode
	*/
	/*  36 */ void *data;

	/*  40 */ union {
		/* pointers to maintain hash-table */
		z_hash_link z;
		j_hash_link j;
	} link;

	/* atom the block is in, if any */
	/*  44 */ txn_atom *atom;

	/* capture list */
	/*  48 */ capture_list_link capture_link;
	/*  52 */ reiser4_tree *tree;
	/*  56 */
#if REISER4_DEBUG
	/* list of all jnodes for debugging purposes. */
	struct list_head jnodes;
	/* how many times this jnode was written in one transaction */
	int      written;
#endif
};

TS_LIST_DEFINE(capture, jnode, capture_link);

typedef enum {
       /* data are loaded from node */
	JNODE_LOADED = 0,
       /* node was deleted, not all locks on it were released. This
	   node is empty and is going to be removed from the tree
	   shortly. */
       /* Josh respectfully disagrees with obfuscated, metaphoric names
	   such as this.  He thinks it should be named ZNODE_BEING_REMOVED. */
	JNODE_HEARD_BANSHEE = 1,
       /* left sibling pointer is valid */
	JNODE_LEFT_CONNECTED = 2,
       /* right sibling pointer is valid */
	JNODE_RIGHT_CONNECTED = 3,

       /* znode was just created and doesn't yet have a pointer from
	   its parent */
	JNODE_ORPHAN = 4,

       /* this node was created by its transaction and has not been assigned
	  a block address. */
	JNODE_CREATED = 5,

       /* this node is currently relocated */
	JNODE_RELOC = 6,
       /* this node is currently wandered */
	JNODE_OVRWR = 7,

       /* this znode has been modified */
	JNODE_DIRTY = 8,

	/* znode lock is being invalidated */
	JNODE_IS_DYING = 9,
	/* jnode of block which has pointer (allocated or unallocated) from
	   extent or something similar (indirect item, for example) */
	JNODE_MAPPED = 10,

	JNODE_EFLUSH = 11,

	/* jnode is queued for flushing. */
	JNODE_FLUSH_QUEUED = 12,

	/* In the following bits jnode type is encoded. */
	JNODE_TYPE_1 = 13,
	JNODE_TYPE_2 = 14,
	JNODE_TYPE_3 = 15,

       /* jnode is being destroyed */
	JNODE_RIP = 16,

       /* znode was not captured during locking (it might so be because
	  ->level != LEAF_LEVEL and lock_mode == READ_LOCK) */
	JNODE_MISSED_IN_CAPTURE = 17,

	/* write is in progress */
	JNODE_WRITEBACK = 18,

	/* used in plugin/item/extent.c */
	JNODE_NEW = 19,
	/* async io request was issued */
	JNODE_ASYNC = 20,
	JNODE_DKSET = 21,

	/* cheap and effective protection of jnode from emergency flush */
	JNODE_EPROTECTED = 22

} reiser4_znode_state;

/* Macros for accessing the jnode state. */
static inline void
JF_CLR(jnode * j, int f)
{
	clear_bit(f, &j->state);
}
static inline int
JF_ISSET(const jnode * j, int f)
{
	return test_bit(f, &((jnode *) j)->state);
}
static inline void
JF_SET(jnode * j, int f)
{
	set_bit(f, &j->state);
}

static inline int
JF_TEST_AND_SET(jnode * j, int f)
{
	return test_and_set_bit(f, &j->state);
}

/* ordering constraint for znode spin lock: znode lock is weaker than 
   tree lock and dk lock */
#define spin_ordering_pred_jnode( node )					\
	( ( lock_counters() -> rw_locked_tree == 0 ) &&			\
	  ( lock_counters() -> spin_locked_txnh == 0 ) &&                       \
	  ( lock_counters() -> rw_locked_dk == 0 )   &&                       \
	  /*                                                                    \
	     in addition you cannot hold more than one jnode spin lock at a     \
	     time.                                                              \
	  */                                                                   \
	  ( lock_counters() -> spin_locked_jnode == 0 ) )

/* Define spin_lock_jnode, spin_unlock_jnode, and spin_jnode_is_locked.
   Take and release short-term spinlocks.  Don't hold these across
   io. 
*/
SPIN_LOCK_FUNCTIONS(jnode, jnode, guard);

static inline int
jnode_is_in_deleteset(const jnode * node)
{
	return JF_ISSET(node, JNODE_RELOC);
}


extern int jnode_init_static(void);
extern int jnode_done_static(void);

/* Jnode routines */
extern jnode *jalloc(void);
extern void jfree(jnode * node);
extern jnode *jnew(void);
extern jnode *jget(reiser4_tree * tree, struct page *pg);
extern jnode *jfind(struct page *pg);
extern jnode *jlook(reiser4_tree *, oid_t objectid, unsigned long index);
extern jnode *jlook_lock(reiser4_tree * tree, oid_t objectid, unsigned long ind);
extern jnode *jnode_by_page(struct page *pg);
extern jnode *jnode_of_page(struct page *pg);
extern jnode *page_next_jnode(jnode * node);
extern void jnode_init(jnode * node, reiser4_tree * tree);
extern void jnode_set_dirty(jnode * node);
extern void jnode_set_clean_nolock(jnode * node);
extern void jnode_set_clean(jnode * node);
extern void jnode_set_block(jnode * node, const reiser4_block_nr * blocknr);
extern int jnode_io_hook(jnode *node, struct page *page, int rw);

extern struct page *jnode_lock_page(jnode *);

/* block number of node */
static inline const reiser4_block_nr *
jnode_get_block(const jnode * node /* jnode to query */)
{
	assert("nikita-528", node != NULL);

	return &node->blocknr;
}

static inline const reiser4_block_nr *
jnode_get_io_block(const jnode * node)
{
	assert("nikita-2768", node != NULL);
	assert("nikita-2769", spin_jnode_is_locked(node));

	if (unlikely(REISER4_USE_EFLUSH && JF_ISSET(node, JNODE_EFLUSH)))
		return eflush_get(node);
	else
		return jnode_get_block(node);
}

/* Jnode flush interface. */
extern int enqueue_unformatted(jnode * node, flush_pos_t * pos);
extern reiser4_blocknr_hint *pos_hint(flush_pos_t * pos);
extern int pos_leaf_relocate(flush_pos_t * pos);

/* FIXME-VS: these are used in plugin/item/extent.c */

/* does extent_get_block have to be called */
#define jnode_mapped(node)     JF_ISSET (node, JNODE_MAPPED)
#define jnode_set_mapped(node) JF_SET (node, JNODE_MAPPED)
/* pointer to this block was just created (either by appending or by plugging a
   hole), or zinit_new was called */
#define jnode_created(node)        JF_ISSET (node, JNODE_CREATED)
#define jnode_set_created(node)    JF_SET (node, JNODE_CREATED)

/* Macros to convert from jnode to znode, znode to jnode.  These are macros because C
   doesn't allow overloading of const prototypes. */
#define ZJNODE(x) (& (x) -> zjnode)
#define JZNODE(x)						\
({								\
	typeof (x) __tmp_x;					\
								\
	__tmp_x = (x);						\
	assert ("jmacd-1300", jnode_is_znode (__tmp_x));	\
	(znode*) __tmp_x;					\
})

extern int jnodes_tree_init(reiser4_tree * tree);
extern int jnodes_tree_done(reiser4_tree * tree);

#if REISER4_DEBUG
extern int znode_is_any_locked(const znode * node);
extern int jnode_invariant(const jnode * node, int tlocked, int jlocked);
#endif

#if REISER4_DEBUG_OUTPUT
extern void info_jnode(const char *prefix, const jnode * node);
extern void print_jnode(const char *prefix, const jnode * node);
extern void print_jnodes(const char *prefix, reiser4_tree * tree);
#else
#define info_jnode(p, n) noop
#define print_jnodes(p, t) noop
#define print_jnode(p, n) noop
#endif

extern int znode_is_root(const znode * node);

/* bump reference counter on @node */
static inline void
add_x_ref(jnode * node /* node to increase x_count of */ )
{
	assert("nikita-1911", node != NULL);

	atomic_inc(&node->x_count);
	ON_DEBUG_CONTEXT(++lock_counters()->x_refs);
}

/* jref() - increase counter of references to jnode/znode (x_count) */
static inline jnode *
jref(jnode * node)
{
	assert("jmacd-508", (node != NULL) && !IS_ERR(node));
	add_x_ref(node);
	return node;
}

extern int jdelete(jnode * node);

/* get the page of jnode */
static inline char *
jdata(const jnode * node)
{
	assert("nikita-1415", node != NULL);
	return node->pg ? node->data : NULL;
}

/* get the page of jnode */
static inline struct page *
jnode_page(const jnode * node)
{
	return node->pg;
}

static inline int
jnode_is_loaded(const jnode * node)
{
	assert("zam-506", node != NULL);
	return JF_ISSET(node, JNODE_LOADED);
}

extern void jnode_attach_page(jnode * node, struct page *pg);
extern void page_detach_jnode(struct page *page, struct address_space *mapping, unsigned long index);
extern void page_clear_jnode(struct page *page, jnode * node);
static inline int jnode_is_dirty(const jnode * node);

static inline int
jnode_is_flushprepped(const jnode * node)
{
	assert("jmacd-78212", node != NULL);
	assert("jmacd-71276", spin_jnode_is_locked(node));
	return !jnode_is_dirty(node) || JF_ISSET(node, JNODE_RELOC)
	    || JF_ISSET(node, JNODE_OVRWR);
}

/* Return true if @node has already been processed by the squeeze and allocate
   process.  This implies the block address has been finalized for the
   duration of this atom (or it is clean and will remain in place).  If this
   returns true you may use the block number as a hint. */
static inline int
jnode_check_flushprepped(jnode * node)
{
	/* It must be clean or relocated or wandered.  New allocations are set to relocate. */
	assert("jmacd-71275", spin_jnode_is_not_locked(node));
	return UNDER_SPIN(jnode, node, jnode_is_flushprepped(node));
}

static inline void
jnode_set_reloc(jnode * node)
{
	assert("nikita-2431", node != NULL);
	assert("nikita-2432", !JF_ISSET(node, JNODE_OVRWR));
	JF_SET(node, JNODE_RELOC);
}

static inline void
jnode_set_wander(jnode * node)
{
	assert("nikita-2431", node != NULL);
	assert("nikita-2432", !JF_ISSET(node, JNODE_RELOC));
	JF_SET(node, JNODE_OVRWR);
}

/* bump data counter on @node */
static inline void add_d_ref(jnode * node /* node to increase d_count of */ )
{
	assert("nikita-1962", node != NULL);

	++ node->d_count;
	ON_DEBUG_CONTEXT(++lock_counters()->d_refs);
}


/* jload/jwrite/junload give a bread/bwrite/brelse functionality for jnodes */

extern int jload_gfp(jnode * node, int gfp);

static inline int jload(jnode * node)
{
	return jload_gfp(node, GFP_KERNEL);
}

extern int jinit_new(jnode * node);
extern int jstartio(jnode * node);

extern int jdrop_in_tree(jnode * node, reiser4_tree * tree);
extern void jdrop(jnode * node);
extern int jwait_io(jnode * node, int rw);

extern void jrelse_nolock(jnode * node);

extern jnode *alloc_io_head(const reiser4_block_nr * block);
extern void drop_io_head(jnode * node);

static inline reiser4_tree *
jnode_get_tree(const jnode * node)
{
	assert("nikita-2691", node != NULL);
	return node->tree;
}

extern void pin_jnode_data(jnode *);
extern void unpin_jnode_data(jnode *);

typedef enum {
	JNODE_UNFORMATTED_BLOCK,
	JNODE_FORMATTED_BLOCK,
	JNODE_BITMAP,
	JNODE_CLUSTER_PAGE,
	JNODE_IO_HEAD,
	LAST_JNODE_TYPE
} jnode_type;

static inline jnode_type
jnode_get_type(const jnode * node)
{
	static const unsigned long state_mask =
	    (1 << JNODE_TYPE_1) | (1 << JNODE_TYPE_2) | (1 << JNODE_TYPE_3);

	static jnode_type mask_to_type[] = {
		/*  JNODE_TYPE_3 : JNODE_TYPE_2 : JNODE_TYPE_1 */

		/* 000 */
		[0] = JNODE_FORMATTED_BLOCK,
		/* 001 */
		[1] = JNODE_UNFORMATTED_BLOCK,
		/* 010 */
		[2] = JNODE_BITMAP,
		/* 011 */
		[3] = JNODE_CLUSTER_PAGE,
		/* 100 */
		[4] = LAST_JNODE_TYPE,	/* invalid */
		/* 101 */
		[5] = LAST_JNODE_TYPE,	/* invalid */
		/* 110 */
		[6] = JNODE_IO_HEAD,
		/* 111 */
		[7] = LAST_JNODE_TYPE,	/* invalid */
	};

	return mask_to_type[(node->state & state_mask) >> JNODE_TYPE_1];
}

extern void jnode_set_type(jnode * node, jnode_type type);

/* returns true if node is a znode */
static inline int
jnode_is_znode(const jnode * node)
{
	return jnode_get_type(node) == JNODE_FORMATTED_BLOCK;
}

/* return true if "node" is dirty */
static inline int
jnode_is_dirty(const jnode * node)
{
	assert("nikita-782", node != NULL);
	assert("jmacd-1800", spin_jnode_is_locked(node) || (jnode_is_znode(node) && znode_is_any_locked(JZNODE(node))));
	return JF_ISSET(node, JNODE_DIRTY);
}

/* return true if "node" is dirty, node is unlocked */
static inline int
jnode_check_dirty(jnode * node)
{
	assert("jmacd-7798", node != NULL);
	assert("jmacd-7799", spin_jnode_is_not_locked(node));
	return UNDER_SPIN(jnode, node, jnode_is_dirty(node));
}

/* returns true if node is unformatted */
static inline int
jnode_is_unformatted(const jnode * node)
{
	assert("jmacd-0123", node != NULL);
	return jnode_get_type(node) == JNODE_UNFORMATTED_BLOCK;
}

/* returns true if node represents a cluster cache page */
static inline int
jnode_is_cluster_page(const jnode * node)
{
	assert("edward-50", node != NULL);
	return jnode_get_type(node) == JNODE_CLUSTER_PAGE;
}

/* returns true if node is not a znode, but can have a "parent"
   coord in the tree */
static inline int
jnode_has_parent(const jnode * node)
{
	assert("edward-51", node != NULL);
	return jnode_is_unformatted(node) || jnode_is_cluster_page(node);
}


/* Get the index of a block. */
static inline unsigned long
jnode_get_index(jnode * node)
{
	assert("edward-52", jnode_has_parent(node));
	/*return jnode_page(node)->index;*/
	return node->key.j.index;
}

/* return true if "node" is the root */
static inline int
jnode_is_root(const jnode * node)
{
	return jnode_is_znode(node) && znode_is_root(JZNODE(node));
}

extern struct address_space * jnode_mapping(const jnode * node);
extern unsigned long jnode_index(const jnode * node);

extern int jnode_try_drop(jnode * node);

static inline void jput(jnode * node);
extern void jput_final(jnode * node);

/* jput() - decrement x_count reference counter on znode.
  
   Count may drop to 0, jnode stays in cache until memory pressure causes the
   eviction of its page. The c_count variable also ensures that children are
   pressured out of memory before the parent. The jnode remains hashed as
   long as the VM allows its page to stay in memory.
*/
static inline void
jput(jnode * node)
{
	trace_stamp(TRACE_ZNODES);

	assert("jmacd-509", node != NULL);
	assert("jmacd-510", atomic_read(&node->x_count) > 0);
	assert("nikita-3065", spin_jnode_is_not_locked(node));
	ON_DEBUG_CONTEXT(--lock_counters()->x_refs);

	if (atomic_dec_and_lock(&node->x_count, &node->guard.lock)) {
		spin_lock_jnode_acc(node, 0);

		/* A fast check for keeping node in cache. We always keep node
		 * in cache if its page is present and node was not marked for
		 * deletion */
		if (jnode_page(node) != NULL && !JF_ISSET(node, JNODE_HEARD_BANSHEE)) {
			spin_unlock_jnode(node);
			return;
		}

		/* More complicated cases are handled in jput_final() */
		jput_final(node);
	}
}

extern void jrelse(jnode * node);

/* protect @node from e-flush */
static inline int jprotect (jnode * node)
{
	int ret;

	LOCK_JNODE(node);

	assert("zam-836", !JF_ISSET(node, JNODE_EPROTECTED));

	JF_SET(node, JNODE_EPROTECTED);
	ret = JF_ISSET(node, JNODE_EFLUSH);

	UNLOCK_JNODE(node);

	if (ret)
		ret = emergency_unflush(node);

	return ret;
}

/* remove protection from e-flush */
static inline void junprotect (jnode * node)
{
	assert("zam-837", !JF_ISSET(node, JNODE_EFLUSH));
	assert("zam-838", JF_ISSET(node, JNODE_EPROTECTED));

	JF_CLR(node, JNODE_EPROTECTED);
}

/* __JNODE_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
