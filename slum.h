/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Slum data-types. See slum_track.c for description.
 */

#if !defined( __SLUM_H__ )
#define __SLUM_H__

#define SLUM_SCAN_MAXNODES 10

/* The slum_scan data structure maintains the state of an in-progress slum scan. */
struct slum_scan {
	/* The number of nodes currently in the slum. */
	unsigned  size;

	/* True if some condition stops the search (e.g., we found a clean
	 * node before reaching max size). */
	int       stop;

	/* The current scan position. */
	jnode    *node;

	/* The current scan parent. */
	znode    *parent;

	/* Locks for the above two nodes. */
	reiser4_lock_handle parent_lock;

	/* The current coord of node in parent. */
	tree_coord coord;

	/* The current scan atom. */
	txn_atom *atom;
};

extern int flush_jnode_slum ( jnode *node );

/* __SLUM_H__ */
#endif

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */

