/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "key.h"
#include "coord.h"
#include "seal.h"
#include "plugin/item/item.h"
#include "plugin/node/node.h"
#include "plugin/plugin.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree_walk.h"
#include "tree.h"
#include "trace.h"
#include "reiser4.h"
#include "super.h"

#include <linux/slab.h>

/* rules for locking during searches: never insert before the first
   item in a node without locking the left neighbor and the patch to
   the common parent from the node and left neighbor.  This ensures
   that searching works. */

/* tree searching algorithm, intranode searching algorithms are in
   plugin/node/ */

/* tree lookup cache */

/* Initialise coord cache slot */
/* Audited by: green(2002.06.15) */
static void
cbk_cache_init_slot(cbk_cache_slot * slot)
{
	assert("nikita-345", slot != NULL);

	cbk_cache_list_clean(slot);
	slot->node = NULL;
}

/* Initialise coord cache */
int
cbk_cache_init(cbk_cache * cache /* cache to init */ )
{
	int i;

	assert("nikita-346", cache != NULL);

	cache->slot = kmalloc(sizeof (cbk_cache_slot) * cache->nr_slots, GFP_KERNEL);
	if (cache->slot == NULL)
		return -ENOMEM;

	cbk_cache_list_init(&cache->lru);
	for (i = 0; i < cache->nr_slots; ++i) {
		cbk_cache_init_slot(cache->slot + i);
		cbk_cache_list_push_back(&cache->lru, cache->slot + i);
	}
	return 0;
}

/* free cbk cache data */
void
cbk_cache_done(cbk_cache * cache /* cache to release */ )
{
	assert("nikita-2493", cache != NULL);
	if (cache->slot != NULL)
		kfree(cache->slot);
}

/* Audited by: green(2002.06.15) */
static inline void
cbk_cache_lock(cbk_cache * cache /* cache to lock */ )
{
	assert("nikita-1800", cache != NULL);
	spin_lock(&cache->guard);
}

/* Audited by: green(2002.06.15) */
static inline void
cbk_cache_unlock(cbk_cache * cache /* cache to unlock */ )
{
	assert("nikita-1801", cache != NULL);
	spin_unlock(&cache->guard);
}

/* macro to iterate over all cbk cache slots */
#define for_all_slots( cache, slot )					\
	for( ( slot ) = cbk_cache_list_front( &( cache ) -> lru ) ;	\
	     !cbk_cache_list_end( &( cache ) -> lru, ( slot ) ) ; 	\
	     ( slot ) = cbk_cache_list_next( slot ) )

#if REISER4_DEBUG_OUTPUT
/* Debugging aid: print human readable information about @slot */
void
print_cbk_slot(const char *prefix /* prefix to print */ ,
	       const cbk_cache_slot * slot /* slot to print */ )
{
	if (slot == NULL)
		printk("%s: null slot\n", prefix);
	else
		print_znode("node", slot->node);
}

/* Debugging aid: print human readable information about @cache */
void
print_cbk_cache(const char *prefix /* prefix to print */ ,
		const cbk_cache * cache /* cache to print */ )
{
	if (cache == NULL)
		printk("%s: null cache\n", prefix);
	else {
		cbk_cache_slot *scan;

		printk("%s: cache: %p\n", prefix, cache);
		for_all_slots(cache, scan)
		    print_cbk_slot("slot", scan);
	}
}
#endif

#if REISER4_DEBUG
/* this function assures that [cbk-cache-invariant] invariant holds */
static int
cbk_cache_invariant(const cbk_cache * cache)
{
	cbk_cache_slot *slot;
	int result;
	int unused;

	assert("nikita-2469", cache != NULL);
	unused = 0;
	result = 1;
	cbk_cache_lock((cbk_cache *) cache);
	for_all_slots(cache, slot) {
		/* in LRU first go all `used' slots followed by `unused' */
		if (unused && (slot->node != NULL))
			result = 0;
		if (slot->node == NULL)
			unused = 1;
		else {
			cbk_cache_slot *scan;

			/* all cached nodes are different */
			scan = slot;
			while (result) {
				scan = cbk_cache_list_next(scan);
				if (cbk_cache_list_end(&cache->lru, scan))
					break;
				if (slot->node == scan->node)
					result = 0;
			}
		}
		if (!result)
			break;
	}
	cbk_cache_unlock((cbk_cache *) cache);
	return result;
}

#endif

/* Remove references, if any, to @node from coord cache */
void
cbk_cache_invalidate(const znode * node /* node to remove from cache */ ,
		     reiser4_tree * tree /* tree to remove node from */ )
{
	cbk_cache_slot *slot;
	cbk_cache *cache;

	assert("nikita-350", node != NULL);
	ON_DEBUG_CONTEXT(assert("nikita-1479", lock_counters()->rw_locked_tree > 0));

	cache = &tree->cbk_cache;
	assert("nikita-2470", cbk_cache_invariant(cache));

	cbk_cache_lock(cache);
	for_all_slots(cache, slot) {
		if (slot->node == NULL)
			break;
		if (slot->node == node) {
			cbk_cache_list_remove(slot);
			cbk_cache_list_push_back(&cache->lru, slot);
			slot->node = NULL;
			break;
		}
	}
	cbk_cache_unlock(cache);
	assert("nikita-2471", cbk_cache_invariant(cache));
}

/* add to the cbk-cache in the "tree" information about "node". This
    can actually be update of existing slot in a cache. */
void
cbk_cache_add(const znode * node /* node to add to the cache */ )
{
	cbk_cache *cache;
	cbk_cache_slot *slot;

	assert("nikita-352", node != NULL);

	cache = &znode_get_tree(node)->cbk_cache;
	assert("nikita-2472", cbk_cache_invariant(cache));
	cbk_cache_lock(cache);
	/* find slot to update/add */
	for_all_slots(cache, slot) {
		/* oops, this node is already in a cache */
		if (slot->node == node)
			break;
		if (slot->node == NULL) {
			slot = NULL;
			break;
		}
	}
	/* if all slots are used, reuse least recently used one */
	if ((slot == NULL) || cbk_cache_list_end(&cache->lru, slot))
		slot = cbk_cache_list_back(&cache->lru);
	slot->node = (znode *) node;
	cbk_cache_list_remove(slot);
	cbk_cache_list_push_front(&cache->lru, slot);
	cbk_cache_unlock(cache);
	assert("nikita-2473", cbk_cache_invariant(cache));
}

