/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */
/* Jnode manipulation functions. */
/* Jnode is entity used to track blocks with data and meta-data in reiser4.

   In particular, jnodes are used to track transactional information
   associated with each block. Each znode contains jnode as ->zjnode field.

   Jnode stands for either Josh or Journal node.
*/

/*
 * Taxonomy.
 *
 *     Jnode represents block containing data or meta-data. There are jnodes
 *     for:
 *
 *         unformatted blocks (jnodes proper). There are plans, however to
 *         have a handle per extent unit rather than per each unformatted
 *         block, because there are so many of them.
 *
 *         For bitmaps. Each bitmap is actually represented by two jnodes--one
 *         for working and another for "commit" data, together forming bnode.
 *
 *         For io-heads. These are used by log writer.
 *
 *         For formatted nodes (znode). See comment at the top of znode.c for
 *         details specific to the formatted nodes (znodes).
 *
 * Node data.
 *
 *     Jnode provides access to the data of node it represents. Data are
 *     stored in a page. Page is kept in a page cache. This means, that jnodes
 *     are highly interconnected with page cache and VM internals.
 *
 *     jnode has a pointer to page (->pg) containing its data. Pointer to data
 *     themselves is cached in ->data field to avoid frequent calls to
 *     page_address().
 *
 *     jnode and page are attached to each other by jnode_attach_page(). This
 *     function places pointer to jnode in page->private, sets PG_private flag
 *     and increments page counter.
 *
 *     Opposite operation is performed by page_clear_jnode().
 *
 *     jnode->pg is protected by jnode spin lock, and page->private is
 *     protected by page lock. See comment at the top of page_cache.c for
 *     more.
 *
 *     page can be detached from jnode for two reasons:
 *
 *         . jnode is removed from a tree (file is truncated, of formatted
 *         node is removed by balancing).
 *
 *         . during memory pressure, VM calls ->releasepage() method
 *         (reiser4_releasepage()) to evict page from memory.
 *
 *    (there, of course, is also umount, but this is special case we are not
 *    concerned with here).
 *
 *    To protect jnode page from eviction, one calls jload() function that
 *    "pins" page in memory (loading it if necessary), increments
 *    jnode->d_count, and kmap()s page. Page is unpinned through call to
 *    jrelse().
 *
 * Jnode life cycle.
 *
 *    jnode is created, placed in hash table, and, optionally, in per-inode
 *    radix tree. Page can be attached to jnode, pinned, released, etc.
 *
 *    When jnode is captured into atom its reference counter is
 *    increased. While being part of an atom, jnode can be "early
 *    flushed". This means that as part of flush procedure, jnode is placed
 *    into "relocate set", and its page is submitted to the disk. After io
 *    completes, page can be detached, then loaded again, re-dirtied, etc.
 *
 *    Thread acquired reference to jnode by calling jref() and releases it by
 *    jput(). When last reference is removed, jnode is still retained in
 *    memory (cached) if it has page attached, _unless_ it is scheduled for
 *    destruction (has JNODE_HEARD_BANSHEE bit set).
 *
 *    Tree read-write lock was used as "existential" lock for jnodes. That is,
 *    jnode->x_count could be changed from 0 to 1 only under tree write lock,
 *    that is, tree lock protected unreferenced jnodes stored in the hash
 *    table, from recycling.
 *
 *    This resulted in high contention on tree lock, because jref()/jput() is
 *    frequent operation. To ameliorate this problem, RCU is used: when jput()
 *    is just about to release last reference on jnode it sets JNODE_RIP bit
 *    on it, and then proceed with jnode destruction (removing jnode from hash
 *    table, cbk_cache, detaching page, etc.). All places that change jnode
 *    reference counter from 0 to 1 (jlookup(), zlook(), and
 *    cbk_cache_scan_slots()) check for JNODE_RIP bit (this is done by
 *    jnode_rip_check() function), and pretend that nothing was found in hash
 *    table if bit is set.
 *
 *    jput defers actual return of jnode into slab cache to some later time
 *    (by call_rcu()), this guarantees that other threads can safely continue
 *    working with JNODE_RIP-ped jnode.
 *
 */

#include "reiser4.h"
#include "debug.h"
#include "dformat.h"
#include "plugin/plugin_header.h"
#include "plugin/plugin.h"
#include "plugin/plugin_hash.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "tree.h"
#include "tree_walk.h"
#include "super.h"
#include "inode.h"
#include "page_cache.h"
#include "prof.h"

#include <asm/uaccess.h>        /* UML needs this for PAGE_OFFSET */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>      /* for vmalloc(), vfree() */
#include <linux/swap.h>
#include <linux/fs.h>		/* for struct address_space  */
#include <linux/writeback.h>	/* for inode_lock */

static kmem_cache_t *_jnode_slab = NULL;

/* true if valid page is attached to jnode */
static inline int jnode_is_parsed (jnode * node)
{
	return JF_ISSET(node, JNODE_PARSED);
}

/* hash table support */

/* compare two jnode keys for equality. Used by hash-table macros */
static inline int
jnode_key_eq(const jnode_key_t * k1, const jnode_key_t * k2)
{
	assert("nikita-2350", k1 != NULL);
	assert("nikita-2351", k2 != NULL);

	return (k1->index == k2->index && k1->objectid == k2->objectid);
}

/* Hash jnode by its key (inode plus offset). Used by hash-table macros */
static inline __u32
jnode_key_hashfn(j_hash_table *table, const jnode_key_t * key)
{
	assert("nikita-2352", key != NULL);
	assert("nikita-3346", IS_POW(table->_buckets));

	/* yes, this is remarkable simply (where not stupid) hash function. */
	return (key->objectid + key->index) & (table->_buckets - 1);
}

/* The hash table definition */
#define KMALLOC(size) vmalloc(size)
#define KFREE(ptr, size) vfree(ptr)
TYPE_SAFE_HASH_DEFINE(j, jnode, jnode_key_t, key.j, link.j, jnode_key_hashfn, jnode_key_eq);
#undef KFREE
#undef KMALLOC

/* call this to initialise jnode hash table */
int
jnodes_tree_init(reiser4_tree * tree /* tree to initialise jnodes for */ )
{
	int buckets;
	int result;

	assert("nikita-2359", tree != NULL);

	/*
	 * number of hash buckets in hash table depends on amount of memory
	 * available. If we cannot allocate that much, number of buckets is
	 * halved until allocation succeeds.
	 */
	buckets = 1 << fls(nr_free_pagecache_pages());
	do {
		result = j_hash_init(&tree->jhash_table, buckets,
				     reiser4_stat(tree->super, hashes.jnode));
		buckets >>= 1;
	} while (result == -ENOMEM);
	return result;
}

