/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/*
 * Object-id manipulations.
 * reiser 4.0 default objectid manager
 */

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
 * plugin->u.oid_allocator.read_oid_allocator
 * Initialise object id allocator
 */
int oid_40_read_allocator( reiser4_oid_allocator *map, __u64 nr_files, __u64 oids )
{
	assert( "nikita-1168", map != NULL );

	spin_lock_init( &map -> u.oid_40.oguard );
	map -> u.oid_40.next_to_use = oids;
	map -> u.oid_40.oids_in_use = nr_files;
	return 0;
}

/** helper function: spin lock allocator */
static void lock( reiser4_oid_allocator *map )
{
	assert( "nikita-1648", map != NULL );
	spin_lock( &map -> u.oid_40.oguard );
}

/** helper function: spin unlock allocator */
static void unlock( reiser4_oid_allocator *map )
{
	assert( "nikita-1660", map != NULL );
	spin_unlock( &map -> u.oid_40.oguard );
}

/**
 * plugin->u.oid_allocator.oids_free
 * number of oids available for use by users
 */
__u64 oid_40_free( reiser4_oid_allocator *map )
{
	__u64 result;

	assert( "nikita-1797", map != NULL );

	lock( map );
	result = ABSOLUTE_MAX_OID - OIDS_RESERVED - map -> u.oid_40.next_to_use;
	unlock( map );
	return result;
}


/**
 * plugin->u.oid_allocator.oids_used
 * return number of user-visible oids already allocated in this map
 */
__u64 oid_40_used( reiser4_oid_allocator *map )
{
	__u64 result;

	assert( "nikita-444", map != NULL );

	lock( map );
	result = map -> u.oid_40.oids_in_use;
	unlock( map );
	return result;
}

/** 
 * plugin->u.oid_allocator.allocate_oid
 * allocate new objectid in "map" and store it in "result". Return 0
 * on success, negative error code on failure.
 */
int oid_40_allocate( reiser4_oid_allocator *map, oid_t *result UNUSED_ARG )
{
	assert( "nikita-445", map != NULL );

	lock( map );
	*result = map -> u.oid_40.next_to_use;
	++ map -> u.oid_40.next_to_use;
	++ map -> u.oid_40.oids_in_use;
	trace_on( TRACE_OIDS, "[%i]: allocated: %llx\n", current_pid, *result );
	assert( "nikita-1794", map -> u.oid_40.next_to_use >= map -> u.oid_40.oids_in_use );
	unlock( map );
	return 0;
}

/**
 * plugin->u.oid_allocator.allocate_oid
 * release object id back to "map".
 */
int oid_40_release( reiser4_oid_allocator *map, oid_t oid UNUSED_ARG )
{
	assert( "nikita-446", map != NULL );

	trace_on( TRACE_OIDS, "[%i]: released: %llx\n", current_pid, oid );
	lock( map );
	assert( "nikita-1796", map -> u.oid_40.oids_in_use > 0 );
	-- map -> u.oid_40.oids_in_use;
	assert( "nikita-1795", map -> u.oid_40.next_to_use >= map -> u.oid_40.oids_in_use );
	unlock( map );
	return 0;
}

/**
 * plugin->u.oid_allocator.reserve_allocate
 * how many pages to reserve in transaction for allocation of new objectid
 */
int oid_40_reserve_allocate( reiser4_oid_allocator *map UNUSED_ARG )
{
	return 1;
}

/**
 * plugin->u.oid_allocator.reserve_release
 * how many pages to reserve in transaction for freeing of an objectid
 */
int oid_40_reserve_release( reiser4_oid_allocator *map UNUSED_ARG )
{
	return 1;
}