static void setup_delimiting_keys(cbk_handle * h);
static lookup_result coord_by_handle(cbk_handle * handle);
static lookup_result traverse_tree(cbk_handle * h);
static int cbk_cache_search(cbk_handle * h);

static level_lookup_result cbk_level_lookup(cbk_handle * h);
static level_lookup_result cbk_node_lookup(cbk_handle * h);

/* helper functions */

/* release parent node during traversal */
static void put_parent(cbk_handle * h);
/* check consistency of fields */
static int sanity_check(cbk_handle * h);
/* release resources in handle */
static void hput(cbk_handle * h);

static level_lookup_result search_to_left(cbk_handle * h);

/* main tree lookup procedure
  
   Check coord cache. If key we are looking for is not found there, call cbk()
   to do real tree traversal.
  
   As we have extents on the twig level, @lock_level and @stop_level can
   be different from LEAF_LEVEL and each other.
  
   Thread cannot keep any reiser4 locks (tree, znode, dk spin-locks, or znode
   long term locks) while calling this. 
*/
lookup_result coord_by_key(reiser4_tree * tree	/* tree to perform search
						 * in. Usually this tree is
						 * part of file-system
						 * super-block */ ,
			   const reiser4_key * key /* key to look for */ ,
			   coord_t * coord	/* where to store found
						   * position in a tree. Fields
						   * in "coord" are only valid if
						   * coord_by_key() returned
						   * "CBK_COORD_FOUND" */ ,
			   lock_handle * lh,	/* NIKITA-FIXME-HANS: comment needed */
			   znode_lock_mode lock_mode	/* type of lookup we
							 * want on node. Pass
							 * ZNODE_READ_LOCK here
							 * if you only want to
							 * read item found and
							 * ZNODE_WRITE_LOCK if
							 * you want to modify
							 * it */ ,
			   lookup_bias bias	/* what to return if coord
						 * with exactly the @key is
						 * not in the tree */ ,
			   tree_level lock_level	/* tree level where to start
							 * taking @lock type of
							 * locks */ ,
			   tree_level stop_level	/* tree level to stop. Pass
							 * LEAF_LEVEL or TWIG_LEVEL
							 * here Item being looked
							 * for has to be between
							 * @lock_level and
							 * @stop_level, inclusive */ ,
			   __u32 flags /* search flags */,
			   ra_info_t *info /* information about desired tree traversal readahead */)
{
	cbk_handle handle;
	lock_handle parent_lh;
	init_lh(lh);
	init_lh(&parent_lh);

	assert("nikita-3023", schedulable());

	assert("nikita-353", tree != NULL);
	assert("nikita-354", key != NULL);
	assert("nikita-355", coord != NULL);
	assert("nikita-356", (bias == FIND_EXACT) || (bias == FIND_MAX_NOT_MORE_THAN));
	assert("nikita-357", stop_level >= LEAF_LEVEL);
	/* no locks can be held during tree traversal */
	assert("nikita-2104", lock_stack_isclean(get_current_lock_stack()));
	trace_stamp(TRACE_TREE);

	xmemset(&handle, 0, sizeof handle);

	handle.tree = tree;
	handle.key = key;
	handle.lock_mode = lock_mode;
	handle.bias = bias;
	handle.lock_level = lock_level;
	handle.stop_level = stop_level;
	handle.coord = coord;
	/* set flags. See comment in tree.h:cbk_flags */
	handle.flags = flags | CBK_TRUST_DK;

	handle.active_lh = lh;
	handle.parent_lh = &parent_lh;
	
	handle.ra_info = info;

	return coord_by_handle(&handle);
}

static lookup_result
coord_by_handle(cbk_handle * handle)
{
	write_tree_trace(handle->tree, tree_lookup, handle->key);

	/* first check whether "key" is in cache of recent lookups. */
	if (cbk_cache_search(handle) == 0)
		return handle->result;
	else
		return traverse_tree(handle);
}

/* Execute actor for each item (or unit, depending on @through_units_p),
   starting from @coord, right-ward, until either: 
  
   - end of the tree is reached
   - unformatted node is met
   - error occurred
   - @actor returns 0 or less
  
   Error code, or last actor return value is returned.
  
   This is used by readdir() and alikes.
*/
int
iterate_tree(reiser4_tree * tree /* tree to scan */ ,
	     coord_t * coord /* coord to start from */ ,
	     lock_handle * lh	/* lock handle to start with and to
				   * update along the way */ ,
	     tree_iterate_actor_t actor	/* function to call on each
					 * item/unit */ ,
	     void *arg /* argument to pass to @actor */ ,
	     znode_lock_mode mode /* lock mode on scanned nodes */ ,
	     int through_units_p	/* call @actor on each item or on each
					 * unit */ )
{
	int result;

	assert("nikita-1143", tree != NULL);
	assert("nikita-1145", coord != NULL);
	assert("nikita-1146", lh != NULL);
	assert("nikita-1147", actor != NULL);

	result = zload(coord->node);
	if (result != 0)
		return result;
	if (!coord_is_existing_unit(coord)) {
		zrelse(coord->node);
		return -ENOENT;
	}
	while ((result = actor(tree, coord, lh, arg)) > 0) {
		/* move further  */
		if ((through_units_p && coord_next_unit(coord)) || (!through_units_p && coord_next_item(coord))) {
			do {
				lock_handle couple;

				/* move to the next node  */
				init_lh(&couple);
				result = reiser4_get_right_neighbor(&couple, coord->node, (int) mode, GN_DO_READ);
				zrelse(coord->node);
				if (result == 0) {

					result = zload(couple.node);
					if (result != 0) {
						done_lh(&couple);
						return result;
					}

					coord_init_first_unit(coord, couple.node);
					done_lh(lh);
					move_lh(lh, &couple);
				} else
					return result;
			} while (node_is_empty(coord->node));
		}

		assert("nikita-1149", coord_is_existing_unit(coord));
	}
	zrelse(coord->node);
	return result;
}

