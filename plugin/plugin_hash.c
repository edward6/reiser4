/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Generic hash table for use by external plugins. */

#include "../debug.h"

#include "plugin_hash.h"

#include <linux/slab.h>
#include <linux/stddef.h>

TS_LIST_DEFINE(phash, phash_user, link);

static phash_list_head phash_anchor[PHASH_LAST];
static spinlock_t phash_lock = SPIN_LOCK_UNLOCKED;

/* hash table support */

#define PHASH_TABLE_SIZE (128)

static inline phash_header *to_header(const phash_hash_link * anchor)
{
	return container_of(anchor, phash_header, link);
}

static inline int
phasheq(const phash_hash_link * a1, const phash_hash_link * a2)
{
	phash_header *h1;
	phash_header *h2;

	h1 = to_header(a1);
	h2 = to_header(a2);
	return (h1->user == h1->user) && (h1->object == h2->object);
}

static inline __u32
hash(__u32 user, __u32 object)
{
	return (user >> 3) ^ (object >> 1);
}

static inline __u32
phash_hash(const phash_hash_link * a)
{
	phash_header *h;

	h = to_header(a);
	return hash((__u32)h->user, (__u32)h->object);
}

/* The hash table definition */
#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TS_HASH_DEFINE(phash, phash_header,
	       phash_hash_link, link, link, phash_hash, phasheq);
#undef KFREE
#undef KMALLOC

static phash_hash_table phash_table;

#if REISER4_DEBUG
static int is_scope_valid(phash_scope scope)
{
	return (0 <= (int)scope) && (scope < PHASH_LAST);
}
#endif

int phash_user_register(phash_user *user)
{
	assert("nikita-2924", user != NULL);
	assert("nikita-2925", is_scope_valid(user->scope));
	spin_lock(&phash_lock);
	phash_list_push_back(&phash_anchor[user->scope], user);
	spin_unlock(&phash_lock);
	return 0;
}

void phash_user_unregister(phash_user *user)
{
	assert("nikita-2926", user != NULL);
	assert("nikita-2927", is_scope_valid(user->scope));
	spin_lock(&phash_lock);
	phash_list_remove_clean(user);
	spin_unlock(&phash_lock);
}

phash_header *phash_get(phash_user *user, void *object)
{
	phash_header  head;
	phash_header *found;

	head.user   = user;
	head.object = object;

	spin_lock(&phash_lock);
	found = phash_hash_find(&phash_table, &head.link);
	spin_unlock(&phash_lock);
	return found;
}

void phash_set(phash_user *user, void *object, phash_header *value)
{
	value->user   = user;
	value->object = object;

	spin_lock(&phash_lock);
	phash_hash_insert(&phash_table, value);
	spin_unlock(&phash_lock);
}

int phash_destroy_hook(phash_scope scope, void *object)
{
	int result;
	int called;
	phash_user *user;

	assert("nikita-2928", is_scope_valid(scope));

	result = 0;
	do {
		called = 0;
		spin_lock(&phash_lock);
		for_all_tslist(phash, &phash_anchor[scope], user) {
			phash_header *head;

			if (user->ops.destroy == NULL)
				continue;

			head = phash_get(user, object);
			if (head != NULL) {
				int reply;

				spin_unlock(&phash_lock);
				reply = user->ops.destroy(user, object, head);
				if (reply != 0 && result == 0)
					result = reply;
				spin_lock(&phash_lock);
			}
			++ called;
		}
	} while(called > 0);
	spin_unlock(&phash_lock);
	return result;
}

int phash_init(void)
{
	int i;

	for (i = 0 ; i < PHASH_LAST ; ++ i)
		phash_list_init(&phash_anchor[i]);

	return phash_hash_init(&phash_table, PHASH_TABLE_SIZE, NULL);
}

void phash_done(void)
{
	phash_hash_done(&phash_table);
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
