/* Routines and macros to:

   get_left_neighbor()

   get_right_neighbor()

   get_parent()

   get_first_child()

   get_last_child()

   various routines to walk the whole tree and do things to it like
   repack it, or move it to tertiary storage.  Please make them as
   generic as is reasonable.

 */

#include "reiser4.h"

/* These macros are used internally in tree_walk.c in attempt to make
 * lock_neighbor() code usable to build lock_parent(), lock_right_neighbor,
 * lock_left_neighbor */
#define GET_NODE_BY_DIR(node, dir) (*(znode**)(((unsigned long)(node)) + (dir)))
#define FIELD_OFFSET(name)  ((int)(&((znode*)0)-> ## name ))
#define PARENT_DIR FIELD_OFFSET(ptr_in_parent_hint.node)
#define LEFT_DIR   FIELD_OFFSET(left)
#define RIGHT_DIR  FIELD_OFFSET(right)

/** This is the generic procedure to get and lock `generic' neighbor (left or right
    neighbor or parent). It implements common algorithm for all cases of
    getting lock on neighbor node, only znode structure field is different in
    each case. This is parameterized by dir argument, which ?word missing here? byte offset for
    neighbor pointer field within znode structure. This function should be
    called with the tree lock held */
/* Audited by: umka (2002.06.12) */
static int lock_neighbor (lock_handle * result /* resulting lock
							* handle*/, 
			  znode * node /* node to lock */,
			  int dir, /* FIXME: I have no understanding of why this is named this, and it seems like it should be more strongly typed. -Hans */
			           /* FIXME: I don't have strong objections to the lack of strong types, but I agree that the
				    * name is very bad.  Reame "dir" to "field_offset", perhaps?  You could do it w/
				    * strong types by passing a function pointer, or by passing an enumerated type
				    * value, then using a switch() to select the field based on that. -josh */
			  znode_lock_mode mode,
			  znode_lock_request req,
			  int only_connected_p /* if this is true, neighbor is
						* only returned when it is
						* connected. If neighbor is
						* unconnected, -ENAVAIL is
						* returned. Normal users
						* should pass 1 here. Only
						* during carry we want to
						* access still unconnected
						* neighbors. */ )
{
	reiser4_tree * tree = current_tree;
	znode * neighbor;
	int ret;
	
	assert("umka-236", node != NULL);
	assert("umka-237", tree != NULL);
	
	reiser4_stat_znode_add(lock_neighbor);
	/* get neighbor's address by using of sibling link, quit while loop
	 * (and return) if link is not available. */
	while (1) {
		reiser4_stat_znode_add(lock_neighbor_iteration);
		neighbor = GET_NODE_BY_DIR(node, dir);
		if (neighbor == NULL || 
		    (only_connected_p && !znode_is_connected(neighbor)))
			return -ENAVAIL;

		/* protect it from deletion. */
		zref(neighbor);
		spin_unlock_tree(tree);

		ret = longterm_lock_znode(result, neighbor, mode, req);

		/* The lock handle obtains its own reference, release the one from above. */
		zput(neighbor);

		spin_lock_tree(tree);

		/* restart if node we got reference to is being
		 * invalidated. we should not get reference to this node
		 * again.*/
		if (ret == -EINVAL)
			continue;
		if (ret)
			return ret;

		/* check is neighbor link still points to just locked znode;
		 * the link could be changed while process slept. */
		if (neighbor == GET_NODE_BY_DIR(node, dir))
			return 0;

		/* znode was locked by mistake; unlock it and restart locking
		 * process from beginning. */
		spin_unlock_tree(tree);
		longterm_unlock_znode(result);
		spin_lock_tree(tree);
	}
}

/* description is in tree_walk.h */
/* Audited by: umka (2002.06.12) */
int reiser4_get_parent (lock_handle * result /* resulting lock
						      * handle */, 
			znode * node /* child node */,
			znode_lock_mode mode /* type of lock: read or write */, 
			int only_connected_p /* if this is true, parent is
					      * only returned when it is
					      * connected. If parent is
					      * unconnected, -ENAVAIL is
					      * returned. Normal users should
					      * pass 1 here. Only during carry
					      * we want to access still
					      * unconnected parents. */ )
{
	reiser4_tree * tree = current_tree;
	int ret;
	
	assert("umka-238", tree != NULL);
	
	spin_lock_tree(tree);
	ret = lock_neighbor(result, node, PARENT_DIR, mode, ZNODE_LOCK_HIPRI, 
			    only_connected_p);
	spin_unlock_tree(tree);

	return ret;
}