/* call this to destroy jnode hash table. This is called during umount. */
int
jnodes_tree_done(reiser4_tree * tree /* tree to destroy jnodes for */ )
{
	j_hash_table *jtable;
	jnode *node;
	jnode *next;

	assert("nikita-2360", tree != NULL);

	/*
	 * Scan hash table and free all jnodes.
	 */

	IF_TRACE(TRACE_ZWEB, UNDER_RW_VOID(tree, tree, read,
					   print_jnodes("umount", tree)));

	jtable = &tree->jhash_table;
	for_all_in_htable(jtable, j, node, next) {
		assert("nikita-2361", !atomic_read(&node->x_count));
		jdrop(node);
	}

	j_hash_done(&tree->jhash_table);
	return 0;
}

/* Initialize static variables in this file. */
int
jnode_init_static(void)
{
	assert("umka-168", _jnode_slab == NULL);

	_jnode_slab = kmem_cache_create("jnode", sizeof (jnode), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (_jnode_slab == NULL) {
		goto error;
	}

	return 0;

error:

	if (_jnode_slab != NULL) {
		kmem_cache_destroy(_jnode_slab);
	}
	return RETERR(-ENOMEM);
}

int
jnode_done_static(void)
{
	int ret = 0;

	if (_jnode_slab != NULL) {
		ret = kmem_cache_destroy(_jnode_slab);
		_jnode_slab = NULL;
	}

	return ret;
}

/* Initialize a jnode. */
void
jnode_init(jnode * node, reiser4_tree * tree, jnode_type type)
{
	assert("umka-175", node != NULL);

	xmemset(node, 0, sizeof (jnode));
	ON_DEBUG(node->magic = JMAGIC);
	node->state = 0;
	jnode_set_type(node, type);
	atomic_set(&node->d_count, 0);
	atomic_set(&node->x_count, 0);
	spin_jnode_init(node);
	spin_jload_init(node);
	node->atom = NULL;
	node->tree = tree;
	capture_list_clean(node);
	ON_DEBUG(node->list = NOT_CAPTURED);
	INIT_RCU_HEAD(&node->rcu);

#if REISER4_DEBUG
	{
		reiser4_super_info_data *sbinfo;

		sbinfo = get_super_private(tree->super);
		spin_lock_irq(&sbinfo->all_guard);
		list_add(&node->jnodes, &sbinfo->all_jnodes);
		spin_unlock_irq(&sbinfo->all_guard);
	}
#endif
}

#if REISER4_DEBUG
/*
 * Remove jnode from ->all_jnodes list. See comment for this field in super.h
 */
void
jnode_done(jnode * node, reiser4_tree * tree)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = get_super_private(tree->super);

	spin_lock_irq(&sbinfo->all_guard);
	assert("nikita-2422", !list_empty(&node->jnodes));
	list_del_init(&node->jnodes);
	assert("nikita-3218", atomic_read(&sbinfo->jnodes_in_flight) > 0);
	atomic_dec(&sbinfo->jnodes_in_flight);
	kcond_signal(&sbinfo->rcu_done);
	spin_unlock_irq(&sbinfo->all_guard);
}
#endif

/* return already existing jnode of page */
jnode *
jnode_by_page(struct page *pg)
{
	assert("nikita-2066", pg != NULL);
	assert("nikita-2400", PageLocked(pg));
	assert("nikita-2068", PagePrivate(pg));
	assert("nikita-2067", jprivate(pg) != NULL);
	return jprivate(pg);
}

/* exported functions to allocate/free jnode objects outside this file */
jnode *
jalloc(void)
{
	jnode *jal = kmem_cache_alloc(_jnode_slab, GFP_KERNEL);
	return jal;
}

/* return jnode back to the slab allocator */
inline void
jfree(jnode * node)
{
	assert("zam-449", node != NULL);

	assert("nikita-2663", capture_list_is_clean(node) && node->list == NOT_CAPTURED);
	assert("nikita-2774", !JF_ISSET(node, JNODE_EFLUSH));
	assert("nikita-3222", list_empty(&node->jnodes));
	assert("nikita-3221", jnode_page(node) == NULL);

	/* not yet phash_jnode_destroy(node); */

	/* poison memory. */
	ON_DEBUG(xmemset(node, 0xad, sizeof *node));
	kmem_cache_free(_jnode_slab, node);
}

/* allocate new unformatted jnode */
jnode *
jnew_unformatted(void)
{
	jnode *jal;

	jal = jalloc();
	if (jal == NULL)
		return NULL;

	jnode_init(jal, current_tree, JNODE_UNFORMATTED_BLOCK);
	jal->key.j.mapping = 0;
	jal->key.j.index = (unsigned long)-1;
	jal->key.j.objectid = 0;
	return jal;
}

/* look for jnode with given mapping and offset within hash table */
jnode *
jlookup(reiser4_tree * tree, oid_t objectid, unsigned long index)
{
	jnode_key_t jkey;
	jnode *node;

	assert("nikita-2353", tree != NULL);

	jkey.objectid = objectid;
	jkey.index = index;

	rcu_read_lock();
	node = j_hash_find(&tree->jhash_table, &jkey);
	if (node != NULL) {
		/* protect @node from recycling */
		jref(node);
		assert("nikita-2955", jnode_invariant(node, 0, 0));
		node = jnode_rip_check(tree, node);
	}
	rcu_read_unlock();
	return node;
}

static int
inode_has_no_jnodes(reiser4_inode *r4_inode)
{
	if (r4_inode->jnode_tree.rnode == 0) {
		assert("vs-1434", r4_inode->jnodes == 0);
		assert("vs-1435", (inode_by_reiser4_inode(r4_inode)->i_state & I_JNODES) == 0);
		return 1;
	}
	assert("vs-1436", r4_inode->jnodes > 0);
	assert("vs-1437", (inode_by_reiser4_inode(r4_inode)->i_state & I_JNODES) != 0);
	return 0;
}


/* insert jnode into reiser4 inode's radix tree of jnodes. This is performed under tree spin lock. It also sets a bit
   (I_JNODES) in inode's i_state so that fs/inode.c:can_unuse never returns 1, so, jnodes in inode's jnode tree prevent
   inode from being pruned. This is important because jnodes store pointer to inode's mapping */
static void
inode_attach_jnode(jnode *node)
{
	struct inode *inode;
	reiser4_inode *r4_inode;

	assert("vs-1439", node->key.j.mapping);

	inode = node->key.j.mapping->host;
	r4_inode = reiser4_inode_data(inode);
	spin_lock(&inode_lock);
	if (inode_has_no_jnodes(r4_inode)) {
		assert("vs-1433", (inode->i_state & I_JNODES) == 0);
		inode->i_state |= I_JNODES;
	}
	check_me("vs-1431", radix_tree_insert(&r4_inode->jnode_tree, node->key.j.index, node) == 0);
	ON_DEBUG(r4_inode->jnodes ++);
	spin_unlock(&inode_lock);
}

/* remove jnode into reiser4 inode's radix tree. This is performed under tree spin lock. If last jnode is removed from
   inode's jnode tree inode gets "released" - bit I_JNODES is cleared */
static void
inode_detach_jnode(jnode *node)
{
	struct inode *inode;
	reiser4_inode *r4_inode;

	assert("vs-1440", node->key.j.mapping);
	inode = node->key.j.mapping->host;
	assert("vs-1441", node->key.j.objectid == get_inode_oid(inode));
	r4_inode = reiser4_inode_data(inode);
	assert("vs-1431", r4_inode->jnodes > 0 && (inode->i_state & I_JNODES));

	spin_lock(&inode_lock);
	check_me("vs-1431", radix_tree_delete(&r4_inode->jnode_tree, jnode_get_index(node)));
	ON_DEBUG(r4_inode->jnodes --);
	if (r4_inode->jnode_tree.rnode == 0) {
		assert("vs-1432", inode->i_state & I_JNODES);
		assert("vs-1432", r4_inode->jnodes == 0);
		inode->i_state &= ~I_JNODES;
	}
	spin_unlock(&inode_lock);
}

/* put jnode into hash table (where they can be found by flush who does not know mapping) and to inode's tree of jnodes
   (where they can be found (hopefully faster) in places where mapping is known). Currently it is used by
   fs/reiser4/plugin/item/extent_file_ops.c:index_extent_jnode when new jnode is created */
void
hash_unformatted_jnode(jnode *node, struct address_space *mapping, unsigned long index)
{
	j_hash_table *jtable;

	assert("vs-1446", jnode_is_unformatted(node));
	assert("vs-1442", node->key.j.mapping == 0);
	assert("vs-1443", node->key.j.objectid == 0);
	assert("vs-1444", node->key.j.index == (unsigned long)-1);
	
	node->key.j.mapping  = mapping;
	node->key.j.objectid = get_inode_oid(mapping->host);
	node->key.j.index    = index;

	jtable = &node->tree->jhash_table;

	WLOCK_TREE(node->tree);
	/* race with some other thread inserting jnode into the hash table is
	 * impossible, because we keep the page lock. */
	/*
	 * following assertion no longer holds because of RCU: it is possible
	 * jnode is in the hash table, but with JNODE_RIP bit set.
	 */
	/* assert("nikita-3211", j_hash_find(jtable, &node->key.j) == NULL); */
	j_hash_insert_rcu(jtable, node);

	inode_attach_jnode(node);

	WUNLOCK_TREE(node->tree);
}

static void
unhash_unformatted_node_nolock(jnode *node)
{
	/* remove jnode from hash-table */	
	j_hash_remove_rcu(&node->tree->jhash_table, node);

	/* remove jnode from inode's tree of jnodes */
	inode_detach_jnode(node);

	node->key.j.mapping = 0;
	node->key.j.index = (unsigned long)-1;
	node->key.j.objectid = 0;
}

/* remove jnode from hash table and from inode's tree of jnodes. This is used in reiser4_invalidatepage and in
   kill_hook_extent->truncate_inode_jnodes->uncapture_jnode */
void
unhash_unformatted_jnode(jnode *node)
{
	assert("vs-1445", jnode_is_unformatted(node));
	WLOCK_TREE(node->tree);

	unhash_unformatted_node_nolock(node);

	WUNLOCK_TREE(node->tree);
}

/* jget() (a la zget() but for unformatted nodes). Returns (and possibly
   creates) jnode corresponding to page @pg. jnode is attached to page and
   inserted into jnode hash-table. */
static jnode *
do_jget(reiser4_tree * tree, struct page * pg)
{
	/* FIXME: Note: The following code assumes page_size == block_size.
	   When support for page_size > block_size is added, we will need to
	   add a small per-page array to handle more than one jnode per
	   page. */
	jnode *jal;
	jnode *result;
	oid_t oid = get_inode_oid(pg->mapping->host);

	assert("umka-176", pg != NULL);
	assert("nikita-2394", PageLocked(pg));

	result = jprivate(pg);
	if (likely(result != NULL))
		return jref(result);

	/* check hash-table first */
	tree = tree_by_page(pg);

	result = jlookup(tree, oid, pg->index);
	if (unlikely(result != NULL)) {
		assert("nikita-2358", jnode_page(result) == NULL);
		UNDER_SPIN_VOID(jnode, result, jnode_attach_page(result, pg));
		result->key.j.mapping = pg->mapping;
		return result;
	}

	jal = jnew_unformatted();

	if (unlikely(jal == NULL))
		return ERR_PTR(RETERR(-ENOMEM));

	assert("nikita-3209", jprivate(pg) == NULL);
	jref(jal);
	hash_unformatted_jnode(jal, pg->mapping, pg->index);
	/* attach jnode to page */
	UNDER_SPIN_VOID(jnode, jal, jnode_attach_page(jal, pg));
	return jal;
}

jnode *
jnode_of_page(struct page * pg)
{
	jnode * result;

	assert("umka-176", pg != NULL);
	assert("nikita-2394", PageLocked(pg));

	result = do_jget(tree_by_page(pg), pg);

	if (REISER4_DEBUG && !IS_ERR(result)) {
		assert("nikita-3210", result == jprivate(pg));
		assert("nikita-2046", jnode_page(jprivate(pg)) == pg);
		if (jnode_is_unformatted(jprivate(pg))) {
			assert("nikita-2364", jprivate(pg)->key.j.index == pg->index);
			assert("nikita-2367",
			       jprivate(pg)->key.j.mapping == pg->mapping);
			assert("nikita-2365",
			       jprivate(pg)->key.j.objectid == get_inode_oid(pg->mapping->host));
			assert("vs-1200",
			       jprivate(pg)->key.j.objectid == pg->mapping->host->i_ino);
			assert("nikita-2356", jnode_is_unformatted(jnode_by_page(pg)));
		}
		assert("nikita-2956", jnode_invariant(jprivate(pg), 0, 0));
	}
	return result;
}

void
jnode_attach_page(jnode * node, struct page *pg)
{
	assert("nikita-2060", node != NULL);
	assert("nikita-2061", pg != NULL);

	assert("nikita-2050", pg->private == 0ul);
	assert("nikita-2393", !PagePrivate(pg));

	assert("nikita-2396", PageLocked(pg));
	assert("nikita-2397", spin_jnode_is_locked(node));

	page_cache_get(pg);
	pg->private = (unsigned long) node;
	node->pg = pg;
	SetPagePrivate(pg);
}

void
page_clear_jnode(struct page *page, jnode * node)
{
	assert("nikita-2424", page != NULL);
	assert("nikita-2425", PageLocked(page));
	assert("nikita-2426", node != NULL);
	assert("nikita-2427", spin_jnode_is_locked(node));
	assert("nikita-2428", PagePrivate(page));

	JF_CLR(node, JNODE_PARSED);
	page->private = 0ul;
	ClearPagePrivate(page);
	node->pg = NULL;
	page_cache_release(page);
}

/* it is only used in one place to handle error */
void
page_detach_jnode(struct page *page, struct address_space *mapping, unsigned long index)
{
	assert("nikita-2395", page != NULL);

	lock_page(page);
	if ((page->mapping == mapping) && (page->index == index) && PagePrivate(page)) {
		jnode *node;

		node = jprivate(page);
		assert("nikita-2399", spin_jnode_is_not_locked(node));
		UNDER_SPIN_VOID(jnode, node, page_clear_jnode(page, node));
	}
	unlock_page(page);
}