int get_fake_znode(reiser4_tree * tree, znode_lock_mode mode, 
		   znode_lock_request pri, lock_handle *lh)
{
	znode *fake;
	int result;

	fake = zget(tree, &FAKE_TREE_ADDR, NULL, 0, GFP_KERNEL);

	if (!IS_ERR(fake)) {
		result = longterm_lock_znode(lh, fake, mode, pri);
		zput(fake);
	} else
		result = PTR_ERR(fake);
	return result;
}

/* main function that handles common parts of tree traversal: starting
    (fake znode handling), restarts, error handling, completion */
static lookup_result
traverse_tree(cbk_handle * h /* search handle */ )
{
	int done;
	int iterations;

	assert("nikita-365", h != NULL);
	assert("nikita-366", h->tree != NULL);
	assert("nikita-367", h->key != NULL);
	assert("nikita-368", h->coord != NULL);
	assert("nikita-369", (h->bias == FIND_EXACT) || (h->bias == FIND_MAX_NOT_MORE_THAN));
	assert("nikita-370", h->stop_level >= LEAF_LEVEL);
	assert("nikita-2949", !(h->flags & CBK_DKSET));
	assert("zam-355", lock_stack_isclean(get_current_lock_stack()));

	trace_stamp(TRACE_TREE);
	reiser4_stat_inc(tree.cbk);

	iterations = 0;

	/* loop for restarts */
restart:

	assert("nikita-3024", schedulable());

	h->result = CBK_COORD_FOUND;

	done = get_fake_znode(h->tree, ZNODE_READ_LOCK, ZNODE_LOCK_LOPRI,
			      h->parent_lh);

	assert("nikita-1637", done != -EDEADLK);

	if (done)
		return done;

	/* connect_znode() needs it */
	h->coord->node = h->parent_lh->node;
	h->ld_key = *min_key();
	h->rd_key = *max_key();
	h->flags |= CBK_DKSET;

	h->block = h->tree->root_block;
	h->level = h->tree->height;
	h->error = NULL;

	/* loop descending a tree */
	while (!done) {

		if (unlikely((iterations > REISER4_CBK_ITERATIONS_LIMIT) &&
			     IS_POW(iterations))) {
			warning("nikita-1481", "Too many iterations: %i", iterations);
			print_key("key", h->key);
			++iterations;
		} else if (unlikely(iterations > REISER4_MAX_CBK_ITERATIONS)) {
			h->error =
			    "reiser-2018: Too many iterations. Tree corrupted, or (less likely) starvation occurring.";
			h->result = -EIO;
			break;
		}
		switch (cbk_level_lookup(h)) {
		case LOOKUP_CONT:
			move_lh(h->parent_lh, h->active_lh);
			continue;
		default:
			wrong_return_value("nikita-372", "cbk_level");
		case LOOKUP_DONE:
			done = 1;
			break;
		case LOOKUP_REST:
			reiser4_stat_inc(tree.cbk_restart);
			hput(h);
			++iterations;
			preempt_point();
			goto restart;
		}
	}
	/* that's all. The rest is error handling */
	if (unlikely(h->error != NULL)) {
		warning("nikita-373", "%s: level: %i, "
			"lock_level: %i, stop_level: %i "
			"lock_mode: %s, bias: %s",
			h->error, h->level, h->lock_level, h->stop_level,
			lock_mode_name(h->lock_mode), bias_name(h->bias));
		print_address("block", &h->block);
		print_key("key", h->key);
		print_coord_content("coord", h->coord);
		print_znode("active", h->active_lh->node);
		print_znode("parent", h->parent_lh->node);
	}
	/* `unlikely' error case */
	if (unlikely((h->result != CBK_COORD_FOUND) && (h->result != CBK_COORD_NOTFOUND))) {
		/* failure. do cleanup */
		hput(h);
	} else {
		assert("nikita-1605", WITH_DATA_RET
		       (h->coord->node, 1,
			ergo((h->result == CBK_COORD_FOUND) &&
			     (h->bias == FIND_EXACT) &&
			     (!node_is_empty(h->coord->node)), coord_is_existing_item(h->coord))));
	}
	write_tree_trace(h->tree, tree_exit);
	return h->result;
}

