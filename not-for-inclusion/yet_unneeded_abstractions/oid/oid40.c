/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "../../debug.h"
#include "../../key.h"
#include "oid40.h"
#include "oid.h"

#include <linux/types.h>	/* for __u??  */

/* Object-id manipulations.
   reiser 4.0 default objectid manager */

/* Maximal possible object id. */
static const oid_t ABSOLUTE_MAX_OID = (oid_t) ~ 0;

/* Minimal possible object id. */
static const oid_t ABSOLUTE_MIN_OID = (oid_t) 0;

/* reserve 65k oids for internal use on both ends of oid-space.
    There is no reason to be greedy here. */
/* AUDIT how is it reserved on both ends of oid space, if oid40_read_allocator
   is passed with pre-determined starting oid value (that is not checked against
   being smaller then this value)? */
#define OIDS_RESERVED  ( 1 << 16 )

/* plugin->u.oid_allocator.read_oid_allocator
   Initialise object id allocator */
int
oid40_read_allocator(reiser4_oid_allocator * map, __u64 nr_files, __u64 oids)
{
	assert("nikita-1977", map != NULL);

	spin_lock_init(&map->u.oid40.oguard);
	map->u.oid40.next_to_use = oids;
	map->u.oid40.oids_in_use = nr_files;
	return 0;
}

/* helper function: spin lock allocator */
static void
lock(reiser4_oid_allocator * map)
{
	assert("nikita-1978", map != NULL);
	spin_lock(&map->u.oid40.oguard);
}

/* helper function: spin unlock allocator */
static void
unlock(reiser4_oid_allocator * map)
{
	assert("nikita-1979", map != NULL);
	spin_unlock(&map->u.oid40.oguard);
}

/* plugin->u.oid_allocator.oids_free
   number of oids available for use by users */
__u64 oid40_free(reiser4_oid_allocator * map)
{
	__u64 result;

	assert("nikita-1980", map != NULL);

	lock(map);
	result = ABSOLUTE_MAX_OID - OIDS_RESERVED - map->u.oid40.next_to_use;
	unlock(map);
	return result;
}

/* plugin->u.oid_allocator.next_oid */
__u64 oid40_next_oid(reiser4_oid_allocator * map)
{
	__u64 result;

	assert("zam-601", map != NULL);

	lock(map);
	result = map->u.oid40.next_to_use;
	unlock(map);

	return result;
}

/* plugin->u.oid_allocator.oids_used
   return number of user-visible oids already allocated in this map */
__u64 oid40_used(reiser4_oid_allocator * map)
{
	__u64 result;

	assert("nikita-1981", map != NULL);

	lock(map);
	result = map->u.oid40.oids_in_use;
	unlock(map);
	return result;
}

/* plugin->u.oid_allocator.allocate_oid
   allocate new objectid in "map" and store it in "result". Return 0
   on success, negative error code on failure. */
int
oid40_allocate(reiser4_oid_allocator * map, oid_t * result)
{
	assert("nikita-1982", map != NULL);

	lock(map);
	*result = map->u.oid40.next_to_use;
	++map->u.oid40.next_to_use;
	++map->u.oid40.oids_in_use;
	ON_TRACE(TRACE_OIDS, "[%i]: allocated: %llx\n", current->pid, *result);
	assert("nikita-1983", map->u.oid40.next_to_use >= map->u.oid40.oids_in_use);
	unlock(map);
	return 0;
}

/* plugin->u.oid_allocator.allocate_oid
   release object id back to "map". */
/* This never actually marks oid as free, oid "map" is 64 bits and right now
   there is assumption that counter would never overflow */
int
oid40_release(reiser4_oid_allocator * map, oid_t oid UNUSED_ARG)
{
	assert("nikita-1984", map != NULL);	/* BIG BROTHER IS WATCHING YOU */

	ON_TRACE(TRACE_OIDS, "[%i]: released: %llx\n", current->pid, oid);
	lock(map);
	assert("nikita-1985", map->u.oid40.oids_in_use > 0);
	--map->u.oid40.oids_in_use;
	assert("nikita-1986", map->u.oid40.next_to_use >= map->u.oid40.oids_in_use);
	unlock(map);
	return 0;
}

/* plugin->u.oid_allocator.reserve_allocate
   how many pages to reserve in transaction for allocation of new objectid */
/* This currently assumes that PAGE_SIZE equals blocksize */
int
oid40_reserve_allocate(reiser4_oid_allocator * map UNUSED_ARG)
{
	return 1;
}

/* plugin->u.oid_allocator.reserve_release
   how many pages to reserve in transaction for freeing of an objectid */
/* This currently assumes that PAGE_SIZE equals blocksize */
int
oid40_reserve_release(reiser4_oid_allocator * map UNUSED_ARG)
{
	return 1;
}

/* plugin->u.oid_allocator.print_info */
void
oid40_print_info(const char *prefix, reiser4_oid_allocator * map)
{
	lock(map);
	printk("%s: next free objectid %lli, "
	       "oids in use %llu\n", prefix, map->u.oid40.next_to_use, map->u.oid40.oids_in_use);
	unlock(map);
}

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
