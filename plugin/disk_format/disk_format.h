/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

/* identifiers for disk layouts, they are also used as indexes in array of disk
 * plugins */
typedef enum { 
	/* standard reiser4 disk layout plugin id */
	FORMAT40_ID,
	TEST_FORMAT_ID,
	LAST_FORMAT_ID
} disk_format_id;

