/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* plugin-sets */

#include "../debug.h"

#include "plugin_set.h"

#include <linux/slab.h>
#include <linux/stddef.h>

/* slab for plugin sets */
static kmem_cache_t *plugin_set_slab;

static spinlock_t plugin_set_lock[8] __cacheline_aligned_in_smp = {
	[0 ... 7] = SPIN_LOCK_UNLOCKED
};

/* hash table support */

#define PS_TABLE_SIZE (32)

static inline plugin_set *
cast_to(const atomic_t * a)
{
	return container_of(a, plugin_set, ref);
}

static inline int
pseq(const atomic_t * a1, const atomic_t * a2)
{
	plugin_set *set1;
	plugin_set *set2;

	/* make sure fields are not missed in the code below */
	cassert(sizeof *set1 ==

		sizeof set1->ref + 
		sizeof set1->hashval + 
		sizeof set1->link + 

		sizeof set1->file + 
		sizeof set1->dir + 
		sizeof set1->perm + 
		sizeof set1->tail + 
		sizeof set1->hash + 
		sizeof set1->sd + 
		sizeof set1->dir_item + 
		sizeof set1->crypto +
		sizeof set1->digest +
		sizeof set1->compression);

	set1 = cast_to(a1);
	set2 = cast_to(a2);
	return 
		set1->file == set2->file &&
		set1->dir == set2->dir &&
		set1->perm == set2->perm &&
		set1->tail == set2->tail &&
		set1->hash == set2->hash &&
		set1->sd == set2->sd &&
		set1->dir_item == set2->dir_item &&
		set1->crypto == set2->crypto &&
		set1->digest == set2->digest &&
		set1->compression == set2->compression;
}

#define HASH_FIELD(hash, set, field)		\
({						\
	__u32 top;				\
						\
	top = (hash) >> (32 - 4);		\
	(hash) <<= 4;				\
	(hash) |= top;				\
	(hash) ^= (__u32)(set)->field;		\
})

static inline __u32 calculate_hash(const plugin_set *set)
{
	__u32 result;

	result = 0;
	HASH_FIELD(result, set, file);
	HASH_FIELD(result, set, dir);
	HASH_FIELD(result, set, perm);
	HASH_FIELD(result, set, tail);
	HASH_FIELD(result, set, hash);
	HASH_FIELD(result, set, sd);
	HASH_FIELD(result, set, dir_item);
	HASH_FIELD(result, set, crypto);
	HASH_FIELD(result, set, digest);
	HASH_FIELD(result, set, compression);
	return result & (PS_TABLE_SIZE - 1);
}

static inline __u32
pshash(const atomic_t * a)
{
	return cast_to(a)->hashval;
}

/* The hash table definition */
#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TS_HASH_DEFINE(ps, plugin_set, atomic_t, ref, link, pshash, pseq);
#undef KFREE
#undef KMALLOC

static ps_hash_table ps_table;
static plugin_set empty_set = {
	.ref                = ATOMIC_INIT(1),
	.hashval            = 0,
	.file               = NULL,
	.dir                = NULL,
	.perm               = NULL,
	.tail               = NULL,
	.hash               = NULL,
	.sd                 = NULL,
	.dir_item           = NULL,
	.crypto             = NULL,
	.digest             = NULL,
	.compression        = NULL,
	.link               = { NULL }
};

plugin_set *plugin_set_get_empty(void)
{
	return plugin_set_clone(&empty_set);
}

plugin_set *plugin_set_clone(plugin_set *set)
{
	assert("nikita-2901", set != NULL);

	atomic_inc(&set->ref);
	return set;
}

void plugin_set_put(plugin_set *set)
{
	spinlock_t *lock;

	assert("nikita-2900", set != NULL);

	lock = &plugin_set_lock[set->hashval & 7];
	if (atomic_dec_and_lock(&set->ref, lock)) {
		assert("nikita-2899", set != &empty_set);
		ps_hash_remove(&ps_table, set);
		spin_unlock(lock);
		kmem_cache_free(plugin_set_slab, set);
	}
}

int plugin_set_field(plugin_set **set, void *val, int offset, int len)
{
	spinlock_t *lock;
	plugin_set  replica;
	plugin_set *twin;
	plugin_set *psal;
	plugin_set *orig;

	assert("nikita-2902", set != NULL);
	assert("nikita-2904", *set != NULL);
	assert("nikita-2903", val != NULL);

	if (unlikely(!memcmp(((char *)*set) + offset, val, len)))
		return 0;

	replica = *(orig = *set);
	xmemcpy(((char *)&replica) + offset, val, len);
	replica.hashval = calculate_hash(&replica);
	psal = NULL;
	lock = &plugin_set_lock[replica.hashval & 7];
	spin_lock(lock);
	twin = ps_hash_find(&ps_table, &replica.ref);
	if (unlikely(twin == NULL)) {
		spin_unlock(lock);
		psal = kmem_cache_alloc(plugin_set_slab, GFP_KERNEL);
		if (psal == NULL)
			return RETERR(-ENOMEM);
		atomic_set(&psal->ref, 1);
		*psal = replica;
		spin_lock(lock);
		twin = ps_hash_find(&ps_table, &replica.ref);
		if (likely(twin) == NULL) {
			*set = psal;
			ps_hash_insert(&ps_table, psal);
		} else {
			*set = plugin_set_clone(twin);
			kmem_cache_free(plugin_set_slab, psal);
		}
	} else
		*set = plugin_set_clone(twin);
	spin_unlock(lock);
	plugin_set_put(orig);
	return 0;
}

#define DEFINE_PLUGIN_SET(type, field)						\
int plugin_set_ ## field(plugin_set **set, type *val)				\
{										\
	return plugin_set_field(set, &val, 					\
				offsetof(plugin_set, field), sizeof(val));	\
}

DEFINE_PLUGIN_SET(file_plugin, file)
DEFINE_PLUGIN_SET(dir_plugin, dir)
DEFINE_PLUGIN_SET(perm_plugin, perm)
DEFINE_PLUGIN_SET(tail_plugin, tail)
DEFINE_PLUGIN_SET(hash_plugin, hash)
DEFINE_PLUGIN_SET(item_plugin, sd)
DEFINE_PLUGIN_SET(item_plugin, dir_item)
DEFINE_PLUGIN_SET(crypto_plugin, crypto)
DEFINE_PLUGIN_SET(digest_plugin, digest)
DEFINE_PLUGIN_SET(compression_plugin, compression)

int plugin_set_init(void)
{
	int result;

	result = ps_hash_init(&ps_table, PS_TABLE_SIZE, NULL);
	if (result == 0) {
		plugin_set_slab = kmem_cache_create("plugin_set", 
						    sizeof (plugin_set), 0, 
						    SLAB_HWCACHE_ALIGN, 
						    NULL, NULL);
		if (plugin_set_slab == NULL)
			result = RETERR(-ENOMEM);
	}
	return result;
}

void plugin_set_done(void)
{
	kmem_cache_destroy(plugin_set_slab);
	ps_hash_done(&ps_table);
}


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