/** 
 * wrapper function to lock right or left neighbor depending on GN_GO_LEFT
 * bit in @flags parameter 
 */
/* Audited by: umka (2002.06.12) */
static inline
int lock_side_neighbor( lock_handle * result,
			znode * node,
			znode_lock_mode mode, int flags)
{
	int ret;
	int dir;
	znode_lock_request req;

	if (flags & GN_GO_LEFT) {
		dir = LEFT_DIR;
		req = ZNODE_LOCK_LOPRI;
	} else {
		dir = RIGHT_DIR;
		req = ZNODE_LOCK_HIPRI;
	}

	if (flags & GN_TRY_LOCK) req |= ZNODE_LOCK_NONBLOCK;

	ret =  lock_neighbor(result, node, dir, mode, req, 1);

	if (ret == -ENAVAIL)	/* if we walk left or right -ENAVAIL does not
				 * guarantee that neighbor is absent in the
				 * tree; in this case we return -ENOENT --
				 * means neighbor at least not found in
				 * cache */
		return -ENOENT;

	return ret;
}

/*
 * znode sibling pointers maintaining.
 */

/* After getting new znode we have to establish sibling pointers. Znode web
 * maintaining overhead is in additional hash table searches for left and
 * right neighbors (and worse locking scheme in case close neighbors do not
 * share same parent).
 *
 * We can reduce that overhead by introducing of znode `connected' states. For
 * simpler locking and simpler implementation znodes have two connection state
 * bits: left-connected and right connected. We never do hash table search for
 * neighbor from connected side even corresponded pointer is null. This way we
 * only do hash searches when new znode is allocated and should be connected
 * to znode web dynamic structure.
 *
 * Locking in left direction (required for finding of parent of left neighbor)
 * can fail and cause whole lookup process to restart. It means lookup process
 * may leave znode in unconnected state. These znodes should not be used,
 * loaded while they are is such state. */

/* adjusting of sibling pointers and `connected' states for two
 * neighbors; works if one neighbor is NULL (was not found). */

/*
 * FIXME-VS: this is unstatic-ed to use in tree.c in prepare_twig_cut
 */
/* Audited by: umka (2002.06.12) */
/*static*/ /*inline*/ void link_left_and_right (znode * left, znode * right)
{
	assert("umka-239", left != NULL);
	assert("umka-240", right != NULL);
	
	if (left != NULL) {
		left->right = right;
		ZF_SET (left, ZNODE_RIGHT_CONNECTED);
	}

	if (right != NULL) {
		right->left = left;
		ZF_SET (right, ZNODE_LEFT_CONNECTED);
	}
}

/* Audited by: umka (2002.06.12) */
static void link_znodes (znode * first, znode * second, int to_left)
{
	assert("umka-241", first != NULL);
	assert("umka-242", second != NULL);
	
	if (to_left) link_left_and_right(second, first);
	else         link_left_and_right(first, second);
}

/* getting of next (to left or to right, depend on gn_to_left bit in flags)
 * coord's unit position in horizontal direction, even across node
 * boundary. Should be called under tree lock, it protects nonexistence of
 * sibling link on parent level, if lock_side_neighbor() fails with
 * -ENOENT. */
/* Audited by: umka (2002.06.12) */
static int far_next_coord (new_coord * coord, lock_handle * handle, int flags)
{
	int ret;
	znode *node;
	
	assert("umka-243", coord != NULL);
	assert("umka-244", handle != NULL);
	
	handle->owner = NULL;	/* mark lock handle as unused */

	ret = (flags & GN_GO_LEFT) ? ncoord_prev_unit(coord) : ncoord_next_unit(coord);
	if (!ret) return 0;

	ret = lock_side_neighbor(handle, coord->node, ZNODE_READ_LOCK, flags);

	if (ret) return ret;

	spin_unlock_tree(current_tree);

	ncoord_init_zero(coord);

	node = handle->node;

	/* corresponded zrelse() should be called by the clients of
	 * far_next_coords(), in place when this node gets unlocked. */
	ret = zload(handle->node);

	if (ret) {
		longterm_unlock_znode(handle);
		spin_lock_tree(current_tree);
		return ret;
	}

	if (flags & GN_GO_LEFT) ncoord_init_last_unit(coord, node);
	else                    ncoord_init_first_unit(coord, node);

	spin_lock_tree(current_tree);
	return 0;
}

/** Very significant function which performs a step in horizontal direction
 * when sibling pointer is not available.  Actually, it is only function which
 * does it. */
