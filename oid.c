/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "super.h"
#include "txnmgr.h"

/* we used to have oid plugin. It was removed because it was
   recognized as providing unneeded level of abstraction. If one ever
   will find it useful - look at as_yet_unneeded_abstractions/oid */

int
oid_init_allocator(struct super_block *super, oid_t nr_files, oid_t next)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = get_super_private(super);

	sbinfo->next_to_use = next;
	sbinfo->oids_in_use = nr_files;
	return 0;
}

oid_t
oid_allocate(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t oid;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(super);
	oid = sbinfo->next_to_use ++;
	sbinfo->oids_in_use ++;
	reiser4_spin_unlock_sb(super);
	return oid;
}

int
oid_release(struct super_block *s, oid_t oid)
{
	return 0;
}

oid_t oid_next(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t oid;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(super);
	oid = sbinfo->next_to_use;
	reiser4_spin_unlock_sb(super);
	return oid;
}

long oids_used(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t used;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(super);
	used = sbinfo->oids_in_use;
	reiser4_spin_unlock_sb(super);
	if (used < (__u64) ((long) ~0) >> 1)
		return (long) used;
	else
		return (long) -1;
}

/* Maximal possible object id. */
static const oid_t ABSOLUTE_MAX_OID = (oid_t) ~ 0;
#define OIDS_RESERVED  ( 1 << 16 )

long oids_free(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t oids;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(super);
	oids = ABSOLUTE_MAX_OID - OIDS_RESERVED - sbinfo->next_to_use;
	reiser4_spin_unlock_sb(super);
	if (oids < (__u64) ((long) ~0) >> 1)
		return (long) oids;
	else
		return (long) -1;
}

void
oid_count_allocated(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	atom->nr_objects_created++;
	UNLOCK_ATOM(atom);
}

/* count an object deletion in atom's nr_objects_deleted */
void
oid_count_released(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	atom->nr_objects_deleted++;
	UNLOCK_ATOM(atom);
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