/* Perform tree lookup at one level. This is called from cbk_traverse()
   function that drives lookup through tree and calls cbk_node_lookup() to
   perform lookup within one node.
  
   See comments in a code.
*/
static level_lookup_result
cbk_level_lookup(cbk_handle * h /* search handle */ )
{
	int ret;
	znode *active;

	assert("nikita-3025", schedulable());

	/* acquire reference to @active node */
	active = zget(h->tree, &h->block, h->parent_lh->node, h->level, GFP_KERNEL);

	if (IS_ERR(active)) {
		h->result = PTR_ERR(active);
		return LOOKUP_DONE;
	}

	/* lock @active */
	h->result = longterm_lock_znode(h->active_lh, active, cbk_lock_mode(h->level, h), ZNODE_LOCK_LOPRI);
	/* longterm_lock_znode() acquires additional reference to znode (which
	   will be later released by longterm_unlock_znode()). Release
	   reference acquired by zget().
	*/
	zput(active);
	if (h->result)
		goto fail_or_restart;

	/* if @active is accessed for the first time, setup delimiting keys on
	   it. Delimiting keys are taken from the parent node. See
	   setup_delimiting_keys() for details. 
	*/
	if (h->flags & CBK_DKSET) {
		setup_delimiting_keys(h);
		h->flags &= ~CBK_DKSET;
	} else {
		znode *parent;

		parent = h->parent_lh->node;
		h->result = zload(parent);
		if (h->result)
			goto fail_or_restart;

		set_child_delimiting_keys(parent, h->coord, 
					  h->active_lh->node);
		zrelse(parent);
	}

	/* this is ugly kludge. Reminder: this is necessary, because
	   ->lookup() method returns coord with ->between field probably set
	   to something different from AT_UNIT.
	*/
	h->coord->between = AT_UNIT;

	/* if we are going to load znode right now, setup
	   ->in_parent: coord where pointer to this node is stored in
	   parent.
	*/
	write_lock_tree(h->tree);

	if (znode_just_created(active) && (h->coord->node != NULL))
		active->in_parent = *h->coord;

	/* protect sibling pointers and `connected' state bits, check
	   znode state */
	ret = znode_is_connected(active);

	/* above two operations (setting ->in_parent up and checking
	   connectedness) are logically separate and one can release and
	   re-acquire tree lock between them. On the other hand,
	   releasing-acquiring spinlock requires d-cache flushing on some
	   architectures and can thus be expensive.
	*/
	write_unlock_tree(h->tree);

	if (!ret) {
		/* FIXME: h->coord->node and active are of different levels? */
		h->result = connect_znode(h->coord, active);
		if (h->result) {
			put_parent(h);
			goto fail_or_restart;
		}
	}

	/* FIXME: there is a guess that right delimiting key which are brought from the parent can be incorrect
	   already */
	spin_lock_dk(h->tree);
	read_lock_tree(h->tree);
	if (ZF_ISSET(active, JNODE_RIGHT_CONNECTED) && active->right) {
		if (!keyeq(znode_get_rd_key(active), znode_get_ld_key(active->right))) {
			printk("cbk_level_lookup: right delimiting key changed. Corrected\n");
			znode_set_rd_key(active, znode_get_ld_key(active->right));
		}
	}
	read_unlock_tree(h->tree);
	spin_unlock_dk(h->tree);



	/* put_parent() cannot be called earlier, because connect_znode()
	   assumes parent node is referenced; */
	put_parent(h);

	if ((!znode_contains_key_lock(active, h->key) &&
	     (h->flags & CBK_TRUST_DK)) || ZF_ISSET(active, JNODE_HEARD_BANSHEE)) {
		/* 1. key was moved out of this node while this thread was
		   waiting for the lock. Restart. More elaborate solution is
		   to determine where key moved (to the left, or to the right)
		   and try to follow it through sibling pointers.
		  
		   2. or, node itself is going to be removed from the
		   tree. Release lock and restart.
		*/
		if (REISER4_STATS) {
			if (znode_contains_key_lock(active, h->key))
				reiser4_stat_inc_at_level(h->level, cbk_met_ghost);
			else
				reiser4_stat_inc_at_level(h->level, cbk_key_moved);
		}
		h->result = -EAGAIN;
	}
	if (h->result == -EAGAIN)
		return LOOKUP_REST;

	h->result = zload_ra(active, h->ra_info);
	if (h->result) {
		return LOOKUP_DONE;
	}

	/* sanity checks */
	if (sanity_check(h)) {
		zrelse(active);
		return LOOKUP_DONE;
	}

	ret = cbk_node_lookup(h);

	/* reget @active from handle, because it can change in
	   cbk_node_lookup()  */
	active = h->active_lh->node;
	zrelse(active);

	return ret;

fail_or_restart:
	if (h->result == -EDEADLK)
		return LOOKUP_REST;
	return LOOKUP_DONE;
}

void check_dkeys(const znode *node);

/* Process one node during tree traversal.
  
   This is called by cbk_level_lookup(). */
static level_lookup_result
cbk_node_lookup(cbk_handle * h /* search handle */ )
{
	/* node plugin of @active */
	node_plugin *nplug;
	/* item plugin of item that was found */
	item_plugin *iplug;
	/* search bias */
	lookup_bias node_bias;
	/* node we are operating upon */
	znode *active;
	/* tree we are searching in */
	reiser4_tree *tree;
	/* result */
	int result;

	/* true if @key is left delimiting key of @node */
	static int key_is_ld(znode * node, const reiser4_key * key) {
		int ld;

		 assert("nikita-1716", node != NULL);
		 assert("nikita-1758", key != NULL);

		 spin_lock_dk(znode_get_tree(node));
		 assert("nikita-1759", znode_contains_key(node, key));
		 ld = keyeq(znode_get_ld_key(node), key);
		 spin_unlock_dk(znode_get_tree(node));
		 return ld;
	}
	assert("nikita-379", h != NULL);

	active = h->active_lh->node;
	tree = h->tree;

	nplug = active->nplug;
	assert("nikita-380", nplug != NULL);

	/* FIXME: remove after debugging */
	check_dkeys(active);

	/* return item from "active" node with maximal key not greater than
	   "key"  */
	node_bias = h->bias;
	result = nplug->lookup(active, h->key, node_bias, h->coord);
	if (unlikely(result != NS_FOUND && result != NS_NOT_FOUND)) {
		/* error occured */
		h->result = result;
		return LOOKUP_DONE;
	}
	if (h->level == h->stop_level) {
		/* welcome to the stop level */
		assert("nikita-381", h->coord->node == active);
		if (result == NS_FOUND) {
			/* success of tree lookup */
			/* following assertion doesn't work currently, because
			   ->lookup method of internal item sets ->between ==
			   AFTER_UNIT and bias is unconditionally set to
			   FIND_EXACT above (why?)
			*/
			assert("nikita-1604", 1 || ergo(h->bias == FIND_EXACT, coord_is_existing_unit(h->coord)));
			if (!(h->flags & CBK_UNIQUE) && key_is_ld(active, h->key)) {
				return search_to_left(h);
			} else
				h->result = CBK_COORD_FOUND;
			reiser4_stat_inc(tree.cbk_found);
		} else {
			h->result = CBK_COORD_NOTFOUND;
			reiser4_stat_inc(tree.cbk_notfound);
		}
		cbk_cache_add(active);
		return LOOKUP_DONE;
	}

	if (h->level > TWIG_LEVEL && result == NS_NOT_FOUND) {
		h->error = "not found on internal node";
		h->result = result;
		return LOOKUP_DONE;
	}

	assert("vs-361", h->level > h->stop_level);

	if (handle_eottl(h, &result))
		return result;

	assert("nikita-2116", item_is_internal(h->coord));
	iplug = item_plugin_by_coord(h->coord);

	/* go down to next level */
	assert("vs-515", item_is_internal(h->coord));
	iplug->s.internal.down_link(h->coord, h->key, &h->block);
	--h->level;
	return LOOKUP_CONT;	/* continue */
}

