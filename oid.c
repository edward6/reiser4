/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
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
/* Audited by: green(2002.06.15) */
int init_oid_allocator( reiser4_oid_allocator_t *map /* oid allocator to
						      * initialise */ )
{
	assert( "nikita-1168", map != NULL );
	
	spin_lock_init( &map -> oguard );
	map -> next_to_use = ABSOLUTE_MIN_OID + OIDS_RESERVED;
	map -> oids_in_use = 0;
	return 0;
}

/** helper function: spin lock allocator */
/* Audited by: green(2002.06.15) */
static void lock( reiser4_oid_allocator_t *map /* oid allocator to lock */ )
{
	assert( "nikita-1648", map != NULL );
	spin_lock( &map -> oguard );
}

/** helper function: spin unlock allocator */
/* Audited by: green(2002.06.15) */
static void unlock( reiser4_oid_allocator_t *map /* oid allocator to unlock */ )
{
	assert( "nikita-1660", map != NULL );
	spin_unlock( &map -> oguard );
}

/** number of oids available for use by users */
/* Audited by: green(2002.06.15) */
__u64 oids_free( reiser4_oid_allocator_t *map /* oid allocator to query */ )
{
	__u64 result;

	assert( "nikita-1797", map != NULL );

	lock( map );
	/* AUDIT: this calculation seems to suffer from off-by-one error.
	   + 1 should be added to correctly represent amount of free oids */
	result = ABSOLUTE_MAX_OID - OIDS_RESERVED - map -> next_to_use;
	unlock( map );
	return result;
}


/** return number of user-visible oids already allocated in this map.
    Used by reiser4_statfs() to report "f_files". */
/* Audited by: green(2002.06.15) */
__u64 oids_used( reiser4_oid_allocator_t *map /* oid allocator to query */ )
{
	__u64 result;

	assert( "nikita-444", map != NULL );
	lock( map );
	result = map -> oids_in_use;
	unlock( map );
	return result;
}

/** allocate new objectid in "map" and store it in "result". Return 0 on
    success, negative error code on failure. */
/* Audited by: green(2002.06.15) */
int allocate_oid( reiser4_oid_allocator_t *map /* oid allocator to allocate
						* oid from */, 
		  oid_t *result UNUSED_ARG /* result */ )
{
	assert( "nikita-445", map != NULL );
	lock( map );
	*result = map -> next_to_use;
	++ map -> next_to_use;
	++ map -> oids_in_use;
	trace_on( TRACE_OIDS, "[%i]: allocated: %llx\n", current_pid, *result );
	assert( "nikita-1794", map -> next_to_use >= map -> oids_in_use );
	unlock( map );
	return 0;
}

/** release object id back to "map". */
/* Audited by: green(2002.06.15) */
int release_oid( reiser4_oid_allocator_t *map UNUSED_ARG /* oid allocator to
							  * release oid back
							  * to */,
		 oid_t oid UNUSED_ARG /* oid to release */ )
{
	assert( "nikita-446", map != NULL );
	trace_on( TRACE_OIDS, "[%i]: released: %llx\n", current_pid, oid );
	lock( map );
	assert( "nikita-1796", map -> oids_in_use > 0 );
	-- map -> oids_in_use;
	assert( "nikita-1795", map -> next_to_use >= map -> oids_in_use );
	unlock( map );
	return 0;
}

/** how many pages to reserve in transaction for allocation of new
    objectid */
/* Audited by: green(2002.06.15) */
int oid_reserve_allocate( reiser4_oid_allocator_t *map UNUSED_ARG /* oid
								     allocator
								     to
								     query */ )
{
	return 1;
}

/** how many pages to reserve in transaction for freeing of an
    objectid */
/* Audited by: green(2002.06.15) */
int oid_reserve_release( reiser4_oid_allocator_t *map UNUSED_ARG /* oid
								  * allocator
								  * to
								  * query */)
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
