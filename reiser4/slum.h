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
	unsigned size;
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