/* return @node page locked.

   Locking ordering requires that one first takes page lock and afterwards
   spin lock on node attached to this page. Sometimes it is necessary to go in
   the opposite direction. This is done through standard trylock-and-release
   loop.
*/
struct page *
jnode_lock_page(jnode * node)
{
	struct page *page;

	assert("nikita-2052", node != NULL);
	assert("nikita-2401", spin_jnode_is_not_locked(node));

	while (1) {

		LOCK_JNODE(node);
		page = jnode_page(node);
		if (page == NULL) {
			break;
		}

		/* no need to page_cache_get( page ) here, because page cannot
		   be evicted from memory without detaching it from jnode and
		   this requires spin lock on jnode that we already hold.
		*/
		if (!TestSetPageLocked(page)) {
			/* We won a lock on jnode page, proceed. */
			break;
		}

		/* Page is locked by someone else. */
		page_cache_get(page);
		UNLOCK_JNODE(node);
		wait_on_page_locked(page);
		/* it is possible that page was detached from jnode and
		   returned to the free pool, or re-assigned while we were
		   waiting on locked bit. This will be rechecked on the next
		   loop iteration.
		*/
		page_cache_release(page);

		/* try again */
	}
	return page;
}
static inline int
jparse(jnode * node)
{
	int result;

	assert("nikita-2466", node != NULL);

	LOCK_JNODE(node);
	if (likely(!jnode_is_parsed(node))) {
		result = jnode_ops(node)->parse(node);
		if (likely(result == 0))
			JF_SET(node, JNODE_PARSED);
	} else
		result = 0;
	UNLOCK_JNODE(node);
	return result;
}

/* Lock a page attached to jnode, create and attach page to jnode if it had no one. */
struct page *
jnode_get_page_locked(jnode * node, int gfp_flags)
{
	struct page * page;

	LOCK_JNODE(node);
	page = jnode_page(node);

	if (page == NULL) {
		UNLOCK_JNODE(node);
		page = find_or_create_page(jnode_get_mapping(node),
					   jnode_get_index(node), gfp_flags);
		if (page == NULL)
			return ERR_PTR(RETERR(-ENOMEM));
	} else {
		if (!TestSetPageLocked(page)) {
			UNLOCK_JNODE(node);
			return page;
		}
		page_cache_get(page);
		UNLOCK_JNODE(node);
		lock_page(page);
		assert("nikita-3134", page->mapping == jnode_get_mapping(node));
	}

	LOCK_JNODE(node);
	if (!jnode_page(node))
		jnode_attach_page(node, page);
	UNLOCK_JNODE(node);

	page_cache_release(page);
	assert ("zam-894", jnode_page(node) == page);
	return page;
}

/* Start read operation for jnode's page if page is not up-to-date. */
static int jnode_start_read (jnode * node, struct page * page)
{
	assert ("zam-893", PageLocked(page));

	if (PageUptodate(page)) {
		unlock_page(page);
		return 0;
	}
	return page_io(page, node, READ, GFP_KERNEL);
}

#if REISER4_DEBUG
static void check_jload(jnode * node, struct page * page)
{
	if (jnode_is_znode(node)) {
		node40_header *nh;
		znode *z;

		z = JZNODE(node);
		if (znode_is_any_locked(z)) {
			nh = (node40_header *)kmap(page);
			/* this only works for node40-only file systems. For
			 * debugging. */
			assert("nikita-3253",
			       z->nr_items == d16tocpu(&nh->nr_items));
			kunmap(page);
		}
	}
}
#else
#define check_jload(node, page) noop
#endif

void jload_prefetch(const jnode * node)
{
	prefetchw(&node->x_count);
}

/* load jnode's data into memory */
int jload_gfp (jnode * node /* node to load */, int gfp_flags /* allocation
							       * flags*/)
{
	struct page * page;
	int result = 0;
	int parsed;

	assert("nikita-3010", schedulable());
	write_node_trace(node);

	prefetchw(&node->pg);

	/* taking d-reference implies taking x-reference. */
	jref(node);

	/*
	 * acquiring d-reference to @jnode and check for JNODE_PARSED bit
	 * should be atomic, otherwise there is a race against
	 * reiser4_releasepage().
	 */
	LOCK_JLOAD(node);
	add_d_ref(node);
	parsed = jnode_is_parsed(node);
	UNLOCK_JLOAD(node);

	if (unlikely(!parsed)) {
		ON_TRACE(TRACE_PCACHE, "read node: %p\n", node);

		page = jnode_get_page_locked(node, gfp_flags);
		if (unlikely(IS_ERR(page))) {
			result = PTR_ERR(page);
			goto failed;
		}

		result = jnode_start_read(node, page);
		if (unlikely(result != 0))
			goto failed;

		wait_on_page_locked(page);
		if (unlikely(!PageUptodate(page))) {
			result = RETERR(-EIO);
			goto failed;
		}

		node->data = kmap(page);

		result = jparse(node);
		if (unlikely(result != 0)) {
			kunmap(page);
			goto failed;
		}
		check_jload(node, page);
	} else {
		page = jnode_page(node);
		check_jload(node, page);
		node->data = kmap(page);
		reiser4_stat_inc_at_level(jnode_get_level(node),
					  jnode.jload_already);
	}

	if (unlikely(REISER4_USE_EFLUSH && JF_ISSET(node, JNODE_EFLUSH)))
		UNDER_SPIN_VOID(jnode, node, eflush_del(node, 0));

	if (!is_writeout_mode())
		/* We do not mark pages active if jload is called as a part of
		 * jnode_flush() or reiser4_write_logs().  Both jnode_flush()
		 * and write_logs() add no value to cached data, there is no
		 * sense to mark pages as active when they go to disk, it just
		 * confuses vm scanning routines because clean page could be
		 * moved out from inactive list as a result of this
		 * mark_page_accessed() call. */
		mark_page_accessed(page);

	return 0;

 failed:
	jrelse(node);
	return result;
	
}

/* start asynchronous reading for given jnode's page. */
int jstartio (jnode * node)
{
	struct page * page;

	page = jnode_get_page_locked(node, GFP_KERNEL);
	if (IS_ERR(page))
		return PTR_ERR(page);

	return jnode_start_read(node, page);
}


/* Initialize a node by calling appropriate plugin instead of reading
 * node from disk as in jload(). */
int jinit_new (jnode * node)
{
	struct page * page;
	int result;

	jref(node);
	add_d_ref(node);

	page = jnode_get_page_locked(node, GFP_KERNEL);
	if (IS_ERR(page)) {
		result = PTR_ERR(page);
		goto failed;
	}

	SetPageUptodate(page);
	unlock_page(page);

	node->data = kmap(page);

	if (!jnode_is_parsed(node)) {
		jnode_plugin * jplug = jnode_ops(node);
		result = UNDER_SPIN(jnode, node, jplug->init(node));
		if (result) {
			kunmap(page);
			goto failed;
		}
		JF_SET(node, JNODE_PARSED);
	}

	return 0;

 failed:
	jrelse(node);
	return result;
}

