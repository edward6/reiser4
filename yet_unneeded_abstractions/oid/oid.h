/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __REISER4_OID_H__
#define __REISER4_OID_H__

#include "../../forward.h"
#include "../../key.h"
#include "oid40.h"

#include <linux/fs.h>		/* for struct super_block */

extern __u64 oid_used(void);
extern __u64 oid_next(void);

extern int oid_allocate(oid_t *);
extern int oid_release(oid_t);

extern void oid_count_allocated(void);
extern void oid_count_released(void);

extern int oid_init_allocator(const struct super_block *, __u64, __u64);
#if REISER4_DEBUG_OUTPUT
extern void oid_print_allocator(const char *prefix, const struct super_block *);
#else
#define oid_print_allocator(p,s) noop
#endif

/* identifiers of available objectid managers */
typedef enum {
	/* default for reiser 4.0 oid manager id */
	OID40_ALLOCATOR_ID,
	LAST_OID_ALLOCATOR_ID
} oid_allocator_id;

/* this object is part of reiser4 private in-core super block */
struct reiser4_oid_allocator {
	union {
		oid40_allocator oid40;
	} u;
};

/* __REISER4_OID_H__ */
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
