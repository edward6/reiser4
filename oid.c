/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Object-id manipulations.
 */

#include "reiser4.h"

/**
 * Maximal possible object id.
 */
static const oid_t ABSOLUTE_MAX_OID = ( oid_t ) ~0;

/**
 * Minimal possible object id.
 */
static const oid_t ABSOLUTE_MIN_OID = ( oid_t )  0;

/** reserve 65k oids for internal use on both ends of oid-space.
    There is no reason to be greedy here. */
#define OIDS_RESERVED  ( 1 << 16 )

/**
 * Initialise object id allocator
 */
int reiser4_init_oid_allocator( reiser4_oid_allocator *map )
{
	assert( "nikita-1168", map != NULL );
	
	spin_lock_init( &map -> oguard );
	map -> next_to_use = reiser4_minimal_oid( map );
	return 0;
}

/** helper function: spin lock allocator */
static void lock( reiser4_oid_allocator *map )
{
	assert( "nikita-1648", map != NULL );
	spin_lock( &map -> oguard );
}

/** helper function: spin unlock allocator */
static void unlock( reiser4_oid_allocator *map )
{
	assert( "nikita-1660", map != NULL );
	spin_unlock( &map -> oguard );
}

/** maximal oid that can ever be assigned for user-visible object.
    System can reserve some amount of oids for internal use. */
oid_t reiser4_maximal_oid( reiser4_oid_allocator *map UNUSED_ARG )
{
	assert( "nikita-442", map != NULL );
	return ABSOLUTE_MAX_OID - OIDS_RESERVED;
}

/** minimal oid that can ever be assigned for user-visible object */
oid_t reiser4_minimal_oid( reiser4_oid_allocator *map UNUSED_ARG )
{
	assert( "nikita-443", map != NULL );
	return ABSOLUTE_MIN_OID + OIDS_RESERVED;
}

/** return number of user-visible oids already allocated in this map.
    Used by reiser4_statfs() to report "f_files". */
__u64 reiser4_oids_used( reiser4_oid_allocator *map )
{
	__u64 result;

	assert( "nikita-444", map != NULL );
	lock( map );
	result = map -> next_to_use - reiser4_minimal_oid( map );
	unlock( map );
	return result;
}

/** allocate new objectid in "map" and store it in "result". Return 0 on
    success, negative error code on failure. */
int reiser4_allocate_oid( reiser4_oid_allocator *map, oid_t *result UNUSED_ARG )
{
	assert( "nikita-445", map != NULL );
	lock( map );
	*result = map -> next_to_use;
	++ map -> next_to_use;
	trace_on( TRACE_OIDS, "[%i]: allocated: %llx\n", current_pid, *result );
	unlock( map );
	return 0;
}

/** release object id back to "map". Return error code. */
int reiser4_release_oid( reiser4_oid_allocator *map UNUSED_ARG, 
			 oid_t oid UNUSED_ARG )
{
	assert( "nikita-446", map != NULL );
	trace_on( TRACE_OIDS, "[%i]: released: %llx\n", current_pid, oid );
	return -ENOSYS;
}

/** how many pages to reserve in transaction for allocation of new
    objectid */
int reiser4_oid_reserve_allocate( reiser4_oid_allocator *map UNUSED_ARG )
{
	return 1;
}

/** how many pages to reserve in transaction for freeing of an
    objectid */
int reiser4_oid_reserve_release( reiser4_oid_allocator *map UNUSED_ARG )
{
	return 1;
}

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