/* Audited by: umka (2002.06.12) */
static int renew_sibling_link (new_coord * coord, lock_handle * handle,
			       znode * child, tree_level level, int flags, int *nr_locked)
{
	int ret;
	reiser4_block_nr da;
	/* parent of the neighbor node; we set it to parent until not sharing
	 * of one parent between child and neighbor node is detected */
	znode * side_parent = coord->node;
	reiser4_tree *tree = current_tree;
	znode * neighbor = NULL;
	
	assert("umka-245", coord != NULL);
	assert("umka-246", handle != NULL);
	assert("umka-247", child != NULL);
	
	spin_lock_tree(tree);

	ret = far_next_coord(coord, handle, flags);

	if (ret) {
		if (ret != -ENOENT) return ret;
	} else {
		item_plugin * iplug;
		spin_unlock_tree(tree);

		/* does coord object points to internal item? We do not
		 * support sibling pointers between znode for formatted and
		 * unformatted nodes and return -ENAVAIL in that case. */
		/* FIXME-NIKITA nikita: can child_znode() be used here? */
		iplug = item_plugin_by_coord(coord);
		if (!item_is_internal (coord)) {
			if (handle->owner != NULL) {
				longterm_unlock_znode(handle);
			}
			link_znodes(child, NULL, flags & GN_GO_LEFT);
			/* we know there can't be formatted neighbor*/
			return -ENAVAIL;
		}

		if (handle->owner != NULL) {
			(*nr_locked) ++;
			side_parent = handle->node;
		}

		iplug -> s.internal.down_link(coord, NULL, &da);

		if (flags & GN_NO_ALLOC) {
			neighbor = zlook(tree, &da);
		} else {
			neighbor = zget(tree, &da, side_parent, level, GFP_KERNEL);
		}

		if (IS_ERR(neighbor)) {
			/* restore the state we had before entering
			 * renew_sibling() except coord object is no more
			 * valid. */
			spin_unlock_tree(tree);
			ret = PTR_ERR(neighbor);
			if (handle->owner != NULL) {
				zrelse(handle->node);
				longterm_unlock_znode(handle);
				(*nr_locked) --;
			}

			return ret;
		}

		if(neighbor) {
			/* update delimiting keys */
			spin_lock_dk(tree);
			find_child_delimiting_keys(coord->node, coord,
						   znode_get_ld_key(neighbor),
						   znode_get_rd_key(neighbor));
			spin_unlock_dk(tree);
		}

		spin_lock_tree(tree);
	}

	link_znodes(child, neighbor, flags & GN_GO_LEFT);

	/* if GN_NO_ALLOC isn't set we keep reference to neighbor znode */
	if (neighbor != NULL && (flags & GN_NO_ALLOC))
		zput(neighbor);

	spin_unlock_tree(tree);

	return ret;
}

/*
 *
 * This function is for establishing of one side relation.
 */
/* Audited by: umka (2002.06.12) */
static int connect_one_side (new_coord * coord, znode * node, int flags)
{
	new_coord local;
	lock_handle handle;
	int nr_locked;
	int ret;

	assert("umka-248", coord != NULL);
	assert("umka-249", node != NULL);
	
	ncoord_dup_nocheck(&local, coord);
	init_lh(&handle);

	ret = renew_sibling_link(&local, &handle, node, znode_get_level( node ),
				 flags | GN_NO_ALLOC, &nr_locked);


	if (handle.owner != NULL) {
		/* complementary operations for zload() and lock() in far_next_coord() */
		zrelse(handle.node);
		longterm_unlock_znode(&handle);
	}

	/* we catch error codes which are not interesting for us because we
	 * run renew_sibling_link() only for znode connection. */
	if (ret == -ENOENT || ret == -ENAVAIL) 
		return 0;

	return ret;
}

/* if node is not in `connected' state, performs hash searches for left and
 * right neighbor nodes and establishes horizontal sibling links */
/* Audited by: umka (2002.06.12) */
int connect_znode (new_coord * coord, znode * node)
{
	reiser4_tree * tree = current_tree;
	int ret = 0;

	assert("zam-330", coord != NULL);
	assert("zam-331", node != NULL);
	assert("zam-332", coord->node != NULL);

	/* it is trivial to `connect' root znode because it can't have
	 * neighbors */
	if (znode_above_root(coord->node)) {
		node->left = NULL;
		node->right = NULL;
		ZF_SET (node, ZNODE_LEFT_CONNECTED);
		ZF_SET (node, ZNODE_RIGHT_CONNECTED);
		return 0;
	}

	/* load parent node */
	ret = zload (coord -> node);

	if (ret != 0) return ret; 

	/* protect `connected' state check by tree_lock */
	spin_lock_tree(tree);

	if (!znode_is_right_connected(node)) {
		spin_unlock_tree(tree);

		ret = connect_one_side(coord, node, GN_NO_ALLOC);
		if (ret) goto zrelse_and_ret;

		spin_lock_tree(tree);
	}

	/* connect to right neighbor */
	ret = znode_is_left_connected(node);

	spin_unlock_tree(tree);

	if (!ret) {
		ret = connect_one_side(coord, node, GN_NO_ALLOC | GN_GO_LEFT);
	}

 zrelse_and_ret:
	zrelse (coord -> node);

	return ret;
}