/* multi-key search: comment it out and leave rotting until really needed. */
#if 0

/* look for several keys at once. 
  
   Outline:
  
   One cannot just issue several tree traversals in sequence without releasing
   locks on lookup results, because keeping a lock at the bottom of the tree
   while doing new top-to-bottom traversal can easily lead to the
   deadlock. (Actually, there is assertion at the very beginning of
   coord_by_key(), checking that no locks are held.)
  
   Still, node-level locking for rename requires locking of several nodes at
   once, before starting balancings.
  
   lookup_multikey() uses seals (see seal.[ch]) to work around deadlocks:
  
   tree lookups are issued starting from the largest key in decsending key
   order. For each, but the smallest key, after lookup finishes, its result is
   sealed and corresponding node is unlocked.
  
   After all lookups were performed, we have result of lookup of smallest key
   locked and results of the rest of lookups sealed.
  
   All seals are re-validated in ascending key order. If seal is found broken,
   all locks and seals are released and process repeated.
  
   See comments in the body.
*/
int
lookup_multikey(cbk_handle * handle /* handles to search */ ,
		int nr_keys /* number of handles */ )
{
	seal_t seal[REISER4_MAX_MULTI_SEARCH - 1];
	int i;
	int result;
	int once_again;

	/* helper routine to clean up seals and locks */
	static void done_handles(void) {
		for (i = 0; i < nr_keys - 1; ++i)
			seal_done(&seal[i]);
		if (result != 0) {
			for (i = 0; i < nr_keys - 1; ++i)
				done_lh(handle[i].active_lh);
		}
	}
	assert("nikita-2147", handle != NULL);
	assert("nikita-2148", (0 <= nr_keys) && (nr_keys <= REISER4_MAX_MULTI_SEARCH));

	if (REISER4_DEBUG) {
		/* check that @handle is sorted */

		for (i = 1; i < nr_keys; ++i) {
			assert("nikita-2149", keyle(handle[i - 1].key, handle[i].key));
		}
	}

	for (i = 0; i < nr_keys; ++i)
		init_lh(handle[i].parent_lh);

	for (i = 0; i < nr_keys - 1; ++i)
		seal_init(&seal[i], NULL, NULL);

	/* main loop */
	do {
		once_again = 0;
		/* issue lookups from right to left */
		for (i = nr_keys - 1; i >= 0; --i) {
			cbk_handle *h;

			h = &handle[i];
			result = coord_by_handle(h);
			/* some error, abort */
			if ((result != CBK_COORD_FOUND) && (result != CBK_COORD_NOTFOUND))
				break;
			else
				result = 0;
			if (i == 0)
				break;
			/* seal lookup result */
			seal_init(&seal[i - 1], h->coord, h->key);
			/* and unlock it */
			done_lh(h->active_lh);
		}
		if (result != 0)
			break;
		/* result of smallest key lookup is locked. Others are
		   unlocked, but sealed, try to validate and relock them. */
		for (i = 1; i < nr_keys; ++i) {
			cbk_handle *h;

			h = &handle[i];
			result = seal_validate(&seal[i - 1],
					       h->coord, h->key, h->level, h->active_lh, h->bias, h->lock_mode,
					       /* going from left to right we
					          can request high-priority
					          lock. This is only valid if
					          all handles are targeted to
					          the same level, though.
					       */
					       ZNODE_LOCK_HIPRI);
			if (result == -EAGAIN) {
				/* seal was broken, restart */
				once_again = 1;
				reiser4_stat_tree_add(multikey_restart);
				break;
			}
			/* some other error */
			if (result != 0)
				break;
		}
		done_handles();
	} while (once_again);

	done_handles();
	return result;
}

