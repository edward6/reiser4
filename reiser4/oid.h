/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Object-id API.
 */

#if !defined( __REISER4_OID_H__ )
#define __REISER4_OID_H__

typedef __u64 oid_t;

/** this is structure holding data for object-id allocation algorithms
    we are going to use. */
typedef struct reiser4_oid_allocator {
	spinlock_t oguard;
	oid_t      next_to_use;
} reiser4_oid_allocator;

extern int   reiser4_init_oid_allocator( reiser4_oid_allocator *map );
extern oid_t reiser4_maximal_oid( reiser4_oid_allocator *map );
extern oid_t reiser4_minimal_oid( reiser4_oid_allocator *map );
extern __u64 reiser4_oids_used( reiser4_oid_allocator *map );
extern int   reiser4_allocate_oid( reiser4_oid_allocator *map, oid_t *result );
extern int   reiser4_release_oid( reiser4_oid_allocator *map, oid_t oid );
extern int   reiser4_oid_reserve_allocate( reiser4_oid_allocator *map );
extern int   reiser4_oid_reserve_release( reiser4_oid_allocator *map );

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
