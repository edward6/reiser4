/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* In reiser4 we use 64 bit object ids. One can easily check that
   given exponential growth in hardware speed and disk capacity,
   amount of time required to exhaust 64 bit oid space is enough to
   relax, and forget about oid reuse.

   reiser 4.0 oid allocator is then simple counter incremented on each
   oid allocation. Also counter of used oids is maintained, mainly for
   statfs(2) sake.
*/
#ifndef __REISER4_OID40_H__
#define __REISER4_OID40_H__

#include "../../forward.h"
#include "../../key.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/spinlock.h>

typedef struct {
	/* spinlock serializing accesses to this structure. */
	spinlock_t oguard;
	/* greatest oid ever allocated plus one. This is increased on each oid
	   allocation. */
	oid_t next_to_use;
	/* oids actually used. This is increased on each oid allocation, and
	   decreased on each oid release.
	   number of files, in short
	*/
	oid_t oids_in_use;
} oid40_allocator;

extern int oid40_read_allocator(reiser4_oid_allocator *, __u64 nr_files, __u64 oids);
extern __u64 oid40_free(reiser4_oid_allocator *);
extern __u64 oid40_next_oid(reiser4_oid_allocator *);
extern __u64 oid40_used(reiser4_oid_allocator *);
extern int oid40_allocate(reiser4_oid_allocator *, oid_t * result);
extern int oid40_release(reiser4_oid_allocator *, oid_t oid);
extern int oid40_reserve_allocate(reiser4_oid_allocator *);
extern int oid40_reserve_release(reiser4_oid_allocator *);

extern void oid40_print_info(const char *, reiser4_oid_allocator *);

/* __REISER4_OID40_H__ */
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