/* lookup two keys in a tree. This is required for node-level locking during
   rename. Arguments are similar to these of coord_by_key(). 
  
   Returned value: if some sort of unexpected error (-EIO, -ENOMEM) happened,
   all locks are released, all seals are invalidated, and error code is
   returned. *result1 and *result2 are not modified. If searches completed
   successfully (items were either found, or not found), 0 is returned and
    result1 and *result2 contain search results for respective keys.
  
*/
int
lookup_couple(reiser4_tree * tree /* tree to perform search in */ ,
	      const reiser4_key * key1 /* first key to look for */ ,
	      const reiser4_key * key2 /* second key to look for */ ,
	      coord_t * coord1 /* where to store result for the @key1 */ ,
	      coord_t * coord2 /* where to store result for the @key2 */ ,
	      lock_handle * lh1 /* where to keep lock for @coord1 */ ,
	      lock_handle * lh2 /* where to keep lock for @coord2 */ ,
	      znode_lock_mode lock_mode	/* type of lookup we want on
					 * node. */ ,
	      lookup_bias bias	/* what to return if coord with exactly
				 * the @key is not in the tree */ ,
	      tree_level lock_level	/* tree level where to start taking
					 * @lock type of locks */ ,
	      tree_level stop_level	/* tree level to stop. Pass
					 * LEAF_LEVEL or TWIG_LEVEL here Item
					 * being looked for has to be between
					 * @lock_level and @stop_level,
					 * inclusive */ ,
	      __u32 flags /* search flags */ ,
	      lookup_result * result1	/* where to put result of search for
					 * @key1 */ ,
	      lookup_result * result2	/* where to put result of search for
					 * @key2 */ )
{
	cbk_handle handle[2];
	lock_handle parent_lh[2];
	int first_pos;
	int secnd_pos;
	int result;

	cassert(REISER4_MAX_MULTI_SEARCH >= 2);
	assert("nikita-2139", tree != NULL);
	assert("nikita-2140", key1 != NULL);
	assert("nikita-2141", key2 != NULL);
	assert("nikita-2142", coord1 != NULL);
	assert("nikita-2143", coord2 != NULL);
	assert("nikita-2150", lh1 != NULL);
	assert("nikita-2151", lh2 != NULL);
	assert("nikita-2144", (bias == FIND_EXACT) || (bias == FIND_MAX_NOT_MORE_THAN));
	assert("nikita-2145", stop_level >= LEAF_LEVEL);
	assert("nikita-2146", lock_stack_isclean(get_current_lock_stack()));
	trace_stamp(TRACE_TREE);

	if (keylt(key1, key2)) {
		first_pos = 0;
		secnd_pos = 1;
	} else {
		first_pos = 1;
		secnd_pos = 0;
	}

	xmemset(&handle, 0, sizeof handle);

	handle[first_pos].tree = tree;
	handle[first_pos].key = key1;
	handle[first_pos].lock_mode = lock_mode;
	handle[first_pos].bias = bias;
	handle[first_pos].lock_level = lock_level;
	handle[first_pos].stop_level = stop_level;
	handle[first_pos].coord = coord1;
	handle[first_pos].flags = flags | CBK_TRUST_DK;

	handle[first_pos].active_lh = lh1;
	handle[first_pos].parent_lh = &parent_lh[first_pos];

	handle[secnd_pos].tree = tree;
	handle[secnd_pos].key = key2;
	handle[secnd_pos].lock_mode = lock_mode;
	handle[secnd_pos].bias = bias;
	handle[secnd_pos].lock_level = lock_level;
	handle[secnd_pos].stop_level = stop_level;
	handle[secnd_pos].coord = coord2;
	handle[secnd_pos].flags = flags | CBK_TRUST_DK;

	handle[secnd_pos].active_lh = lh2;
	handle[secnd_pos].parent_lh = &parent_lh[secnd_pos];

	result = lookup_multikey(handle, 2);
	if (result == 0) {
		*result1 = handle[first_pos].result;
		*result2 = handle[secnd_pos].result;
	}
	return result;
}
#endif

/* true if @key is strictly within @node
  
   we are looking for possibly non-unique key and it is item is at the edge of
   @node. May be it is in the neighbor.
*/
static int
znode_contains_key_strict(znode * node	/* node to check key
					 * against */ ,
			  const reiser4_key * key /* key to check */ )
{
	assert("nikita-1760", node != NULL);
	assert("nikita-1722", key != NULL);
	assert("zam-839", spin_dk_is_locked(znode_get_tree(node)));
	
	return keylt(znode_get_ld_key(node), key) && keylt(key, znode_get_rd_key(node));
}

static int
cbk_cache_scan_slots(cbk_handle * h /* cbk handle */ )
{
	level_lookup_result llr;
	znode *node;
	reiser4_tree *tree;
	cbk_cache_slot *slot;
	cbk_cache *cache;
	tree_level level;
	const reiser4_key *key;
	int result;

	assert("nikita-1317", h != NULL);
	assert("nikita-1315", h->tree != NULL);
	assert("nikita-1316", h->key != NULL);

	tree = h->tree;
	cache = &tree->cbk_cache;
	if (cache->nr_slots == 0)
		/* size of cbk cache was set to 0 by mount time option. */
		return -ENOENT;

	assert("nikita-2474", cbk_cache_invariant(cache));
	node = NULL;		/* to keep gcc happy */
	level = h->level;
	key = h->key;

	spin_lock_dk(tree);
	read_lock_tree(tree);
	cbk_cache_lock(cache);
	slot = cbk_cache_list_prev(cbk_cache_list_front(&cache->lru));
	while (1) {
		slot = cbk_cache_list_next(slot);

		if (!cbk_cache_list_end(&cache->lru, slot)) {
			node = slot->node;
		} else
			node = NULL;

		if (node == NULL)
			break;

		if ((znode_get_level(node) == level) &&
		    /* min_key < key < max_key */
		    znode_contains_key_strict(node, key)) {
			zref(node);
			break;
		}
	}

	cbk_cache_unlock(cache);
	read_unlock_tree(tree);
	spin_unlock_dk(tree);

	assert("nikita-2475", cbk_cache_invariant(cache));

	if ((node == NULL) || cbk_cache_list_end(&cache->lru, slot)) {
		h->result = CBK_COORD_NOTFOUND;
		return -ENOENT;
	}

	result = longterm_lock_znode(h->active_lh, node, cbk_lock_mode(level, h), ZNODE_LOCK_LOPRI);
	zput(node);
	if (result != 0)
		return result;
	result = zload(node);
	if (result != 0)
		return result;

	/* recheck keys */
	result = UNDER_SPIN(dk, tree, znode_contains_key_strict(node, key))
		&& !ZF_ISSET(node, JNODE_HEARD_BANSHEE);

	if (result) {
		/* do lookup inside node */
		llr = cbk_node_lookup(h);
		/* if cbk_node_lookup() wandered to another node (due to eottl
		   or non-unique keys), adjust @node */
		node = h->active_lh->node;

		if (llr != LOOKUP_DONE) {
			/* restart of continue on the next level */
			reiser4_stat_inc(tree.cbk_cache_wrong_node);
			result = -ENOENT;
		} else if ((h->result != CBK_COORD_NOTFOUND) && (h->result != CBK_COORD_FOUND))
			/* io or oom */
			result = -ENOENT;
		else {
			/* good. Either item found or definitely not found. */
			result = 0;

			cbk_cache_lock(cache);
			if (slot->node == node) {
				/* if this node is still in cbk cache---move
				   its slot to the head of the LRU list. */
				cbk_cache_list_remove(slot);
				cbk_cache_list_push_front(&cache->lru, slot);
			}
			cbk_cache_unlock(cache);
		}
	} else {
		/* race. While this thread was waiting for the lock, node was
		   rebalanced and item we are looking for, shifted out of it
		   (if it ever was here).
		  
		   Continuing scanning is almost hopeless: node key range was
		   moved to, is almost certainly at the beginning of the LRU
		   list at this time, because it's hot, but restarting
		   scanning from the very beginning is complex. Just return,
		   so that cbk() will be performed. This is not that
		   important, because such races should be rare. Are they?
		*/
		reiser4_stat_inc(tree.cbk_cache_race);
		result = -ENOENT;	/* -ERAUGHT */
	}
	zrelse(node);
	assert("nikita-2476", cbk_cache_invariant(cache));
	return result;
}

