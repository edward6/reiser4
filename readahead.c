/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "tree.h"
#include "tree_walk.h"

/*
 * FIXME-VS: no sure about this
 */
#define MAX_READDIR_RA (256)

void
readdir_readahead(znode *node, ra_info_t *info)
{
	znode *next;
	int i;

	if (znode_get_level(node) != LEAF_LEVEL)
		/*
		 * FIXME-VS: for now only for leaf 
		 */
		return;

	i = 0;
	jstartio(ZJNODE(node));
	next = zref(node);
	while (1) {
		lock_handle lh;
		int grn_flags;

		if (++i >= MAX_READDIR_RA)
			break;
		if (UNDER_SPIN(dk, ZJNODE(next)->tree, (get_key_locality(znode_get_rd_key(next)) != info->u.readdir.oid ||
							get_key_type(znode_get_rd_key(next)) >= KEY_BODY_MINOR)))
			break;
		init_lh(&lh);

		grn_flags = GN_DO_READ; /* read all necessary internals */
		if (reiser4_get_right_neighbor(&lh, next, ZNODE_READ_LOCK, grn_flags))
			break;
		zput(next);
		next = zref(lh.node);
		done_lh(&lh);
		jstartio(ZJNODE(next));
	}
	zput(next);
}