/* drop reference to node data. When last reference is dropped, data are
   unloaded. */
void
jrelse(jnode * node /* jnode to release references to */)
{
	struct page *page;

	assert("nikita-487", node != NULL);
	assert("nikita-1906", spin_jnode_is_not_locked(node));

	LOCK_CNT_DEC(d_refs);

	ON_TRACE(TRACE_PCACHE, "release node: %p\n", node);

	page = jnode_page(node);
	if (likely(page != NULL)) {
		/*
		 * it is safe not to lock jnode here, because at this point
		 * @node->d_count is greater than zero (if jrelse() is used
		 * correctly, that is). JNODE_PARSED may be not set yet, if,
		 * for example, we got here as a result of error handling path
		 * in jload(). Anyway, page cannot be detached by
		 * reiser4_releasepage(). truncate will invalidate page
		 * regardless, but this should not be a problem.
		 */
		kunmap(page);
	}
	assert("nikita-489", atomic_read(&node->d_count) > 0);
	atomic_dec(&node->d_count);
	/* release reference acquired in jload_gfp() or jinit_new() */
	jput(node);
}

/* called from jput() to wait for io completion */
static void jnode_finish_io(jnode * node)
{
	struct page *page;

	assert("nikita-2922", node != NULL);

	LOCK_JNODE(node);
	page = jnode_page(node);
	if (page != NULL) {
		page_cache_get(page);
		UNLOCK_JNODE(node);
		wait_on_page_writeback(page);
		page_cache_release(page);
	} else
		UNLOCK_JNODE(node);
}

/*
 * This is called by jput() when last reference to jnode is released. This is
 * separate function, because we want fast path of jput() to be inline and,
 * therefore, small.
 */
void
jput_final(jnode * node)
{
	int r_i_p;

	/* A fast check for keeping node in cache. We always keep node in cache
	 * if its page is present and node was not marked for deletion */
	if (jnode_page(node) != NULL && !JF_ISSET(node, JNODE_HEARD_BANSHEE)) {
		rcu_read_unlock();
		return;
	}

	r_i_p = !JF_TEST_AND_SET(node, JNODE_RIP);
	/*
	 * if r_i_p is true, we were first to set JNODE_RIP on this node. In
	 * this case it is safe to access node after unlock.
	 */
	rcu_read_unlock();
	if (r_i_p) {
		jnode_finish_io(node);
		if (JF_ISSET(node, JNODE_HEARD_BANSHEE))
			/* node is removed from the tree. */
			jdelete(node);
		else
			jnode_try_drop(node);
	}
	/* if !r_i_p some other thread is already killing it */
}

int
jwait_io(jnode * node, int rw)
{
	struct page *page;
	int result;

	assert("zam-447", node != NULL);
	assert("zam-448", jnode_page(node) != NULL);

	page = jnode_page(node);

	result = 0;
	if (rw == READ) {
		wait_on_page_locked(page);
	} else {
		assert("nikita-2227", rw == WRITE);
		wait_on_page_writeback(page);
	}
	if (PageError(page))
		result = RETERR(-EIO);

	return result;
}

void
jnode_set_type(jnode * node, jnode_type type)
{
	static unsigned long type_to_mask[] = {
		[JNODE_UNFORMATTED_BLOCK] = 1,
		[JNODE_FORMATTED_BLOCK] = 0,
		[JNODE_BITMAP] = 2,
		[JNODE_IO_HEAD] = 6,
		[JNODE_INODE] = 4
	};

	assert("zam-647", type < LAST_JNODE_TYPE);
	assert("nikita-2815", !jnode_is_loaded(node));

	node->state &= ((1UL << JNODE_TYPE_1) - 1);
	node->state |= (type_to_mask[type] << JNODE_TYPE_1);
}

static int
init_noinit(jnode * node UNUSED_ARG)
{
	return 0;
}

static int
parse_noparse(jnode * node UNUSED_ARG)
{
	return 0;
}

struct address_space *
mapping_jnode(const jnode * node)
{
	struct address_space *map;

	assert("nikita-2713", node != NULL);
	map = node->key.j.mapping;
	assert("nikita-2714", map != NULL);
	assert("nikita-2897", is_reiser4_inode(map->host));
	assert("nikita-2715", get_inode_oid(map->host) == node->key.j.objectid);
	assert("vs-1447", !JF_ISSET(node, JNODE_CC));
	return map;
}

unsigned long
index_jnode(const jnode * node)
{
	assert("vs-1447", !JF_ISSET(node, JNODE_CC));
	return node->key.j.index;
}

static inline void
remove_jnode(jnode * node, reiser4_tree * tree)
{
	if (node->key.j.mapping)
		unhash_unformatted_node_nolock(node);
}

static void
remove_inode_jnode(jnode * node, reiser4_tree * tree UNUSED_ARG)
{
	assert("nikita-2663", capture_list_is_clean(node));

	phash_jnode_destroy(node);
}

static struct address_space *
mapping_znode(const jnode * node)
{
	assert("vs-1447", !JF_ISSET(node, JNODE_CC));
	return get_super_fake(jnode_get_tree(node)->super)->i_mapping;
}

extern int znode_shift_order;
static unsigned long
index_znode(const jnode * node)
{
	unsigned long addr;
	assert("nikita-3317", (1 << znode_shift_order) < sizeof(znode));

	addr = (unsigned long)node;
	return (addr - PAGE_OFFSET) >> znode_shift_order;
}

static struct address_space *
mapping_bitmap(const jnode * node)
{
	return get_super_private(jnode_get_tree(node)->super)->bitmap->i_mapping;
}

static unsigned long
index_is_address(const jnode * node)
{
	unsigned long ind;

	ind = (unsigned long)node;
	return ind - PAGE_OFFSET;
}

/* resolve race with jput */
jnode *
jnode_rip_sync(reiser4_tree *t, jnode * node)
{
	if (unlikely(JF_ISSET(node, JNODE_RIP))) {
		RLOCK_TREE(t);
		if (JF_ISSET(node, JNODE_RIP)) {
			dec_x_ref(node);
			node = NULL;
		}
		RUNLOCK_TREE(t);
	}
	return node;
}


reiser4_key *
jnode_build_key(const jnode * node, reiser4_key * key)
{
	struct inode *inode;
	file_plugin  *fplug;
	loff_t        off;

	assert("nikita-3092", node != NULL);
	assert("nikita-3093", key != NULL);
	assert("nikita-3094", jnode_is_unformatted(node));

	inode = mapping_jnode(node)->host;
	fplug = inode_file_plugin(inode);
	off   = ((loff_t)index_jnode(node)) << PAGE_CACHE_SHIFT;

	assert("nikita-3095", fplug != NULL);
	fplug->key_by_inode(inode, off, key);
	return key;
}

extern int zparse(znode * node);

static int
parse_znode(jnode * node)
{
	return zparse(JZNODE(node));
}

