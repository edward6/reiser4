/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "tree.h"
#include "tree_walk.h"
#include "super.h"

/*
 * there are three "types" of readahead:
 * 1. readahead for unformatted nodes
 * 2. readahead for directories: it is implemented via zload
 * 3. 
 */

void
readdir_readahead(znode *node, ra_info_t *info)
{
	ra_params_t *ra_params;
	znode *next;
	int i;

	if (blocknr_is_fake(znode_get_block(node)))
		/*
		 * it is possible that @node has been eflushed, and, thus, has
		 * no page. Don't do read-ahead at all.
		 */
		return;

	ra_params = get_current_super_ra_params();

	if (ra_params->leaves_only && znode_get_level(node) != LEAF_LEVEL)
		/*
		 * FIXME-VS: for now only for leaf 
		 */
		return;

	jstartio(ZJNODE(node));
	i = 1;

	next = zref(node);
	while (1) {
		lock_handle lh;
		int grn_flags;

		if (i >= ra_params->max)
			break;
		if (UNDER_SPIN(dk, ZJNODE(next)->tree, (get_key_locality(znode_get_rd_key(next)) != info->u.readdir.oid ||
							get_key_type(znode_get_rd_key(next)) >= KEY_BODY_MINOR)))
			break;
		init_lh(&lh);

		grn_flags = GN_DO_READ; /* read all necessary internals */
		if (reiser4_get_right_neighbor(&lh, next, ZNODE_READ_LOCK, grn_flags))
			break;
		if (blocknr_is_fake(znode_get_block(lh.node))) {
			done_lh(&lh);
			break;
		}
		if (ra_params->adjacent_only && *znode_get_block(lh.node) != *znode_get_block(next) + 1) {
			done_lh(&lh);
			break;
		}

		zput(next);
		next = zref(lh.node);
		done_lh(&lh);
		jstartio(ZJNODE(next));
		i ++;
	}
	zput(next);
}

