/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Scalable integers. */

/* scint_t is 32bit data-type used for representation of "mostly 31bit" 64bit
   quantities, that is quantities that require 64bits in the worst case, but
   usually fit into 31bits.

   When such value fits into 31 bit it is stored directly in the
   scint->datum. Otherwise, scint->datum is pointer to 64 bit chunk allocated
   from slab. Sign bit of scint->datum is used to discriminate above cases:

   if scint->datum<0, then scint->datum<<1 is pointer to __u64, otherwise

   scint->datum is value itself.
 */

#include "forward.h"
#include "debug.h"
#include "scint.h"

#include <linux/types.h>	/* for __u?? , ino_t */
#include <linux/fs.h>		/* for struct super_block, struct rw_semaphore, etc  */
#include <linux/spinlock.h>
#include <asm/types.h>

#include <linux/slab.h>

#if BITS_PER_LONG < 64

static int is_extern(const scint_t *scint)
{
	return scint->datum < 0;
}

static __u64 *get_extern(const scint_t *scint)
{
	assert("nikita-2843", is_extern(scint));
	return (__u64 *)(scint->datum << 1);
}

static kmem_cache_t *scint_cache;

static int prepare(scint_t *scint, __u64 value, int gfp_mask)
{
	void *arena;

	if (is_extern(scint))
		return 1;

	if (value < 0x80000000)
		return 0;

	arena = kmem_cache_alloc(scint_cache, gfp_mask);
	if (arena != NULL) {
		/* set highest bit */
		scint->datum = (((__u32)arena) >> 1) | 0x80000000;
		return 1;
	} else
		return RETERR(-ENOMEM);
}

void scint_init(scint_t *scint)
{
	scint->datum = 0;
}

void scint_done(scint_t *scint)
{
	if (is_extern(scint))
		kmem_cache_free(scint_cache, get_extern(scint));
}

int scint_pack(scint_t *scint, __u64 value, int gfp_mask)
{
	int result;

	cassert(sizeof(scint->datum) >= sizeof(__u64 *));

	result = prepare(scint, value, gfp_mask);
	switch(result) {
	case 0:
		scint->datum = (__u32)value;
		break;
	case 1:
		assert("nikita-2845", is_extern(scint));
		*get_extern(scint) = value;
		break;
	default:
		return result;
	}
	return 0;
}

__u64 scint_unpack(const scint_t *scint)
{
	if (is_extern(scint))
		return *get_extern(scint);
	else
		return (__u64)scint->datum;
}

static kmem_cache_t *scint_slab;

int scint_init_once(void)
{
	scint_slab = kmem_cache_create("scint", sizeof(__u64), 0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	return (scint_slab == NULL) ? RETERR(-ENOMEM) : 0;
}

void scint_done_once(void)
{
	if (kmem_cache_destroy(scint_slab) != 0)
		warning("nikita-2844", "not all scalable ints were freed");
}

#endif

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