static void
delete_znode(jnode * node, reiser4_tree * tree)
{
	znode *z;

	assert("nikita-2128", rw_tree_is_write_locked(tree));
	assert("vs-898", JF_ISSET(node, JNODE_HEARD_BANSHEE));

	z = JZNODE(node);
	assert("vs-899", z->c_count == 0);

	/* delete znode from sibling list. */
	sibling_list_remove(z);

	znode_remove(z, tree);
}

static int
remove_znode(jnode * node, reiser4_tree * tree)
{
	znode *z;

	assert("nikita-2128", rw_tree_is_locked(tree));
	z = JZNODE(node);

	if (z->c_count == 0) {
		/* detach znode from sibling list. */
		sibling_list_drop(z);
		/* this is called with tree spin-lock held, so call
		   znode_remove() directly (rather than znode_lock_remove()). */
		znode_remove(z, tree);
		return 0;
	}
	return RETERR(-EBUSY);
}

static int
init_znode(jnode * node)
{
	znode *z;

	z = JZNODE(node);
	return z->nplug->init(z);
}

/* jplug->clone for formatted nodes (znodes) */
znode *zalloc(int gfp_flag);
void zinit(znode *, const znode * parent, reiser4_tree *);
jnode *
clone_formatted(jnode *node)
{
	znode *clone;

	assert("vs-1430", jnode_is_znode(node));
	clone = zalloc(GFP_KERNEL);
	if (clone == NULL)
		return ERR_PTR(RETERR(-ENOMEM));
	zinit(clone, 0, current_tree);
	jnode_set_block(ZJNODE(clone), jnode_get_block(node));
	/* ZJNODE(clone)->key.z is not initialized */
	clone->level = JZNODE(node)->level;

	return ZJNODE(clone);
}

/* jplug->clone for unformatted nodes */
jnode *
clone_unformatted(jnode *node)
{
	jnode *clone;

	assert("vs-1431", jnode_is_unformatted(node));
	clone = jalloc();
	if (clone == NULL)
		return ERR_PTR(RETERR(-ENOMEM));

	jnode_init(clone, current_tree, JNODE_UNFORMATTED_BLOCK);
	jnode_set_block(clone, jnode_get_block(node));

	return clone;
	
}

