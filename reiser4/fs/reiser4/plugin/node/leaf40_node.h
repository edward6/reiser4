/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */


typedef d16 node_offset_40;

/** format of node header for 40 node layouts. Keep bloat out of this struct.  */
typedef struct node_header_40 {
	/** identifier of node plugin */
	d16            plugin_id;
	/** free space in node measured in bytes */
	/* it might make some of the code simpler to store this just
	   before the last item header, but then free_space finding
	   code would be more complex.... A thought.... */
	node_offset_40    free_space; /**/
	/** offset to start of free space in node */
	node_offset_40    free_space_start;
/* 0 is leaf level, 1 is twig level, root is the numerically largest
   level */
	d8	       level;
/* commented out uncommented field */
 	d32	       magic;
	/** number of items --- is it too large? Probably.  */
	d16           num_items;
	/** node flags to be used by fsck (reiser4ck or reiser4fsck?)
	    and repacker */
/* commented out because it was uncommented */
/* 	char           flags; */
	/** for reiser4_fsck.  When information about what is a free
	    block is corrupted, and we try to recover everything even
	    if marked as freed, then old versions of data may
	    duplicate newer versions, and this field allows us to
	    restore the newer version.  Also useful for when users
	    delete the wrong files and send us desperate emails
	    offering $25 for them back.  */
	d32            flush_time;
} node_header_40;

/* item headers are not standard across all node layouts, pass
 * pos_in_node to functions instead */
typedef struct item_header_40 {
	/** key of item */
	/*  0 */ reiser4_key  key;
	/** offset from start of a node measured in 8-byte chunks */
	/* 24 */ node_offset_40  offset;
	/* 26 */ d16          plugin_id;
	/* 28 */ /* four bytes will be lost due to padding anyway. Think
		    how can they be used. */
  /* why pad?  Because of cachelines?  I need more convincing.... */
} item_header_40;


/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
