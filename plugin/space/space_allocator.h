/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */


/* identifiers of available space allocators */
typedef enum {
	BITMAP_SPACE_ALLOCATOR_ID,
	TEST_SPACE_ALLOCATOR_ID,
	LAST_SPACE_ALLOCATOR_ID
} space_allocator_id;


/* this object is part of reiser4 private in-core super block */
struct reiser4_space_allocator {
	union {
		test_space_allocator test;
		void * generic;
	} u;
};
