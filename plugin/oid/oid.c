/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../plugin.h"
#include "../plugin_header.h"
#include "../../debug.h"

#include "../../super.h"
#include "../../key.h"
#include "../../txnmgr.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */

/* a wrapper for allocate_oid method of oid_allocator plugin */
int
oid_allocate(oid_t * oid)
{
	struct super_block *s = reiser4_get_current_sb();

	reiser4_super_info_data *sbinfo = get_super_private(s);
	oid_allocator_plugin *oplug;

	assert("vs-479", sbinfo != NULL);
	oplug = sbinfo->oid_plug;
	assert("vs-480", oplug && oplug->allocate_oid);
	return oplug->allocate_oid(get_oid_allocator(s), oid);
}

/* a wrapper for release_oid method of oid_allocator plugin */
int
oid_release(oid_t oid)
{
	struct super_block *s = reiser4_get_current_sb();

	reiser4_super_info_data *sbinfo = get_super_private(s);
	oid_allocator_plugin *oplug;

	assert("nikita-1902", sbinfo != NULL);
	oplug = sbinfo->oid_plug;
	assert("nikita-1903", (oplug != NULL) && (oplug->release_oid != NULL));
	return oplug->release_oid(get_oid_allocator(s), oid);
}

/* FIXME-ZAM: it is enough ugly but each operation of OID allocating/releasing
   are split into two parts, allocating/releasing itself and counting it (or
   logging). At OID allocation time an atom could not be available yet, but it
   should be available after we modify at least one block. So, new OID is
   allocated _before_ actual transaction begins, and this operations is
   counted _after_ it.  An allocation of empty atom can be a solution which
   has inefficiency in case of further atom fusion on first captured
   block. The OID releasing operation is spilt just for symmetry */

/* count an object allocation in atom's nr_objects_created when current atom
   is available */
void
oid_count_allocated(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	atom->nr_objects_created++;
	spin_unlock_atom(atom);
}

/* count an object deletion in atom's nr_objects_deleted */
void
oid_count_released(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	atom->nr_objects_deleted++;
	spin_unlock_atom(atom);
}

__u64 oid_used(void)
{
	reiser4_super_info_data *sbinfo = get_current_super_private();

	assert("zam-636", sbinfo != NULL);
	assert("zam-598", sbinfo->oid_plug != NULL);
	assert("zam-599", sbinfo->oid_plug->oids_used != NULL);

	return sbinfo->oid_plug->oids_used(&sbinfo->oid_allocator);
}

__u64 oid_next(void)
{
	reiser4_super_info_data *sbinfo = get_current_super_private();

	assert("zam-637", sbinfo != NULL);
	assert("zam-638", sbinfo->oid_plug != NULL);
	assert("zam-639", sbinfo->oid_plug->next_oid != NULL);

	return sbinfo->oid_plug->next_oid(&sbinfo->oid_allocator);
}

int
oid_init_allocator(const struct super_block *s, __u64 nr_files, __u64 oids)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);

	assert("zam-640", sbinfo != NULL);
	assert("zam-641", sbinfo->oid_plug != NULL);
	assert("zam-642", sbinfo->oid_plug->init_oid_allocator != NULL);

	return sbinfo->oid_plug->init_oid_allocator(&sbinfo->oid_allocator, nr_files, oids);
}

#if REISER4_DEBUG_OUTPUT
void
oid_print_allocator(const char *prefix, const struct super_block *s)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);

	if (sbinfo->oid_plug && sbinfo->oid_plug->print_info)
		sbinfo->oid_plug->print_info(prefix, &sbinfo->oid_allocator);
	return;
}
#endif

/* initialization of objectid management plugins */
oid_allocator_plugin oid_plugins[LAST_OID_ALLOCATOR_ID] = {
	[OID40_ALLOCATOR_ID] = {
				.h = {
				      .type_id = REISER4_OID_ALLOCATOR_PLUGIN_TYPE,
				      .id = OID40_ALLOCATOR_ID,
				      .pops = NULL,
				      .label = "reiser40 default oid manager",
				      .desc = "no reusing objectids",
				      .linkage = TS_LIST_LINK_ZERO,
				      }
				,
				.init_oid_allocator = oid40_read_allocator,
				.oids_used = oid40_used,
				.next_oid = oid40_next_oid,
				.oids_free = oid40_free,
				.allocate_oid = oid40_allocate,
				.release_oid = oid40_release,
				.oid_reserve_allocate = oid40_reserve_allocate,
				.oid_reserve_release = oid40_reserve_release,
				.print_info = oid40_print_info}
};

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