/* look for item with given key in the coord cache
  
   This function, called by coord_by_key(), scans "coord cache" (&cbk_cache)
   which is a small LRU list of znodes accessed lately. For each znode in
   znode in this list, it checks whether key we are looking for fits into key
   range covered by this node. If so, and in addition, node lies at allowed
   level (this is to handle extents on a twig level), node is locked, and
   lookup inside it is performed.
  
   we need a measurement of the cost of this cache search compared to the cost
   of coord_by_key.
  
*/
static int
cbk_cache_search(cbk_handle * h /* cbk handle */ )
{
	int result = 0;
	tree_level level;

	for (level = h->stop_level; level <= h->lock_level; ++level) {
		h->level = level;
		result = cbk_cache_scan_slots(h);
		if (result != 0) {
			done_lh(h->active_lh);
			done_lh(h->parent_lh);
			reiser4_stat_inc(tree.cbk_cache_miss);
		} else {
			assert("nikita-1319", (h->result == CBK_COORD_NOTFOUND) || (h->result == CBK_COORD_FOUND));
			reiser4_stat_inc(tree.cbk_cache_hit);
			write_tree_trace(h->tree, tree_cached);
			break;
		}
	}
	return result;
}

/* type of lock we want to obtain during tree traversal. On stop level
    we want type of lock user asked for, on upper levels: read lock. */
znode_lock_mode cbk_lock_mode(tree_level level, cbk_handle * h)
{
	assert("nikita-382", h != NULL);

	return (level <= h->lock_level) ? h->lock_mode : ZNODE_READ_LOCK;
}

/* find delimiting keys of child
  
   Determine left and right delimiting keys for child pointed to by
   @parent_coord.
  
*/
static int
find_child_delimiting_keys(znode * parent	/* parent znode, passed
						 * locked */ ,
			   const coord_t * parent_coord	/* coord where
							   * pointer to
							   * child is
							   * stored */ ,
			   reiser4_key * ld	/* where to store left
						 * delimiting key */ ,
			   reiser4_key * rd	/* where to store right
						 * delimiting key */ )
{
	coord_t neighbor;

	assert("nikita-1484", parent != NULL);
	assert("nikita-1485", spin_dk_is_locked(znode_get_tree(parent)));

	coord_dup(&neighbor, parent_coord);

	if (neighbor.between == AT_UNIT)
		/* imitate item ->lookup() behavior. */
		neighbor.between = AFTER_UNIT;

	if (coord_is_existing_unit(&neighbor) || (coord_set_to_left(&neighbor) == 0))
		unit_key_by_coord(&neighbor, ld);
	else
		*ld = *znode_get_ld_key(parent);

	coord_dup(&neighbor, parent_coord);
	if (neighbor.between == AT_UNIT)
		neighbor.between = AFTER_UNIT;
	if (coord_set_to_right(&neighbor) == 0)
		unit_key_by_coord(&neighbor, rd);
	else
		*rd = *znode_get_rd_key(parent);

	return 0;
}

void
set_child_delimiting_keys(znode * parent,
			  const coord_t * coord, znode * child)
{
	reiser4_tree *tree;

	assert("nikita-2952", 
	       znode_get_level(parent) == znode_get_level(coord->node));

	tree = znode_get_tree(parent);
	spin_lock_dk(tree);
	if (!ZF_ISSET(child, JNODE_DKSET)) {
		find_child_delimiting_keys(parent, coord, 
					   znode_get_ld_key(child),
					   znode_get_rd_key(child));
		ZF_SET(child, JNODE_DKSET);
	}
	spin_unlock_dk(tree);
}

static level_lookup_result
search_to_left(cbk_handle * h /* search handle */ )
{
	level_lookup_result result;
	coord_t *coord;
	znode *node;
	znode *neighbor;

	lock_handle lh;

	assert("nikita-1761", h != NULL);
	assert("nikita-1762", h->level == h->stop_level);

	init_lh(&lh);
	coord = h->coord;
	node = h->active_lh->node;
	assert("nikita-1763", coord_is_leftmost_unit(coord));

	reiser4_stat_inc(tree.check_left_nonuniq);
	h->result = reiser4_get_left_neighbor(&lh, node, (int) h->lock_mode, GN_DO_READ);
	neighbor = NULL;
	switch (h->result) {
	case -EDEADLK:
		result = LOOKUP_REST;
		break;
	case 0:{
			node_plugin *nplug;
			coord_t crd;
			lookup_bias bias;

			neighbor = lh.node;
			h->result = zload(neighbor);
			if (h->result != 0) {
				result = LOOKUP_DONE;
				break;
			}

			nplug = neighbor->nplug;

			coord_init_zero(&crd);
			bias = h->bias;
			h->bias = FIND_EXACT;
			h->result = nplug->lookup(neighbor, h->key, h->bias, &crd);
			h->bias = bias;

			if (h->result == NS_NOT_FOUND) {
	case -ENAVAIL:
				h->result = CBK_COORD_FOUND;
				reiser4_stat_inc(tree.cbk_found);
				cbk_cache_add(node);
	default:		/* some other error */
				result = LOOKUP_DONE;
			} else if (h->result == NS_FOUND) {
				reiser4_stat_inc(tree.left_nonuniq_found);

				spin_lock_dk(znode_get_tree(neighbor));
				h->rd_key = *znode_get_ld_key(node);
				leftmost_key_in_node(neighbor, &h->ld_key);
				spin_unlock_dk(znode_get_tree(neighbor));
				h->flags |= CBK_DKSET;

				h->block = *znode_get_block(neighbor);
				/* clear coord -> node so that cbk_level_lookup()
				   wouldn't overwrite parent hint in neighbor.
				  
				   Parent hint was set up by
				   reiser4_get_left_neighbor()
				*/
				UNDER_RW_VOID(tree, znode_get_tree(neighbor), write, 
					      h->coord->node = NULL);
				result = LOOKUP_CONT;
			} else {
				result = LOOKUP_DONE;
			}
			if (neighbor != NULL)
				zrelse(neighbor);
		}
	}
	done_lh(&lh);
	return result;
}

