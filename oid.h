/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Object-id API.
 */

#if !defined( __REISER4_OID_H__ )
#define __REISER4_OID_H__

typedef __u64 oid_t;

/** 
 * this is structure holding data for object-id allocation algorithms we are
 * going to use.
 *
 * Object id allocator is used to keep track of used/free object ids
 * (oids)---unique identifiers attached to each reiser4 object. Reiserfs v3
 * used to maintain special "object id map" for this purpose that allowed to
 * reuse objectid once it has been freed (recycled). In reiser4 we use 64 bit
 * object ids. One can easily check that given exponential growth in hardware
 * speed and disk capacity, amount of time required to exhaust 64 bit oid
 * space is enough to relax, and forget about oid reuse.
 *
 * reiser4_oid_allocator is then simple counter incremented on each oid
 * allocation. Also counter of used oids is maintained, mainly for statfs(2)
 * sake.
 *
 */
typedef struct reiser4_oid_allocator {
	/**
	 * spinlock serializing accesses to this structure.
	 */
	spinlock_t oguard;
	/**
	 * greatest oid ever allocated plus one. This is increased on each oid
	 * allocation.
	 */
	oid_t      next_to_use;
	/**
	 * oids actually used. This is increased on each oid allocation, and
	 * decreased on each oid release.
	 */
	oid_t      oids_in_use;
} reiser4_oid_allocator_t;

extern int   reiser4_init_oid_allocator( reiser4_oid_allocator_t *map );
extern __u64 reiser4_oids_used( reiser4_oid_allocator_t *map );
extern __u64 reiser4_oids_free( reiser4_oid_allocator_t *map );
extern int   reiser4_allocate_oid( reiser4_oid_allocator_t *map, oid_t *result );
extern int   reiser4_release_oid( reiser4_oid_allocator_t *map, oid_t oid );
extern int   reiser4_oid_reserve_allocate( reiser4_oid_allocator_t *map );
extern int   reiser4_oid_reserve_release( reiser4_oid_allocator_t *map );

/* __REISER4_OID_H__ */
#endif

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
