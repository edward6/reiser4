/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* DECLARATIONS: */

#include "plugin/item/ctail.h" /* for ctail scan/squeeze info */

typedef enum {
	EXISTING_ITEM = 0,
	NEED_CREATE   = 1
} flush_scan_item_stat_t;

/* The flush_scan data structure maintains the state of an in-progress flush-scan on a
   single level of the tree.  A flush-scan is used for counting the number of adjacent
   nodes to flush, which is used to determine whether we should relocate, and it is also
   used to find a starting point for flush.  A flush-scan object can scan in both right
   and left directions via the scan_left() and scan_right() interfaces.  The
   right- and left-variations are similar but perform different functions.  When scanning
   left we (optionally perform rapid scanning and then) longterm-lock the endpoint node.
   When scanning right we are simply counting the number of adjacent, dirty nodes. */
struct flush_scan {

	/* The current number of nodes scanned on this level. */
	unsigned count;

	/* There may be a maximum number of nodes for a scan on any single level.  When
	   going leftward, max_count is determined by FLUSH_SCAN_MAXNODES (see reiser4.h) */
	unsigned max_count;

	/* Direction: Set to one of the sideof enumeration: { LEFT_SIDE, RIGHT_SIDE }. */
	sideof direction;

	/* Initially @stop is set to false then set true once some condition stops the
	   search (e.g., we found a clean node before reaching max_count or we found a
	   node belonging to another atom). */
	int stop;

	/* The current scan position.  If @node is non-NULL then its reference count has
	   been incremented to reflect this reference. */
	jnode *node;
	
	/* item specific data, can be used when child is not linked yet by any items on
	 * parent level (e.g. when we create new cryptcompress object */
	flush_scan_item_stat_t istat;

	/* A handle for zload/zrelse of current scan position node. */
	load_count node_load;

	/* During left-scan, if the final position (a.k.a. endpoint node) is formatted the
	   node is locked using this lock handle.  The endpoint needs to be locked for
	   transfer to the flush_position object after scanning finishes. */
	lock_handle node_lock;

	/* When the position is unformatted, its parent, coordinate, and parent
	   zload/zrelse handle. */
	lock_handle parent_lock;
	coord_t parent_coord;
	load_count parent_load;

	/* The block allocator preceder hint.  Sometimes flush_scan determines what the
	   preceder is and if so it sets it here, after which it is copied into the
	   flush_position.  Otherwise, the preceder is computed later. */
	reiser4_block_nr preceder_blk;
};

static inline flush_scan_item_stat_t
get_flush_scan_istat(flush_scan * scan)
{
	return scan->istat;
}

static inline void
set_flush_scan_istat(flush_scan * scan, flush_scan_item_stat_t istat)
{
	scan->istat = istat;
}

typedef union flush_squeeze_item_data {
	ctail_squeeze_info_t ctail_info;
} flush_squeeze_item_data_t;

typedef enum flush_position_state {
	POS_INVALID,		/* Invalid or stopped pos, do not continue slum
				 * processing */
	POS_ON_LEAF,		/* pos points to already prepped, locked formatted node at
				 * leaf level */
	POS_ON_EPOINT,		/* pos keeps a lock on twig level, "coord" field is used
				 * to traverse unformatted nodes */
	POS_TO_LEAF,		/* pos is being moved to leaf level */
	POS_TO_TWIG,		/* pos is being moved to twig level */
	POS_END_OF_TWIG,	/* special case of POS_ON_TWIG, when coord is after
				 * rightmost unit of the current twig */
	POS_ON_INTERNAL		/* same as POS_ON_LEAF, but points to internal node */

} flushpos_state_t;



/* An encapsulation of the current flush point and all the parameters that are passed
   through the entire squeeze-and-allocate stage of the flush routine.  A single
   flush_position object is constructed after left- and right-scanning finishes. */
struct flush_position {
	flushpos_state_t state;

	coord_t coord;		/* coord to traverse unformatted nodes */
	lock_handle lock;	/* current lock we hold */
	load_count load;	/* load status for current locked formatted node  */

	jnode * child;          /* for passing a reference to unformatted child
				 * across pos state changes */

	reiser4_blocknr_hint preceder;	/* The flush 'hint' state. */
	int leaf_relocate;	/* True if enough leaf-level nodes were
				 * found to suggest a relocate policy. */
	long *nr_to_flush;	/* If called under memory pressure,
				 * indicates how many nodes the VM asked to flush. */
	int alloc_cnt;		/* The number of nodes allocated during squeeze and allococate. */
	int prep_or_free_cnt;	/* The number of nodes prepared for write (allocate) or squeezed and freed. */
	flush_queue_t *fq;
	long *nr_written;	/* number of nodes submitted to disk */
	int flags;		/* a copy of jnode_flush flags argument */

	znode * prev_twig;	/* previous parent pointer value, used to catch
				 * processing of new twig node */
	flush_squeeze_item_data_t * idata; /* squeeze item data handle */

	unsigned long pos_in_unit; /* for extents only. Position
				      within an extent unit of first
				      jnode of slum */
};

/* used in extent.c */
int scan_set_current(flush_scan * scan, jnode * node, unsigned add_size, const coord_t * parent);
int scan_finished(flush_scan * scan);
int scanning_left(flush_scan * scan);
int scan_goto(flush_scan * scan, jnode * tonode);

int init_fqs(void);
int done_fqs(void);

#if REISER4_TRACE
const char *jnode_tostring(jnode * node);
#else
#define jnode_tostring(n) ""
#endif

#if REISER4_DEBUG
#define check_preceder(blk) \
assert("nikita-2588", blk < reiser4_block_count(reiser4_get_current_sb()));

#else
#define check_preceder( b ) noop
#endif