/* debugging aid: return symbolic name of search bias */
const char *
bias_name(lookup_bias bias /* bias to get name of */ )
{
	if (bias == FIND_EXACT)
		return "exact";
	else if (bias == FIND_MAX_NOT_MORE_THAN)
		return "left-slant";
/* 	else if( bias == RIGHT_SLANT_BIAS ) */
/* 		return "right-bias"; */
	else {
		static char buf[30];

		sprintf(buf, "unknown: %i", bias);
		return buf;
	}
}

#if REISER4_DEBUG_OUTPUT
/* debugging aid: print human readable information about @p */
void
print_coord_content(const char *prefix /* prefix to print */ ,
		    coord_t * p /* coord to print */ )
{
	reiser4_key key;

	if (p == NULL) {
		printk("%s: null\n", prefix);
		return;
	}
	if ((p->node != NULL) && znode_is_loaded(p->node) && coord_is_existing_item(p))
		printk("%s: data: %p, length: %i\n", prefix, item_body_by_coord(p), item_length_by_coord(p));
	print_znode(prefix, p->node);
	if (znode_is_loaded(p->node)) {
		item_key_by_coord(p, &key);
		print_key(prefix, &key);
		print_plugin(prefix, item_plugin_to_plugin(item_plugin_by_coord(p)));
	}
}

/* debugging aid: print human readable information about @block */
void
print_address(const char *prefix /* prefix to print */ ,
	      const reiser4_block_nr * block /* block number to print */ )
{
	printk("%s: %s\n", prefix, sprint_address(block));
}
#endif

char *
sprint_address(const reiser4_block_nr * block /* block number to print */ )
{
	static char address[30];

	if (block == NULL)
		sprintf(address, "null");
	else if (blocknr_is_fake(block))
		sprintf(address, "%llx", *block);
	else
		sprintf(address, "%llu", *block);
	return address;
}

/* release parent node during traversal */
/* Audited by: green(2002.06.15) */
static void
put_parent(cbk_handle * h /* search handle */ )
{
	assert("nikita-383", h != NULL);
	if (h->parent_lh->node != NULL) {
		longterm_unlock_znode(h->parent_lh);
	}
}

/* helper function used by coord_by_key(): release reference to parent znode
   stored in handle before processing its child. */
/* Audited by: green(2002.06.15) */
static void
hput(cbk_handle * h /* search handle */ )
{
	assert("nikita-385", h != NULL);
	done_lh(h->parent_lh);
	done_lh(h->active_lh);
}

/* Helper function used by cbk(): update delimiting keys of child node (stored
   in h->active_lh->node) using key taken from parent on the parent level. */
static void
setup_delimiting_keys(cbk_handle * h /* search handle */)
{
	znode *active;

	assert("nikita-1088", h != NULL);

	active = h->active_lh->node;
	spin_lock_dk(znode_get_tree(active));
	if (!ZF_ISSET(active, JNODE_DKSET)) {
		znode_set_ld_key(active, &h->ld_key);
		znode_set_rd_key(active, &h->rd_key);
		ZF_SET(active, JNODE_DKSET);
	}
	spin_unlock_dk(znode_get_tree(active));
}

static int
block_nr_is_correct(reiser4_block_nr * block UNUSED_ARG	/* block
							 * number
							 * to
							 * check */ ,
		    reiser4_tree * tree UNUSED_ARG	/* tree to check
							 * against */ )
{
	assert("nikita-757", block != NULL);
	assert("nikita-758", tree != NULL);

	/* check to see if it exceeds the size of the device. */
	return reiser4_blocknr_is_sane_for(tree->super, block);
}

/* check consistency of fields */
static int
sanity_check(cbk_handle * h /* search handle */ )
{
	assert("nikita-384", h != NULL);

	if (h->level < h->stop_level) {
		h->error = "Buried under leaves";
		h->result = CBK_IO_ERROR;
		return LOOKUP_DONE;
	} else if (!block_nr_is_correct(&h->block, h->tree)) {
		h->error = "bad block number";
		h->result = CBK_IO_ERROR;
		return LOOKUP_DONE;
	} else
		return 0;
}


/* check left and right delimiting keys of a znode */
void
check_dkeys(const znode *node)
{
	spin_lock_dk(current_tree);
	read_lock_tree(current_tree);

	assert("vs-1197", !keygt(&node->ld_key, &node->rd_key));

	if (ZF_ISSET(node, JNODE_LEFT_CONNECTED) && node->left != NULL)
		/* check left neighbor */
		assert("vs-1198", keyeq(&node->left->rd_key, &node->ld_key));

	if (ZF_ISSET(node, JNODE_RIGHT_CONNECTED) && node->right != NULL)
		/* check right neighbor */
		assert("vs-1199", keyeq(&node->rd_key, &node->right->ld_key));

	read_unlock_tree(current_tree);
	spin_unlock_dk(current_tree);
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