jnode_plugin jnode_plugins[LAST_JNODE_TYPE] = {
	[JNODE_UNFORMATTED_BLOCK] = {
		.h = {
			.type_id = REISER4_JNODE_PLUGIN_TYPE,
			.id = JNODE_UNFORMATTED_BLOCK,
			.pops = NULL,
			.label = "unformatted",
			.desc = "unformatted node",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.init = init_noinit,
		.parse = parse_noparse,
		.mapping = mapping_jnode,
		.index = index_jnode,
		.clone = clone_unformatted
	},
	[JNODE_FORMATTED_BLOCK] = {
		.h = {
			.type_id = REISER4_JNODE_PLUGIN_TYPE,
			.id = JNODE_FORMATTED_BLOCK,
			.pops = NULL,
			.label = "formatted",
			.desc = "formatted tree node",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.init = init_znode,
		.parse = parse_znode,
		.mapping = mapping_znode,
		.index = index_znode,
		.clone = clone_formatted
	},
	[JNODE_BITMAP] = {
		.h = {
			.type_id = REISER4_JNODE_PLUGIN_TYPE,
			.id = JNODE_BITMAP,
			.pops = NULL,
			.label = "bitmap",
			.desc = "bitmap node",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.init = init_noinit,
		.parse = parse_noparse,
		.mapping = mapping_bitmap,
		.index = index_is_address,
		.clone = NULL
	},
	[JNODE_IO_HEAD] = {
		.h = {
			.type_id = REISER4_JNODE_PLUGIN_TYPE,
			.id = JNODE_IO_HEAD,
			.pops = NULL,
			.label = "io head",
			.desc = "io head",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.init = init_noinit,
		.parse = parse_noparse,
		.mapping = mapping_bitmap,
		.index = index_is_address,
		.clone = NULL
	},
	[JNODE_INODE] = {
		.h = {
			.type_id = REISER4_JNODE_PLUGIN_TYPE,
			.id = JNODE_INODE,
			.pops = NULL,
			.label = "inode",
			.desc = "inode's builtin jnode",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.init = NULL,
		.parse = NULL,
		.mapping = NULL,
		.index = NULL,
		.clone = NULL
	}
};

static inline int
jnode_is_busy(const jnode * node, jnode_type jtype)
{
	if (atomic_read(&node->x_count) > 0)
		return 1;
	if (jtype == JNODE_FORMATTED_BLOCK && JZNODE(node)->c_count > 0)
		return 1;
	return 0;
}

static inline void
jnode_remove(jnode * node, jnode_type jtype, reiser4_tree * tree UNUSED_ARG)
{
	switch (jtype) {
	case JNODE_UNFORMATTED_BLOCK:
		remove_jnode(node, tree);
		break;
	case JNODE_IO_HEAD:
	case JNODE_BITMAP:
		break;
	case JNODE_INODE:
		remove_inode_jnode(node, tree);
		break;
	case JNODE_FORMATTED_BLOCK:
		remove_znode(node, tree);
		break;
	default:
		wrong_return_value("nikita-3196", "Wrong jnode type");
	}
}

static inline void
jnode_delete(jnode * node, jnode_type jtype, reiser4_tree * tree UNUSED_ARG)
{
	switch (jtype) {
	case JNODE_UNFORMATTED_BLOCK:
		remove_jnode(node, tree);
		break;
	case JNODE_IO_HEAD:
	case JNODE_BITMAP:
		break;
	case JNODE_FORMATTED_BLOCK:
		delete_znode(node, tree);
		break;
	case JNODE_INODE:
	default:
		wrong_return_value("nikita-3195", "Wrong jnode type");
	}
}

static void
jnode_free_actor(void *arg)
{
	jnode * node;
	jnode_type jtype;

	node = arg;
	jtype = jnode_get_type(node);

	ON_DEBUG(jnode_done(node, jnode_get_tree(node)));

	switch (jtype) {
	case JNODE_IO_HEAD:
	case JNODE_BITMAP:
	case JNODE_UNFORMATTED_BLOCK:
		jfree(node);
		break;
	case JNODE_FORMATTED_BLOCK:
		zfree(JZNODE(node));
		break;
	case JNODE_INODE:
	default:
		wrong_return_value("nikita-3197", "Wrong jnode type");
	}
}

#if REISER4_DEBUG
void jnode_list_remove(jnode * node)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = get_super_private(jnode_get_tree(node)->super);

	spin_lock_irq(&sbinfo->all_guard);
	assert("nikita-2422", !list_empty(&node->jnodes));
	list_del_init(&node->jnodes);
	spin_unlock_irq(&sbinfo->all_guard);
}
#endif

static inline void
jnode_free(jnode * node, jnode_type jtype)
{
	if (jtype != JNODE_INODE) {
#if REISER4_DEBUG
		{
			reiser4_super_info_data *sbinfo;

			sbinfo = get_super_private(jnode_get_tree(node)->super);
			atomic_inc(&sbinfo->jnodes_in_flight);
		}
#endif
		assert("nikita-3219", list_empty(&node->rcu.list));
		call_rcu(&node->rcu, jnode_free_actor, node);
	}
	else
		jnode_list_remove(node);
}

int
jnode_try_drop(jnode * node)
{
	int result;
	reiser4_tree *tree;
	jnode_type    jtype;

	trace_stamp(TRACE_ZNODES);
	assert("nikita-2491", node != NULL);
	assert("nikita-2583", JF_ISSET(node, JNODE_RIP));

	ON_TRACE(TRACE_PCACHE, "trying to drop node: %p\n", node);

	tree = jnode_get_tree(node);
	jtype = jnode_get_type(node);

	LOCK_JNODE(node);
	WLOCK_TREE(tree);
	if (jnode_page(node) != NULL) {
		UNLOCK_JNODE(node);
		WUNLOCK_TREE(tree);
		JF_CLR(node, JNODE_RIP);
		return RETERR(-EBUSY);
	}

	result = jnode_is_busy(node, jtype);
	if (result == 0) {
		assert("nikita-2582", !JF_ISSET(node, JNODE_HEARD_BANSHEE));
		assert("nikita-3223", !JF_ISSET(node, JNODE_EFLUSH));
		assert("jmacd-511/b", atomic_read(&node->d_count) == 0);

		UNLOCK_JNODE(node);
		/* no page and no references---despatch him. */
		jnode_remove(node, jtype, tree);
		WUNLOCK_TREE(tree);
		jnode_free(node, jtype);
	} else {
		WUNLOCK_TREE(tree);
		UNLOCK_JNODE(node);
		JF_CLR(node, JNODE_RIP);
	}
	return result;
}

/* jdelete() -- Remove jnode from the tree */
int
jdelete(jnode * node /* jnode to finish with */)
{
	struct page *page;
	int result;
	reiser4_tree *tree;
	jnode_type    jtype;

	trace_stamp(TRACE_ZNODES);
	assert("nikita-467", node != NULL);
	assert("nikita-2531", JF_ISSET(node, JNODE_RIP));
	/* jnode cannot be eflushed at this point, because emegrency flush
	 * acquired additional reference counter. */
	assert("nikita-2917", !JF_ISSET(node, JNODE_EFLUSH));

	ON_TRACE(TRACE_PCACHE, "delete node: %p\n", node);

	jtype = jnode_get_type(node);

	page = jnode_lock_page(node);
	assert("nikita-2402", spin_jnode_is_locked(node));

	tree = jnode_get_tree(node);

	WLOCK_TREE(tree);
	result = jnode_is_busy(node, jtype);
	if (likely(!result)) {
		assert("nikita-2123", JF_ISSET(node, JNODE_HEARD_BANSHEE));
		assert("jmacd-511", atomic_read(&node->d_count) == 0);

		/* detach page */
		if (page != NULL) {
			/*
			 * FIXME this is racy against jnode_extent_write().
			 */
			page_clear_jnode(page, node);
		}
		UNLOCK_JNODE(node);
		/* goodbye */
		jnode_delete(node, jtype, tree);
		WUNLOCK_TREE(tree);
		jnode_free(node, jtype);
		/* @node is no longer valid pointer */
		if (page != NULL)
			drop_page(page);
	} else {
		JF_CLR(node, JNODE_RIP);
		WUNLOCK_TREE(tree);
		UNLOCK_JNODE(node);
		if (page != NULL)
			unlock_page(page);
	}
	return result;
}

/* drop jnode on the floor.

   Return value:

    -EBUSY:  failed to drop jnode, because there are still references to it

    0:       successfully dropped jnode

*/
static int
jdrop_in_tree(jnode * node, reiser4_tree * tree)
{
	struct page *page;
	jnode_type    jtype;
	int result;

	assert("zam-602", node != NULL);
	assert("nikita-2362", rw_tree_is_not_locked(tree));
	assert("nikita-2403", !JF_ISSET(node, JNODE_HEARD_BANSHEE));
	// assert( "nikita-2532", JF_ISSET( node, JNODE_RIP ) );

	ON_TRACE(TRACE_PCACHE, "drop node: %p\n", node);

	jtype = jnode_get_type(node);

	page = jnode_lock_page(node);
	assert("nikita-2405", spin_jnode_is_locked(node));

	WLOCK_TREE(tree);

	result = jnode_is_busy(node, jtype);
	if (!result) {
		assert("nikita-2488", page == jnode_page(node));
		assert("nikita-2533", atomic_read(&node->d_count) == 0);
		if (page != NULL) {
			assert("nikita-2126", !PageDirty(page));
			assert("nikita-2127", PageUptodate(page));
			assert("nikita-2181", PageLocked(page));
			page_clear_jnode(page, node);
		}
		UNLOCK_JNODE(node);
		jnode_remove(node, jtype, tree);
		WUNLOCK_TREE(tree);
		jnode_free(node, jtype);
		if (page != NULL) {
			drop_page(page);
		}
	} else {
		JF_CLR(node, JNODE_RIP);
		WUNLOCK_TREE(tree);
		UNLOCK_JNODE(node);
		if (page != NULL)
			unlock_page(page);
	}
	return result;
}

/* This function frees jnode "if possible". In particular, [dcx]_count has to
   be 0 (where applicable).  */
void
jdrop(jnode * node)
{
	jdrop_in_tree(node, jnode_get_tree(node));
}


/* IO head jnode implementation; The io heads are simple j-nodes with limited
   functionality (these j-nodes are not in any hash table) just for reading
   from and writing to disk. */

jnode *
alloc_io_head(const reiser4_block_nr * block)
{
	jnode *jal = jalloc();

	if (jal != NULL) {
		jnode_init(jal, current_tree, JNODE_IO_HEAD);
		jnode_set_block(jal, block);
	}

	jref(jal);

	return jal;
}

void
drop_io_head(jnode * node)
{
	assert("zam-648", jnode_get_type(node) == JNODE_IO_HEAD);

	jput(node);
	jdrop(node);
}

/* protect keep jnode data from reiser4_releasepage()  */
void
pin_jnode_data(jnode * node)
{
	assert("zam-671", jnode_page(node) != NULL);
	page_cache_get(jnode_page(node));
}

/* make jnode data free-able again */
void
unpin_jnode_data(jnode * node)
{
	assert("zam-672", jnode_page(node) != NULL);
	page_cache_release(jnode_page(node));
}

struct address_space *
jnode_get_mapping(const jnode * node)
{
	assert("nikita-3162", node != NULL);
	return jnode_ops(node)->mapping(node);
}

#if REISER4_DEBUG
/* debugging aid: jnode invariant */
int
jnode_invariant_f(const jnode * node,
		  char const **msg)
{
#define _ergo(ant, con) 						\
	((*msg) = "{" #ant "} ergo {" #con "}", ergo((ant), (con)))
#define _check(exp) ((*msg) = #exp, (exp))

	return
		_check(node != NULL) &&

		/* [jnode-queued] */

		/* only relocated node can be queued, except that when znode
		 * is being deleted, its JNODE_RELOC bit is cleared */
		_ergo(JF_ISSET(node, JNODE_FLUSH_QUEUED),
		      JF_ISSET(node, JNODE_RELOC) ||
		      JF_ISSET(node, JNODE_HEARD_BANSHEE)) &&

		_check(node->jnodes.prev != NULL) &&
		_check(node->jnodes.next != NULL) &&

		/* [jnode-dirty] invariant */

		/* dirty inode is part of atom */
		_ergo(jnode_is_dirty(node), node->atom != NULL) &&

		/* [jnode-oid] invariant */

		/* for unformatted node ->objectid and ->mapping fields are
		 * consistent */
		_ergo(jnode_is_unformatted(node),
		      node->key.j.objectid == get_inode_oid(node->key.j.mapping->host)) &&
		/* [jnode-refs] invariant */

		/* only referenced jnode can be loaded */
		_check(atomic_read(&node->x_count) >= atomic_read(&node->d_count));

}

/* debugging aid: check znode invariant and panic if it doesn't hold */
int
jnode_invariant(const jnode * node, int tlocked, int jlocked)
{
	char const *failed_msg;
	int result;
	reiser4_tree *tree;

	tree = jnode_get_tree(node);

	assert("umka-063312", node != NULL);
	assert("umka-064321", tree != NULL);

	if (!jlocked && !tlocked)
		LOCK_JNODE((jnode *) node);
	if (!tlocked)
		RLOCK_TREE(jnode_get_tree(node));
	result = jnode_invariant_f(node, &failed_msg);
	if (!result) {
		info_jnode("corrupted node", node);
		warning("jmacd-555", "Condition %s failed", failed_msg);
	}
	if (!tlocked)
		RUNLOCK_TREE(jnode_get_tree(node));
	if (!jlocked && !tlocked)
		UNLOCK_JNODE((jnode *) node);
	return result;
}
/* REISER4_DEBUG */
#endif

#if REISER4_STATS
void reiser4_stat_inc_at_level_jput(const jnode * node)
{
	reiser4_stat_inc_at_level(jnode_get_level(node), jnode.jput);
}

void reiser4_stat_inc_at_level_jputlast(const jnode * node)
{
	reiser4_stat_inc_at_level(jnode_get_level(node), jnode.jputlast);
}
/* REISER4_STATS */
#endif

#if REISER4_DEBUG_OUTPUT

const char *
jnode_type_name(jnode_type type)
{
	switch (type) {
	case JNODE_UNFORMATTED_BLOCK:
		return "unformatted";
	case JNODE_FORMATTED_BLOCK:
		return "formatted";
	case JNODE_BITMAP:
		return "bitmap";
	case JNODE_IO_HEAD:
		return "io head";
	case JNODE_INODE:
		return "inode";
	case LAST_JNODE_TYPE:
		return "last";
	default:{
			static char unknown[30];

			sprintf(unknown, "unknown %i", type);
			return unknown;
		}
	}
}

#define jnode_state_name( node, flag )			\
	( JF_ISSET( ( node ), ( flag ) ) ? ((#flag "|")+6) : "" )

/* debugging aid: output human readable information about @node */
void
info_jnode(const char *prefix /* prefix to print */ ,
	   const jnode * node /* node to print */ )
{
	assert("umka-068", prefix != NULL);

	if (node == NULL) {
		printk("%s: null\n", prefix);
		return;
	}

	printk("%s: %p: state: %lx: [%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s], level: %i,"
	       " block: %s, d_count: %d, x_count: %d, "
	       "pg: %p, atom: %p, lock: %i:%i, type: %s, ",
	       prefix, node, node->state,
	       jnode_state_name(node, JNODE_PARSED),
	       jnode_state_name(node, JNODE_HEARD_BANSHEE),
	       jnode_state_name(node, JNODE_LEFT_CONNECTED),
	       jnode_state_name(node, JNODE_RIGHT_CONNECTED),
	       jnode_state_name(node, JNODE_ORPHAN),
	       jnode_state_name(node, JNODE_CREATED),
	       jnode_state_name(node, JNODE_RELOC),
	       jnode_state_name(node, JNODE_OVRWR),
	       jnode_state_name(node, JNODE_DIRTY),
	       jnode_state_name(node, JNODE_IS_DYING),
	       jnode_state_name(node, JNODE_MAPPED),
	       jnode_state_name(node, JNODE_EFLUSH),
	       jnode_state_name(node, JNODE_FLUSH_QUEUED),
	       jnode_state_name(node, JNODE_RIP),
	       jnode_state_name(node, JNODE_MISSED_IN_CAPTURE),
	       jnode_state_name(node, JNODE_WRITEBACK),
	       jnode_state_name(node, JNODE_NEW),
	       jnode_state_name(node, JNODE_DKSET),
	       jnode_state_name(node, JNODE_EPROTECTED),
	       jnode_state_name(node, JNODE_REPACK),
	       jnode_get_level(node), sprint_address(jnode_get_block(node)),
	       atomic_read(&node->d_count), atomic_read(&node->x_count),
	       jnode_page(node), node->atom,
#if REISER4_LOCKPROF && REISER4_LOCKPROF_OBJECTS
	       node->guard.held, node->guard.trying,
#else
	       0, 0,
#endif
	       jnode_type_name(jnode_get_type(node)));
	if (jnode_is_unformatted(node)) {
		printk("inode: %llu, index: %lu, ",
		       node->key.j.objectid, node->key.j.index);
	}
}

/* debugging aid: output human readable information about @node */
void
print_jnode(const char *prefix /* prefix to print */ ,
	    const jnode * node /* node to print */)
{
	if (jnode_is_znode(node))
		print_znode(prefix, JZNODE(node));
	else
		info_jnode(prefix, node);
}

/* this is cut-n-paste replica of print_znodes() */
void
print_jnodes(const char *prefix, reiser4_tree * tree)
{
	jnode *node;
	jnode *next;
	j_hash_table *htable;
	int tree_lock_taken;

	if (tree == NULL)
		tree = current_tree;

	/* this is a debugging function. It can be called by reiser4_panic()
	   with tree spin-lock already held. Trylock is not exactly what we
	   want here, but it is passable.
	*/
	tree_lock_taken = write_trylock_tree(tree);
	htable = &tree->jhash_table;

	for_all_in_htable(htable, j, node, next) {
		info_jnode(prefix, node);
		printk("\n");
	}
	if (tree_lock_taken)
		WUNLOCK_TREE(tree);
}

/* REISER4_DEBUG_OUTPUT */
#endif

/* this is only used to created jnode during capture copy */
jnode *jclone(jnode *node)
{
	jnode *clone;

	assert("vs-1429", jnode_ops(node)->clone);
	clone = jnode_ops(node)->clone(node);
	if (IS_ERR(clone))
		return clone;

	jref(clone);
	JF_SET(clone, JNODE_HEARD_BANSHEE);
	JF_SET(clone, JNODE_CC);
	return clone;
}


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