/* this function is like renew_sibling_link() but allocates neighbor node if
 * it doesn't exist and `connects' it. It may require making two steps in
 * horizontal direction, first one for neighbor node finding/allocation,
 * second one is for finding neighbor of neighbor to connect freshly allocated
 * znode. */
/* Audited by: umka (2002.06.12) */
static int renew_neighbor (new_coord * coord, znode * node, tree_level level, int flags)
{
	new_coord local;
	lock_handle empty[2];
	reiser4_tree * tree = current_tree;
	znode * neighbor = NULL;
	int nr_locked = 0;
	int ret;

	assert("umka-250", coord != NULL);
	assert("umka-251", node != NULL);
	
	/* 
	 * umka (2002.06.12) 
	 * Here probably should be a check for given "level" validness.
	 * Something like assert("xxx-yyy", level < REAL_MAX_ZTREE_HEIGHT); 
	 * */
	
	ncoord_dup(&local, coord);

	ret = renew_sibling_link(&local, &empty[0], node, level, flags & ~GN_NO_ALLOC, &nr_locked);
	if (ret) goto out;

	/* tree lock is not needed here because we keep parent node(s) locked
	 * and reference to neighbor znode incremented */
	neighbor = (flags & GN_GO_LEFT) ? node->left : node->right;

	spin_lock_tree(tree);
	ret = znode_is_connected(neighbor);
	spin_unlock_tree(tree);

	if (ret) {
		ret = 0;
		goto out;
	}

	ret = renew_sibling_link(&local, &empty[nr_locked], neighbor,
				 level, flags | GN_NO_ALLOC, &nr_locked);
	/* second renew_sibling_link() call is used for znode connection only,
	 * so we can live with these errors */
	if (-ENOENT == ret || -ENAVAIL == ret) 
		ret = 0;

 out:

	for (-- nr_locked ; nr_locked >= 0 ; -- nr_locked) {
		zrelse(empty[nr_locked].node);
		longterm_unlock_znode(&empty[nr_locked]);
	}

	if (neighbor != NULL) zput(neighbor);

	return ret;
}

/*
 * reiser4_get_neighbor() locks node's neighbor (left or right one, depends on
 * given parameter) using sibling link to it. If sibling link is not available
 * (i.e. neighbor znode is not in cache) and flags allow read blocks, we go
 * one level up for information about neighbor's disk address. We lock node's
 * parent, if it is common parent for both 'node' and its neighbor, neighbor's
 * disk address is in next (to left or to right) down link from link that
 * points to original node. If not, we need to lock parent's neighbor, read
 * its content and take first(last) downlink with neighbor's disk address.
 * That locking could be done by using sibling link and lock_neighbor()
 * function, if sibling link exists. In another case we have to go level up
 * again until we find common parent or valid sibling link. Then go down
 * allocating/connecting/locking/reading nodes until neigbor of first one is
 * locked.
 */

