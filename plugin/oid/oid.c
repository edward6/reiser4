/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "../../reiser4.h"

/* a wrapper for allocate_oid method of oid_allocator plugin */
int allocate_oid( oid_t *oid )
{
	struct super_block *s = reiser4_get_current_sb();

	reiser4_super_info_data * private = get_super_private(s);
	oid_allocator_plugin *oplug;

	assert( "vs-479", private != NULL );
	oplug = private-> oid_plug;
	assert( "vs-480", oplug && oplug -> allocate_oid );
	return oplug -> allocate_oid( get_oid_allocator( s ), oid );
}

/* a wrapper for release_oid method of oid_allocator plugin */
int release_oid( oid_t oid )
{
	struct super_block *s = reiser4_get_current_sb();

	reiser4_super_info_data * private = get_super_private(s);
	oid_allocator_plugin *oplug;

	assert( "nikita-1902", private != NULL);
	oplug = private -> oid_plug;
	assert( "nikita-1903", ( oplug != NULL ) && 
		( oplug -> release_oid != NULL ) );
	return  oplug -> release_oid
		( get_oid_allocator(s), oid );
}

/* FIXME-ZAM: it is enough ugly but each operation of OID allocating/releasing
 * are split into two parts, allocating/releasing itself and counting it (or
 * logging). At OID allocation time an atom could not be available yet, but it
 * should be available after we modify at least one block. So, new OID is
 * allocated _before_ actual transaction begins, and this operations is
 * counted _after_ it.  An allocation of empty atom can be a solution which
 * has inefficiency in case of further atom fusion on first captured
 * block. The OID releasing operation is spilt just for symmetry */

/* count an object allocation in atom's nr_objects_created when current atom
 * is available */
void count_allocated_oid ( void )
{
	txn_atom * atom;

	atom = get_current_atom_locked();
	atom->nr_objects_created ++;
	spin_unlock_atom(atom);
}

/* count an object deletion in atom's nr_objects_deleted */
void count_released_oid( void )
{
	txn_atom * atom;

	atom = get_current_atom_locked();
	atom->nr_objects_created ++;
	spin_unlock_atom(atom);
}


/* initialization of objectid management plugins */
reiser4_plugin oid_plugins[ LAST_OID_ALLOCATOR_ID ] = {
	[ OID_40_ALLOCATOR_ID ] = {
		.oid_allocator = {
			.h = {
				.type_id = REISER4_OID_ALLOCATOR_PLUGIN_TYPE,
				.id      = OID_40_ALLOCATOR_ID,
				.pops    = NULL,
				.label   = "reiser40 default oid manager",
				.desc    = "no reusing objectids",
				.linkage = TS_LIST_LINK_ZERO,
			},
			.init_oid_allocator   = oid_40_read_allocator,
			.oids_used            = oid_40_used,
			.next_oid             = oid_40_next_oid,
			.oids_free            = oid_40_free,
			.allocate_oid         = oid_40_allocate,
			.release_oid          = oid_40_release,
			.oid_reserve_allocate = oid_40_reserve_allocate,
			.oid_reserve_release  = oid_40_reserve_release,
			.print_info           = oid_40_print_info
		}
	}
};
