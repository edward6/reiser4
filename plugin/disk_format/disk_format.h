/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

/* identifiers for disk layouts, they are also used as indexes in array of disk
 * plugins */
typedef enum { 
	/* standard reiser4 disk layout plugin id */
	FORMAT_40_ID,
	TEST_FORMAT_ID,
	LAST_FORMAT_ID
} disk_format_id;

extern reiser4_plugin format_plugins [ LAST_FORMAT_ID ];