/* Audited by: umka (2002.06.12) */
int reiser4_get_neighbor (lock_handle * neighbor /* lock handle that
							  * points to origin
							  * node we go to
							  * left/right/upward
							  * from */,
			  znode * node,
			  znode_lock_mode lock_mode /* lock mode {LM_READ,
						     * LM_WRITE}.*/, 
			  int flags /* logical OR of {GN_*} (see description
				     * above) subset. */ )
{
	reiser4_tree * tree = current_tree;
	lock_handle path[REAL_MAX_ZTREE_HEIGHT];

	new_coord coord;

	tree_level base_level;
	tree_level h = 0;
	int ret;
	
	assert("umka-252", tree != NULL);
	assert("umka-253", neighbor != NULL);
	assert("umka-254", node != NULL);
	
	base_level = znode_get_level( node );
	
	ncoord_init_zero(&coord);

 again:
	/* first, we try to use simple lock_neighbor() which requires sibling
	 * link existence */
	spin_lock_tree(tree);
	ret = lock_side_neighbor(neighbor, node, lock_mode, flags);
	spin_unlock_tree(tree);

	if (!ret) {
		/* load znode content if it was specified */
		if (flags & GN_LOAD_NEIGHBOR) {
			ret = zload (node);
			if (ret) longterm_unlock_znode(neighbor);
		}
		return ret;
	}

	/* only -ENOENT means we may look upward and try to connect
	 * @node with its neighbor (if @flags allow us to do it) */
	if (ret != -ENOENT || !(flags & GN_DO_READ)) return ret;

	/* before establishing of sibling link we lock parent node; it is
	 * required by renew_neighbor() to work.  */
	init_lh(&path[0]);
	ret = reiser4_get_parent(&path[0], node, ZNODE_READ_LOCK, 1);
	if (ret) return ret;
	if (znode_above_root(path[0].node)) {
		longterm_unlock_znode(&path[0]);
		return -ENAVAIL;
	}

	while (1) {
		znode * child  = (h == 0) ? node : path[h - 1].node;
		znode * parent = path[h].node;

		reiser4_stat_add_at_level(h + LEAF_LEVEL, sibling_search);
		ret = zload(parent);
		if (ret) break;

		ret = find_child_ptr(parent, child, &coord);

		if (ret) {
			zrelse(parent);
			break;
		}

		/* try to establish missing sibling link */
		ret = renew_neighbor(&coord, child, h + base_level, flags);

		zrelse(parent);

		switch (ret) {
		    case 0:
			    /* unlocking of parent znode prevents simple
			     * deadlock situation */
			    done_lh(&path[h]);

			    /* depend on tree level we stay on we repeat first
			     * locking attempt ...  */
			    if (h == 0) goto again;

			    /* ... or repeat establishing of sibling link at
			     * one level below. */
			    -- h;
			    break;

		    case -ENOENT:
			    /* sibling link is not available -- we go
			     * upward. */
			    init_lh(&path[h + 1]);
			    ret = reiser4_get_parent(&path[h + 1], parent, ZNODE_READ_LOCK, 1);
			    if (ret) goto fail;
			    ++ h;
			    if (znode_above_root(path[h].node)) {
				    ret = -ENAVAIL;
				    goto fail;
			    }
			    break;

		    case -EDEADLK:
			    /* there was lock request from hi-pri locker. if
			       it is possible we unlock last parent node and
			       re-lock it again. */
			    while (check_deadlock()) {
				    if (h == 0) goto fail;

				    done_lh(&path[--h]);
			    }

			    break;

		    default:	/* other errors. */
			    goto fail;
		}
	}
 fail:
	/* unlock path */
	do {
		longterm_unlock_znode(&path[h]);
		-- h;
	} while (h + 1 != 0);

	return ret;
}

/** remove node from sibling list */
/* Audited by: umka (2002.06.12) */
void sibling_list_remove (znode * node)
{
	assert("umka-255", node != NULL);
	
	if (!znode_is_connected(node))
		return;

	if (node->right != NULL) {
		assert("zam-322", znode_is_left_connected(node->right));
		node->right->left = node->left;
	}
	if (node->left != NULL) {
		assert("zam-323", znode_is_right_connected(node->left));
		node->left->right = node->right;
	}
	/*
	 * FIXME-NIKITA ordering?
	 */
	ZF_CLR (node, ZNODE_LEFT_CONNECTED);
	ZF_CLR (node, ZNODE_RIGHT_CONNECTED);
}

/* Audited by: umka (2002.06.12) */
void sibling_list_insert_nolock (znode *new, znode *before)
{
	assert("zam-334", new != NULL);

	if (before != NULL) {
		assert("zam-333", znode_is_connected(before));
		new->right = before->right;
		new->left = before;
		if (before->right != NULL)
			before->right->left = new;
		before->right = new;
	} else {
		new->right = NULL;
		new->left = NULL;
	}
	/*
	 * FIXME-NIKITA ordering?
	 */
	ZF_SET (new, ZNODE_LEFT_CONNECTED);
	ZF_SET (new, ZNODE_RIGHT_CONNECTED);
}

/** Insert new node into sibling list. Regular balancing inserts new node
 * after (at right side) existing and locked node (@before), except one case
 * of adding new tree root node. @before should be NULL in that case. */
/* Audited by: umka (2002.06.12) */
void sibling_list_insert (znode *new, znode *before)
{
	assert("umka-256", new != NULL);
	assert("umka-257", current_tree != NULL);
	
	spin_lock_tree(current_tree);
	sibling_list_insert_nolock(new, before);
	spin_unlock_tree(current_tree);
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
