/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */
/* Jnode manipulation functions. */
/* Jnode is entity used to track blocks with data and meta-data in reiser4.
  
   In particular, jnodes are used to track transactional information
   associated with each block. Each znode contains jnode as ->zjnode field.
  
   Jnode stands for either Josh or Journal node.
  
*/

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

#include <asm/uaccess.h>        /* UML needs this for PAGE_OFFSET */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/fs.h>		/* for struct address_space  */

static kmem_cache_t *_jnode_slab = NULL;

static inline jnode_plugin *
jnode_ops_of(const jnode_type type)
{
	assert("nikita-2367", type < LAST_JNODE_TYPE);
	return jnode_plugin_by_id((reiser4_plugin_id) type);
}

static inline jnode_plugin *
jnode_ops(const jnode * node)
{
	assert("nikita-2366", node != NULL);

	return jnode_ops_of(jnode_get_type(node));
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
jnode_key_hashfn(const jnode_key_t * key)
{
	__u32 hash;
	__u32 shift;

	assert("nikita-2352", key != NULL);

	shift = (((__u32) (key->objectid & 0xffffffff)) | ((__u32) (key->objectid >> 32)));
	/*shift /= ( sizeof( struct inode ) & ~ ( sizeof ( struct inode ) - 1 ) ); */
	shift += (__u32) key->index;

	hash = shift + (shift >> REISER4_JNODE_HASH_TABLE_BITS);
	return hash & (REISER4_JNODE_HASH_TABLE_SIZE - 1);
}

/* The hash table definition */
#define KMALLOC( size ) reiser4_kmalloc( ( size ), GFP_KERNEL )
#define KFREE( ptr, size ) reiser4_kfree( ptr, size )
TS_HASH_DEFINE(j, jnode, jnode_key_t, key.j, link.j, jnode_key_hashfn, jnode_key_eq);
#undef KFREE
#undef KMALLOC

/* call this to initialise jnode hash table */
int
jnodes_tree_init(reiser4_tree * tree /* tree to initialise jnodes for */ )
{
	assert("nikita-2359", tree != NULL);

	return j_hash_init(&tree->jhash_table, REISER4_JNODE_HASH_TABLE_SIZE);
}

/* call this to destroy jnode hash table */
int
jnodes_tree_done(reiser4_tree * tree /* tree to destroy jnodes for */ )
{
	j_hash_table *jtable;
	jnode *node;
	jnode *next;
	int killed;

	assert("nikita-2360", tree != NULL);

	trace_if(TRACE_ZWEB, UNDER_RW_VOID(tree, tree, read,
					   print_jnodes("umount", tree)));

	jtable = &tree->jhash_table;
	do {
		killed = 0;
		for_all_in_htable(jtable, j, node, next) {
			assert("nikita-2361", !atomic_read(&node->x_count));
			jdrop(node);
			++killed;
			break;
		}
	} while (killed > 0);

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
	return -ENOMEM;
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
jnode_init(jnode * node, reiser4_tree * tree)
{
	assert("umka-175", node != NULL);

	xmemset(node, 0, sizeof (jnode));
	node->state = 0;
	atomic_set(&node->d_count, 0);
	atomic_set(&node->x_count, 0);
	spin_lock_init(&node->guard);
	node->atom = NULL;
	node->tree = tree;
	capture_list_clean(node);

#if REISER4_DEBUG
	UNDER_RW_VOID(tree, tree, write,
		      list_add(&node->jnodes, &get_current_super_private()->all_jnodes));
#endif
}

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

void
jfree(jnode * node)
{
	assert("zam-449", node != NULL);

	assert("nikita-2422", !list_empty(&node->jnodes));
	assert("nikita-2663", capture_list_is_clean(node));
	assert("nikita-2774", !JF_ISSET(node, JNODE_EFLUSH));

	phash_jnode_destroy(node);
	ON_DEBUG(list_del_init(&node->jnodes));
	/* poison memory. */
	ON_DEBUG(xmemset(node, 0xad, sizeof *node));
	kmem_cache_free(_jnode_slab, node);
}

jnode *
jnew(void)
{
	jnode *jal;

	jal = jalloc();

	if (jal == NULL)
		return NULL;

	jnode_init(jal, current_tree);

	/* FIXME: not a strictly correct, but should help in avoiding of
	   looking to missing znode-only fields */
	jnode_set_type(jal, JNODE_UNFORMATTED_BLOCK);

	return jal;
}

/* look for jnode with given mapping and offset within hash table */
jnode *
jlook(reiser4_tree * tree, oid_t objectid, unsigned long index)
{
	jnode_key_t jkey;
	jnode *node;

	assert("nikita-2353", tree != NULL);
	assert("nikita-2355", rw_tree_is_locked(tree));

	jkey.objectid = objectid;
	jkey.index = index;
	node = j_hash_find(&tree->jhash_table, &jkey);
	if (node != NULL) {
		/* protect @node from recycling */
		jref(node);
		assert("nikita-2955", jnode_invariant(node, 1, 0));
	}
	return node;
}

/* like jlook, but acquire tree read lock first */
jnode *
jlook_lock(reiser4_tree * tree, oid_t objectid, unsigned long index)
{
	return UNDER_RW(tree, tree, read, jlook(tree, objectid, index));
}

/* jget() (a la zget() but for unformatted nodes). Returns (and possibly
   creates) jnode corresponding to page @pg. jnode is attached to page and
   inserted into jnode hash-table. */
jnode *
jget(reiser4_tree * tree, struct page * pg)
{
	/* FIXME: Note: The following code assumes page_size == block_size.
	   When support for page_size > block_size is added, we will need to
	   add a small per-page array to handle more than one jnode per
	   page. */
	jnode *jal = NULL;
	oid_t oid = get_inode_oid(pg->mapping->host);

	assert("umka-176", pg != NULL);
	/* check that page is unformatted */
	assert("nikita-2065", pg->mapping->host != get_super_private(pg->mapping->host->i_sb)->fake);
	assert("nikita-2394", PageLocked(pg));
again:
	if (jprivate(pg) == NULL) {
		jnode *in_hash;
		/* check hash-table first */
		tree = tree_by_page(pg);
		write_lock_tree(tree);
		in_hash = jlook(tree, oid, pg->index);
		if (in_hash != NULL) {
			assert("nikita-2358", jnode_page(in_hash) == NULL);
			write_unlock_tree(tree);
			UNDER_SPIN_VOID(jnode, in_hash, jnode_attach_page(in_hash, pg));
			in_hash->key.j.mapping = pg->mapping;
		} else {
			j_hash_table *jtable;

			if (jal == NULL) {
				write_unlock_tree(tree);
				jal = jnew();

				if (jal == NULL) {
					return ERR_PTR(-ENOMEM);
				}

				goto again;
			}

			jref(jal);

			jal->key.j.mapping = pg->mapping;
			jal->key.j.objectid = oid;
			jal->key.j.index = pg->index;

			jtable = &tree->jhash_table;
			assert("nikita-2357", j_hash_find(jtable, &jal->key.j) == NULL);

			j_hash_insert(jtable, jal);
			write_unlock_tree(tree);

			UNDER_SPIN_VOID(jnode, jal, jnode_attach_page(jal, pg));
			jal = NULL;
		}
	} else
		jref(jprivate(pg));

	assert("nikita-2046", jnode_page(jprivate(pg)) == pg);
	assert("nikita-2364", jprivate(pg)->key.j.index == pg->index);
	assert("nikita-2367", jprivate(pg)->key.j.mapping == pg->mapping);
	assert("nikita-2365", jprivate(pg)->key.j.objectid == oid);
	assert("vs-1200", jprivate(pg)->key.j.objectid == pg->mapping->host->i_ino);
	assert("nikita-2356", jnode_get_type(jnode_by_page(pg)) == JNODE_UNFORMATTED_BLOCK);
	assert("nikita-2956", jnode_invariant(jprivate(pg), 0, 0));

	if (jal != NULL) {
		jfree(jal);
	}
	return jnode_by_page(pg);
}

jnode *
jnode_of_page(struct page * pg)
{
	return jget(tree_by_page(pg), pg);
}

/* return jnode associated with page, possibly creating it. */
jnode *
jfind(struct page * page)
{
	jnode *node;

	assert("nikita-2417", page != NULL);
	assert("nikita-2418", PageLocked(page));

	if (PagePrivate(page))
		node = jref(jprivate(page));
	else {
		/* otherwise it can only be unformatted---znode is never
		   detached from the page. */
		node = jnode_of_page(page);
	}
	return node;
}

/* FIXME-VS: change next two functions switching to support of blocksize !=
   page cache size */
jnode *
nth_jnode(struct page * page, int block)
{
	assert("vs-695", PagePrivate(page) && page->private);
	assert("vs-696", current_blocksize == (unsigned) PAGE_CACHE_SIZE);
	assert("vs-697", block == 0);
	return jprivate(page);
}

/* get next jnode of a page.
   FIXME-VS: update this when more than one jnode per page will be allowed */
/* Audited by: umka (2002.06.13) */
jnode *
next_jnode(jnode * node UNUSED_ARG)
{
	return 0;
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

	page->private = 0ul;
	ClearPagePrivate(page);
	node->pg = NULL;
	page_cache_release(page);
}

void
page_detach_jnode(struct page *page, struct address_space *mapping, unsigned long index)
{
	assert("nikita-2395", page != NULL);

	reiser4_lock_page(page);
	if ((page->mapping == mapping) && (page->index == index) && PagePrivate(page)) {
		jnode *node;

		node = jprivate(page);
		assert("nikita-2399", spin_jnode_is_not_locked(node));
		UNDER_SPIN_VOID(jnode, node, page_clear_jnode(page, node));
	}
	reiser4_unlock_page(page);
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

		spin_lock_jnode(node);
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
		spin_unlock_jnode(node);
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

struct page_filler_arg {
	jnode * node;
	int gfp;
};

static int
page_filler(void *arg, struct page *page)
{
	struct page_filler_arg * f_arg = arg;

	assert("nikita-2369", page->mapping == jnode_ops(f_arg->node)->mapping(f_arg->node));

	reiser4_stat_inc_at_level(jnode_get_level(f_arg->node), jnode.jload_read);
	return page_io(page, f_arg->node, READ, f_arg->gfp);
}

static inline int
jparse(jnode * node, struct page *page)
{
	int result;

	assert("nikita-2466", node != NULL);
	assert("nikita-2630", page != NULL);
	assert("nikita-2637", spin_jnode_is_locked(node));
	assert("nikita-2638", PageLocked(page));

	result = 0;
	if (!jnode_is_loaded(node)) {
		result = jnode_ops(node)->parse(node);
		if (likely(result == 0))
			JF_SET(node, JNODE_LOADED);
		else {
			/* if parsing failed, detach jnode from page. */
			assert("nikita-2467", page == jnode_page(node));
			page_clear_jnode(page, node);
		}
	}
	return result;
}

/* helper function used by jload() */
static inline void
load_page(struct page *page, jnode *node)
{
	PROF_BEGIN(load_page);
	page_cache_get(page);
	if (!is_writeout_mode()) 
		/* We do not mark pages active if jload is called as a part of
		 * jnode_flush() or reiser4_write_logs().  Both jnode_flush()
		 * and write_logs() add no value to cached data, there is no
		 * sense to mark pages as active when they go to disk, it just
		 * confuses vm scanning routines because clean page could be
		 * moved out from inactive list as a result of this
		 * mark_page_accessed() call. */
		mark_page_accessed(page);
	kmap(page);
	if (REISER4_USE_EFLUSH && JF_ISSET(node, JNODE_EFLUSH))
		UNDER_SPIN_VOID(jnode, node, eflush_del(node, 0));
	PROF_END(load_page, load_page);
}

/* load jnode's data into memory using read_cache_page() */
int
jload_gfp (jnode * node, int gfp_flags)
{
	int result;
	struct page *page;
	PROF_BEGIN(jload);

	schedulable();

	write_node_trace(node);

	result = 0;
	reiser4_stat_inc_at_level(jnode_get_level(node), jnode.jload);
	jref(node);

	/*
	 * acquiring d-reference to @jnode and check for JNODE_LOADED bit
	 * should be atomic, otherwise there is a race against jrelse().
	 */
	spin_lock_jnode(node);
	add_d_ref(node);

	if (!jnode_is_loaded(node)) {
		/* If node is not loaded we need a spin lock to get reliable not
		 * null jnode_page() result */
		page = jnode_page(node);
		/* read data from page cache. Page reference counter is
		   incremented and page is kmapped, it will kunmapped in
		   zrelse
		*/
		trace_on(TRACE_PCACHE, "read node: %p\n", node);

		/* Our initial design was to index pages with formatted data
		   by their block numbers. One disadvantage of this is that
		   such setup makes relocation harder to implement: when tree
		   node is relocated we need to re-index its data in a page
		   cache. To avoid data copying during this re-indexing it was
		   decided that first version of reiser4 will only support
		   block size equal to PAGE_CACHE_SIZE.

		   But another problem came up: our block numbers are 64bit
		   and pages are indexed by 32bit ->index. Moreover:

		    - there is strong opposition for enlarging ->index field
		    (and for good reason: size of struct page is critical,
		    because there are so many of them).

		    - our "unallocated" block numbers have highest bit set,
		    which makes 64bit block number support essential
		    independently of device size.

		   Code below uses jnode _address_ as page index. This has
		   following advantages:

		    - relocation is simplified

		    - if ->index is jnode address, than ->private is free for
		    use. It can be used to store some jnode data making it
		    smaller (not yet implemented). Pointer to atom?

		*/
		/* subtle locking point: ->pg pointer is protected by jnode
		   spin lock, but it is safe to release spin lock here,
		   because page can be detached from jnode only when ->d_count
		   is 0, and JNODE_LOADED is not set.
		*/
		spin_unlock_jnode(node);
		if (likely(page != NULL && !JF_ISSET(node, JNODE_ASYNC))) {
			load_page(page, node);
			node->data = page_address(page);
			reiser4_stat_inc_at_level(jnode_get_level(node), 
						  jnode.jload_page);
			JF_SET(node, JNODE_LOADED);
		} else {
			jnode_plugin *jplug = jnode_ops(node);
			struct page_filler_arg f_arg = {.node = node, .gfp = gfp_flags };
 
			page = read_cache_page(jplug->mapping(node), 
					       jplug->index(node), 
					       page_filler, &f_arg);
			/* after (successful) return from read_cache_page()
			   @page is pinned into memory. */
			if (!IS_ERR(page)) {
				kmap(page);
				reiser4_lock_page(page);
				spin_lock_jnode(node);
				node->data = page_address(page);
				if (jnode_page(node) == NULL)
					/* this line and jget() are the only
					 * places, where page is attached to
					 * jnode */
					jnode_attach_page(node, page);
				assert("nikita-2636", jnode_page(node) == page);
				if (PageUptodate(page))
					result = jparse(node, page);
				else
					result = -EIO;
				if (REISER4_USE_EFLUSH)
					eflush_del(node, 1);
				if (REISER4_STATS && JF_ISSET(node, JNODE_ASYNC))
					reiser4_stat_inc_at_level(jnode_get_level(node), jnode.jload_async);
				JF_CLR(node, JNODE_ASYNC);
				spin_unlock_jnode(node);
				reiser4_unlock_page(page);
			} else
				result = PTR_ERR(page);

			if (unlikely(result != 0))
				jrelse(node);
		}
	} else {
		spin_unlock_jnode(node);
		page = jnode_page(node);
		assert("nikita-2348", page != NULL);
		load_page(page, node);
		reiser4_stat_inc_at_level(jnode_get_level(node), 
					  jnode.jload_already);
	}
	assert("nikita-2814", ergo(result == 0, jnode_is_loaded(node)));
	assert("nikita-2816", ergo(result == 0 && jnode_is_znode(node),
				   JZNODE(node)->nplug != NULL));
	PROF_END(jload, jload);
	return result;
}

/* call node plugin to initialise newly allocated node. */
int
jinit_new(jnode * node /* jnode to initialise */)
{
	int result;
	struct page *page;
	jnode_plugin *jplug;

	assert("nikita-1234", node != NULL);

	jref(node);
	add_d_ref(node);
	jplug = jnode_ops(node);
	page = grab_cache_page(jplug->mapping(node), jplug->index(node));
	if (page != NULL) {
		SetPageUptodate(page);
		UNDER_SPIN_VOID(jnode, node, jnode_attach_page(node, page));
		reiser4_unlock_page(page);
		kmap(page);
		node->data = page_address(page);
		result = 0;
		spin_lock_jnode(node);
		if (likely(!jnode_is_loaded(node))) {
			result = jplug->init(node);
			if (likely(result == 0))
				JF_SET(node, JNODE_LOADED);
		}
		spin_unlock_jnode(node);
	} else
		result = -ENOMEM;

	if (unlikely(result != 0))
		jrelse(node);
	return result;
}

/* just like jrelse, but assume jnode is already spin-locked */
void
jrelse_nolock(jnode * node /* jnode to release references to */ )
{
	struct page *page;

	assert("nikita-487", node != NULL);
	assert("nikita-489", atomic_read(&node->d_count) > 0);
	assert("nikita-1906", spin_jnode_is_locked(node));

	ON_DEBUG_CONTEXT(--lock_counters()->d_refs);

	trace_on(TRACE_PCACHE, "release node: %p\n", node);

	page = jnode_page(node);
	if (page != NULL) {
		kunmap(page);
		page_cache_release(page);
	}

	if (atomic_dec_and_test(&node->d_count))
		/* FIXME it is crucial that we first decrement ->d_count and
		   only later clear JNODE_LOADED bit. I hope that
		   atomic_dec_and_test() implies memory barrier (and
		   optimization barrier, of course).
		*/
		JF_CLR(node, JNODE_LOADED);
}

/* start async io for @node */
int
jstartio(jnode * node)
{
	jnode_plugin *jplug;
	struct page *page;
	int result;

	assert("nikita-2857", node != NULL);

	result = 0;
	jplug = jnode_ops(node);
	page = find_or_create_page(jplug->mapping(node), jplug->index(node),
				   GFP_KERNEL);
	if (page == NULL)
		return -ENOMEM;

	assert("nikita-2858", PageLocked(page));

	spin_lock_jnode(node);
	if (jnode_page(node) == NULL) {
		/* NOTE: JNODE_ASYNC has to be set *before* page is attached
		 * to jnode, because jload() will assume that attached page is
		 * ok, if JNODE_ASYNC is not set, and jload() doesn't hold
		 * jnode spin-lock in this case. */
		JF_SET(node, JNODE_ASYNC);
		jnode_attach_page(node, page);
		spin_unlock_jnode(node);
		if (!PageUptodate(page))
			result = page_io(page, node, READ, GFP_KERNEL);
		else
			unlock_page(page);
	} else {
		assert("nikita-2636", jnode_page(node) == page);
		spin_unlock_jnode(node);
		unlock_page(page);
	}
	
	return result;
}

int
jnode_try_drop(jnode * node)
{
	int result;
	reiser4_tree *tree;
	jnode_plugin *jplug;

	trace_stamp(TRACE_ZNODES);
	assert("nikita-2491", node != NULL);
	assert("nikita-2582", !JF_ISSET(node, JNODE_HEARD_BANSHEE));
	assert("nikita-2583", JF_ISSET(node, JNODE_RIP));

	trace_on(TRACE_PCACHE, "trying to drop node: %p\n", node);

	tree = jnode_get_tree(node);
	jplug = jnode_ops(node);

	spin_lock_jnode(node);
	write_lock_tree(tree);
	if (jnode_page(node) != NULL) {
		JF_CLR(node, JNODE_RIP);
		spin_unlock_jnode(node);
		write_unlock_tree(tree);
		return -EBUSY;
	}

	result = jplug->is_busy(node);
	spin_unlock_jnode(node);
	if (result == 0)
		/* no page and no references---despatch him. */
		result = jplug->remove(node, tree);
	else
		JF_CLR(node, JNODE_RIP);
	write_unlock_tree(tree);
	return result;
}

/* jdelete() -- Remove jnode from the tree */
int
jdelete(jnode * node /* jnode to finish with */)
{
	struct page *page;
	int result;
	reiser4_tree *tree;
	jnode_plugin *jplug;

	trace_stamp(TRACE_ZNODES);
	assert("nikita-467", node != NULL);
	assert("nikita-2123", JF_ISSET(node, JNODE_HEARD_BANSHEE));
	assert("nikita-2531", JF_ISSET(node, JNODE_RIP));
	/* jnode cannot be eflushed at this point, because emegrency flush
	 * acquired additional reference counter. */
	assert("nikita-2917", !JF_ISSET(node, JNODE_EFLUSH));

	trace_on(TRACE_PCACHE, "delete node: %p\n", node);

	jplug = jnode_ops(node);

	page = jnode_lock_page(node);
	assert("nikita-2402", spin_jnode_is_locked(node));

	tree = jnode_get_tree(node);

	write_lock_tree(tree);
	result = jplug->is_busy(node);
	if (!result) {
		/* detach page */
		if (page != NULL)
			drop_page(page, node);
		spin_unlock_jnode(node);
		result = jplug->delete(node, tree);
	} else {
		JF_CLR(node, JNODE_RIP);
		spin_unlock_jnode(node);
		if (page != NULL)
			reiser4_unlock_page(page);
	}
	write_unlock_tree(tree);
	return result;
}

/* drop jnode on the floor.
  
   Return value:
  
    -EBUSY:  failed to drop jnode, because there are still references to it
  
    0:       successfully dropped jnode
  
*/
int
jdrop_in_tree(jnode * node, reiser4_tree * tree)
{
	struct page *page;
	jnode_plugin *jplug;
	int result;

	assert("zam-602", node != NULL);
	assert("nikita-2362", rw_tree_is_not_locked(tree));
	assert("nikita-2403", !JF_ISSET(node, JNODE_HEARD_BANSHEE));
	// assert( "nikita-2532", JF_ISSET( node, JNODE_RIP ) );

	trace_on(TRACE_PCACHE, "drop node: %p\n", node);

	jplug = jnode_ops(node);

	page = jnode_lock_page(node);
	assert("nikita-2405", spin_jnode_is_locked(node));

	write_lock_tree(tree);

	result = jplug->is_busy(node);
	if (!result) {
		assert("nikita-2488", page == jnode_page(node));
		assert("nikita-2533", atomic_read(&node->d_count) == 0);
		if (page != NULL) {
			assert("nikita-2126", !PageDirty(page));
			assert("nikita-2127", PageUptodate(page));
			assert("nikita-2181", PageLocked(page));
			remove_from_page_cache(page);
			page_clear_jnode(page, node);
			reiser4_unlock_page(page);
			page_cache_release(page);
		}
		spin_unlock_jnode(node);
		result = jplug->remove(node, tree);
	} else {
		JF_CLR(node, JNODE_RIP);
		spin_unlock_jnode(node);
		if (page != NULL)
			reiser4_unlock_page(page);
	}
	write_unlock_tree(tree);
	return result;
}

/* This function frees jnode "if possible". In particular, [dcx]_count has to
   be 0 (where applicable).  */
void
jdrop(jnode * node)
{
	jdrop_in_tree(node, jnode_get_tree(node));
}

void
jput_final(jnode * node)
{
	int r_i_p;

	assert("nikita-2772", !JF_ISSET(node, JNODE_EFLUSH));

	r_i_p = !JF_TEST_AND_SET(node, JNODE_RIP);
	jnode_finish_io(node);
	if (r_i_p) {
		if (JF_ISSET(node, JNODE_HEARD_BANSHEE))
			/* node is removed from the tree. */
			jdelete(node);
		else
			jnode_try_drop(node);
	}
	/* if !r_i_p some other thread is already killing it */
}

/* called from jput() to wait for io completion */
void
jnode_finish_io(jnode * node)
{
	struct page *page;

	assert("nikita-2922", node != NULL);

	spin_lock_jnode(node);
	page = jnode_page(node);
	if (page != NULL) {
		page_cache_get(page);
		spin_unlock_jnode(node);
		wait_on_page_writeback(page);
		page_cache_release(page);
	} else
		spin_unlock_jnode(node);
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
		result = -EIO;

	return result;
}

void
jnode_set_type(jnode * node, jnode_type type)
{
	static unsigned long type_to_mask[] = {
		[JNODE_UNFORMATTED_BLOCK] = 1,
		[JNODE_FORMATTED_BLOCK] = 0,
		[JNODE_BITMAP] = 2,
		[JNODE_IO_HEAD] = 6
	};

	assert("zam-647", type < LAST_JNODE_TYPE);
	assert("nikita-2815", !jnode_is_loaded(node));

	node->state &= ((1UL << JNODE_TYPE_1) - 1);
	node->state |= (type_to_mask[type] << JNODE_TYPE_1);
}

static int
noparse(jnode * node UNUSED_ARG)
{
	return 0;
}

struct address_space *
jnode_mapping(const jnode * node)
{
	struct address_space *map;

	assert("nikita-2713", node != NULL);
	map = node->key.j.mapping;
	assert("nikita-2714", map != NULL);
	assert("nikita-2897", is_reiser4_inode(map->host));
	assert("nikita-2715", get_inode_oid(map->host) == node->key.j.objectid);

	return map;
}

unsigned long
jnode_index(const jnode * node)
{
	return node->key.j.index;
}

static int
jnode_remove_op(jnode * node, reiser4_tree * tree)
{
	/* remove jnode from hash-table */
	j_hash_remove(&tree->jhash_table, node);
	jfree(node);
	return 0;
}

static int
jnode_is_busy(const jnode * node)
{
	return (atomic_read(&node->x_count) > 0);
}

static struct address_space *
znode_mapping(const jnode * node UNUSED_ARG)
{
	return get_super_fake(reiser4_get_current_sb())->i_mapping;
}

static unsigned long
znode_index(const jnode * node)
{
	unsigned long ind;

	ind = (unsigned long)node;
	return ind - PAGE_OFFSET;
}

extern int zparse(znode * node);

static int
znode_parse(jnode * node)
{
	return zparse(JZNODE(node));
}

extern void znode_remove(znode * node, reiser4_tree * tree);
static int
znode_delete_op(jnode * node, reiser4_tree * tree)
{
	znode *z;

	assert("nikita-2128", rw_tree_is_write_locked(tree));
	assert("vs-898", JF_ISSET(node, JNODE_HEARD_BANSHEE));

	z = JZNODE(node);
	assert("vs-899", atomic_read(&z->c_count) == 0);

	/* delete znode from sibling list. */
	sibling_list_remove(z);

	znode_remove(z, tree);
	zfree(z);
	return 0;
}

static int
znode_remove_op(jnode * node, reiser4_tree * tree)
{
	znode *z;

	assert("nikita-2128", rw_tree_is_locked(tree));
	z = JZNODE(node);

	if (atomic_read(&z->c_count) == 0) {
		/* detach znode from sibling list. */
		sibling_list_drop(z);
		/* this is called with tree spin-lock held, so call
		   znode_remove() directly (rather than znode_lock_remove()). */
		znode_remove(z, tree);
		zfree(z);
		return 0;
	}
	return -EBUSY;
}

static int
znode_is_busy(const jnode * node)
{
	return jnode_is_busy(node) || (atomic_read(&JZNODE(node)->c_count) > 0);
}

static int
znode_init(jnode * node)
{
	znode *z;

	z = JZNODE(node);
	return z->nplug->init(z);
}

static int
no_hook(jnode * node UNUSED_ARG, struct page *page UNUSED_ARG, int rw UNUSED_ARG)
{
	return 1;
}

static int
other_remove_op(jnode * node, reiser4_tree * tree UNUSED_ARG)
{
	jfree(node);
	return 0;
}

extern int znode_io_hook(jnode * node, struct page *page, int rw);

jnode_plugin jnode_plugins[LAST_JNODE_TYPE] = {
	[JNODE_UNFORMATTED_BLOCK] = {
				     .h = {
					   .type_id = REISER4_JNODE_PLUGIN_TYPE,
					   .id = JNODE_UNFORMATTED_BLOCK,
					   .pops = NULL,
					   .label = "unformatted",
					   .desc = "unformatted node",
					   .linkage = TS_LIST_LINK_ZERO
				     },
				     .init = noparse,
				     .parse = noparse,
				     .remove = jnode_remove_op,
				     .delete = jnode_remove_op,
				     .is_busy = jnode_is_busy,
				     .mapping = jnode_mapping,
				     .index = jnode_index,
				     .io_hook = no_hook
	},
	[JNODE_FORMATTED_BLOCK] = {
				   .h = {
					 .type_id = REISER4_JNODE_PLUGIN_TYPE,
					 .id = JNODE_FORMATTED_BLOCK,
					 .pops = NULL,
					 .label = "formatted",
					 .desc = "formatted tree node",
					 .linkage = TS_LIST_LINK_ZERO
				   },
				   .init = znode_init,
				   .parse = znode_parse,
				   .remove = znode_remove_op,
				   .delete = znode_delete_op,
				   .is_busy = znode_is_busy,
				   .mapping = znode_mapping,
				   .index = znode_index,
				   .io_hook = znode_io_hook
	},
	[JNODE_BITMAP] = {
			  .h = {
				.type_id = REISER4_JNODE_PLUGIN_TYPE,
				.id = JNODE_BITMAP,
				.pops = NULL,
				.label = "bitmap",
				.desc = "bitmap node",
				.linkage = TS_LIST_LINK_ZERO
			  },
			  .init = noparse,
			  .parse = noparse,
			  .remove = other_remove_op,
			  .delete = other_remove_op,
			  .is_busy = jnode_is_busy,
			  .mapping = znode_mapping,
			  .index = znode_index,
			  .io_hook = no_hook
	},
	[JNODE_IO_HEAD] = {
			   .h = {
				 .type_id = REISER4_JNODE_PLUGIN_TYPE,
				 .id = JNODE_IO_HEAD,
				 .pops = NULL,
				 .label = "io head",
				 .desc = "io head",
				 .linkage = TS_LIST_LINK_ZERO
			   },
			   .init = noparse,
			   .parse = noparse,
			   .remove = other_remove_op,
			   .delete = other_remove_op,
			   .is_busy = jnode_is_busy,
			   .mapping = znode_mapping,
			   .index = znode_index,
			   .io_hook = no_hook
	}
};

/* IO head jnode implementation; The io heads are simple j-nodes with limited
   functionality (these j-nodes are not in any hash table) just for reading
   from and writing to disk. */

jnode *
alloc_io_head(const reiser4_block_nr * block)
{
	jnode *jal = jalloc();

	if (jal != NULL) {
		jnode_init(jal, current_tree);
		jnode_set_type(jal, JNODE_IO_HEAD);
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


int jnode_io_hook(jnode *node, struct page *page, int rw)
{
	/* prepare node to being written */
	return jnode_ops(node)->io_hook(node, page, rw);
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
		spin_lock_jnode((jnode *) node);
	if (!tlocked)
		read_lock_tree(jnode_get_tree(node));
	result = jnode_invariant_f(node, &failed_msg);
	if (!result) {
		info_jnode("corrupted node", node);
		warning("jmacd-555", "Condition %s failed", failed_msg);
	}
	if (!tlocked)
		read_unlock_tree(jnode_get_tree(node));
	if (!jlocked && !tlocked)
		spin_unlock_jnode((jnode *) node);
	return result;
}
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
		info("%s: null\n", prefix);
		return;
	}

	info("%s: %p: state: %lx: [%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s], level: %i,"
	     " block: %s, d_count: %d, x_count: %d, pg: %p, atom: %p(%i), type: %s, ",
	     prefix, node, node->state,
	     jnode_state_name(node, JNODE_LOADED),
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
	     jnode_state_name(node, JNODE_ASYNC),
	     jnode_get_level(node), sprint_address(jnode_get_block(node)),
	     atomic_read(&node->d_count), atomic_read(&node->x_count),
	     jnode_page(node), node->atom, !capture_list_is_clean(node),
	     jnode_type_name(jnode_get_type(node)));
	if (jnode_is_unformatted(node)) {
		info("inode: %llu, index: %lu, ", node->key.j.objectid, node->key.j.index);
	}
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

	/* this is debugging function. It can be called by reiser4_panic()
	   with tree spin-lock already held. Trylock is not exactly what we
	   want here, but it is passable.
	*/
	tree_lock_taken = write_trylock_tree(tree);
	htable = &tree->jhash_table;

	for_all_in_htable(htable, j, node, next) {
		info_jnode(prefix, node);
		info("\n");
	}
	if (tree_lock_taken)
		write_unlock_tree(tree);
}

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
