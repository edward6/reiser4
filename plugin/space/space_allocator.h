/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __SPACE_ALLOCATOR_H__
#define __SPACE_ALLOCATOR_H__

#include "../../forward.h"
#include "test.h"

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
/* ZAM-FIXME-HANS: comment this */
		void *generic;
	} u;
};

/* __SPACE_ALLOCATOR_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
