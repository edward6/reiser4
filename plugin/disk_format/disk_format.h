/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

/* identifiers for disk layouts, they are also used as indexes in array of disk
 * plugins */
typedef enum { 
	/* standard reiser4 disk layout plugin id */
	LAYOUT_40_ID,
	TEST_LAYOUT_ID,
	LAST_LAYOUT_ID
} disk_layout_id;

extern reiser4_plugin layout_plugins [ LAST_LAYOUT_ID ];
