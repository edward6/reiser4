/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "tree.h"
#include "tree_walk.h"
#include "super.h"

void init_ra_info(ra_info_t * rai)
{
	rai->key_to_stop = *min_key();
}

/* global formatted node readahead parameter. It can be set by mount option -o readahead:NUM:1 */
static inline int ra_adjacent_only(int flags)
{
	return flags & RA_ADJACENT_ONLY;
}

/* global formatted node readahead parameter. It can be set by mount option -o readahead:NUM:2 */
static inline int ra_all_levels(int flags)
{
	return flags & RA_ALL_LEVELS;
}

/* global formatted node readahead parameter. It can be set by mount option -o readahead:NUM:4 */
/* REMOVE THIS */
static inline int ra_continue_if_present(int flags)
{
	return flags & RA_CONTINUE_ON_PRESENT;
}

/* global formatted node readahead parameter. It can be set by mount option -o readahead:NUM:8 */
static inline int ra_get_rn_hard(int flags)
{
	return flags & RA_READ_ON_GRN;
}

/* this is used by formatted_readahead to decide whether read for right neighbor of node is to be issued. It returns 1
   if right neighbor's first key is less or equal to readahead's stop key */
static int
should_readahead_neighbor(znode *node, ra_info_t *info)
{
	return (UNDER_RW(dk, ZJNODE(node)->tree, read,
			 keyle(znode_get_rd_key(node), &info->key_to_stop)));
}

/* start read for @node and for few of its right neighbors */
void
formatted_readahead(znode *node, ra_info_t *info)
{
	ra_params_t *ra_params;
	znode *cur;
	int i;
	int grn_flags;

	if (blocknr_is_fake(znode_get_block(node)))
		/*
		 * it is possible that @node has been eflushed, and, thus, has
		 * no page. Don't do read-ahead at all.
		 */
		return;

	ra_params = get_current_super_ra_params();

	if (!ra_continue_if_present(ra_params->flags) && !znode_just_created(node))
		/* node is available immediately and global readahead flags require to not readahead in this case */
		return;

	jstartio(ZJNODE(node));

	if (!ra_all_levels(ra_params->flags) && znode_get_level(node) != LEAF_LEVEL)
		return;

	write_current_tracef("...readahead\n");

	grn_flags = (ra_get_rn_hard(ra_params->flags) ? GN_DO_READ : 0);

	/*
	 * FIXME-ZAM: GN_DO_READ seems to not work these days
	 */
	grn_flags |= GN_DO_READ;

	/* We can have locked nodes on upper tree levels, in this situation lock
	   priorities do not help to resolve deadlocks, we have to use TRY_LOCK
	   here. */
	grn_flags |= GN_TRY_LOCK;

	i = 0;
	cur = zref(node);
	while (i < ra_params->max) {
		lock_handle next_lh;

		if (!should_readahead_neighbor(cur, info))
			break;

		init_lh(&next_lh);
		if (reiser4_get_right_neighbor(&next_lh, cur, ZNODE_READ_LOCK, grn_flags))
			break;

		if (!ra_continue_if_present(ra_params->flags) && !znode_just_created(next_lh.node)) {
			/* node is available already. Do not readahead more */
			done_lh(&next_lh);
			break;
		}

		if (JF_ISSET(ZJNODE(next_lh.node), JNODE_EFLUSH)) {
			/* emergency flushed znode is encountered. That means we are low on memory. Do not readahead
			   then */
			done_lh(&next_lh);
			break;
		}

		assert("vs-1143", !blocknr_is_fake(znode_get_block(next_lh.node)));

		if (ra_adjacent_only(ra_params->flags) && *znode_get_block(next_lh.node) != *znode_get_block(cur) + 1) {
			done_lh(&next_lh);
			break;
		}

		zput(cur);
		cur = zref(next_lh.node);
		done_lh(&next_lh);
		jstartio(ZJNODE(cur));
		i ++;
	}
	zput(cur);

	write_current_tracef("...readahead exits\n");
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
