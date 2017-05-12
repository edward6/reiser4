/*
  Balanced Fiber-Striped array with Weights.
  Implementation over 32-bit hash.
  Inventor, Author: Eduard O. Shishkin

  Copyright (c) 2014-2017 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <asm/types.h>
#include "../../debug.h"
#include "../../inode.h"
#include "../plugin.h"
#include "aid.h"

#define MAX_SHIFT    31
#define MAX_BUCKETS  (1u << MAX_SHIFT)
#define WORDSIZE     (sizeof(u32))

static inline void *mem_alloc(u64 len)
{
	return kzalloc(len, reiser4_ctx_gfp_mask_get());
}

static inline void mem_free(void *p)
{
	kfree(p);
}

static inline struct fsw32_aid *fsw32_private(reiser4_aid *aid)
{
	return &aid->fsw32;
}

static void init_fibers_by_tab(u32 numb,
			       u32 nums_bits,
			       u32 *tab,
			       void *vec,
			       void *(*fiber_at)(void *vec, u64 idx),
			       u32 *weights)
{
	u32 i;
	u32 nums = 1 << nums_bits;

	for(i = 0; i < numb; i++)
		weights[i] = 0;

	for(i = 0; i < nums; i++) {
		u32 *fib;

		fib = fiber_at(vec, tab[i]);
		fib[(weights[tab[i]])++] = i;
	}
}

static void init_tab_by_fibers(u32 numb,
			       u32 *tab,
			       void *vec,
			       void *(*fiber_at)(void *vec, u64 idx),
			       u32 *weights)
{
	u32 i, j;

	for(i = 0; i < numb; i++)
		for (j = 0; j < weights[i]; j++) {
			u32 *fib;
			fib = fiber_at(vec, i);
			tab[fib[j]] = i;
		}
}

static u64 array64_el_at(void *vec, u32 idx)
{
	return ((u64 *)vec)[idx];
}

u32 *init_tab_from_scratch(u32 *weights, u32 numb, u32 nums_bits)
{
	u32 i, j, k;
	u32 *tab;
	u32 nums = 1 << nums_bits;

	tab = mem_alloc(nums * WORDSIZE);
	if (!tab)
		return NULL;
	for (i = 0, k = 0; i < numb; i++)
		for (j = 0; j < weights[i]; j++)
			tab[k++] = i;
	return tab;
}

/*
 * Construct a "similar" integer vector, which have
 * specified @sum of its components
 */
void normalize_vector(u32 num,
		      void *vec,
		      u64 (*vec_el_at)(void *vec, u64 idx),
		      u32 sum, u32 *result)
{
	u32 i;
	u32 rest;
	u32 sum_scaled = 0;
	u64 sum_not_scaled = 0;

	for (i = 0; i < num; i++)
		sum_not_scaled += vec_el_at(vec, i);
	for (i = 0; i < num; i++) {
		u64 q;
		q = sum * vec_el_at(vec, i);
		result[i] = div64_u64(q, sum_not_scaled);
		sum_scaled += result[i];
	}
	rest = sum - sum_scaled;
	/*
	 * distribute the rest among the first members.
	 * Don't modify this: it will be a format change
	 */
	for (i = 0; i < rest; i++)
		result[i] ++;
	return;
}

static int create_fibers(u32 nums_bits, u32 *tab,
			 u32 new_numb, u32 *new_weights, void *vec,
			 void *(*fiber_at)(void *vec, u64 idx),
			 void (*fiber_set_at)(void *vec, u64 idx, void *fib),
			 u64 *(*fiber_lenp_at)(void *vec, u64 idx))
{
	u32 i;
	for(i = 0; i < new_numb; i++) {
		u32 *fib;
		u64 *fib_lenp;

		fib = mem_alloc(WORDSIZE * new_weights[i]);
		if (!fib)
			return -ENOMEM;
		fiber_set_at(vec, i, fib);
		fib_lenp = fiber_lenp_at(vec, i);
		*fib_lenp = new_weights[i];
	}
	/*
	 * init freshly allocated fibers by system table
	 */
	init_fibers_by_tab(new_numb,
			   nums_bits, tab, vec, fiber_at, new_weights);

	for (i = 0; i < new_numb + 1; i++)
		assert("edward-1901",
		       new_weights[i] == *(fiber_lenp_at(vec, i)));
	return 0;
}

static void release_fibers(u32 numb, void *vec,
			   void *(*fiber_at)(void *vec, u64 idx))
{
	u32 i;

	for(i = 0; i < numb; i++) {
		u32 *fib;
		fib = fiber_at(vec, i);
		mem_free(fib);
	}
}

static int replace_fibers(u32 nums_bits, u32 *tab, u32 numb,
			  u32 *new_weights, void *vec,
			  void *(*fiber_at)(void *vec, u64 idx),
			  void (*fiber_set_at)(void *vec, u64 idx, void *fib),
			  u64 *(*fiber_lenp_at)(void *vec, u64 idx))
{
	release_fibers(numb, vec, fiber_at);
	return create_fibers(nums_bits, tab, numb, new_weights, vec,
			     fiber_at, fiber_set_at, fiber_lenp_at);
}

/*
 * balance fibers after adding a bucket
 */
static int balance_fibers_add(u32 old_numb, u32 *tab,
			      u32 *old_weights, u32 *new_weights,
			      void *vec,
			      void *(*fiber_at)(void *vec, u64 idx))
{
	int ret = 0;
	u32 i, j;
	u32 *excess = NULL;

	excess = mem_alloc(old_numb * WORDSIZE);
	if (!excess) {
		ret = ENOMEM;
		goto exit;
	}
	for (i = 0 ; i < old_numb; i++)
		excess[i] = old_weights[i] - new_weights[i];
	/*
	 * steal segments for the new fiber
	 */
	for(i = 0; i < old_numb; i++)
		for(j = 0; j < excess[i]; j++) {
			u32 *fib;
			fib = fiber_at(vec, i);
			assert("edward-1902",
			       tab[fib[new_weights[i] + j]] == i);
			tab[fib[new_weights[i] + j]] = old_numb + 1;
		}
 exit:
	if (excess)
		mem_free(excess);
	return ret;
}

/*
 * balance fibers after removing a bucket
 */
static int balance_fibers_remove(u32 old_numb, u32 *tab,
				 u32 *old_weights, u32 *new_weights,
				 u32 victim_pos,
				 void *vec,
				 void *(*fiber_at)(void *vec, u64 idx))
{
	int ret = 0;
	u32 i, j;
	u32 off = 0;
	u32 *shortage;
	u32 *victim;

	shortage = mem_alloc(old_numb * WORDSIZE);
	if (!shortage) {
		ret = ENOMEM;
		goto exit;
	}
	for(i = 0; i < victim_pos; i++)
		shortage[i] = new_weights[i] - old_weights[i];
	for(i = victim_pos + 1; i < old_numb; i++)
		shortage[i] = new_weights[i-1] - old_weights[i];
	/*
	 * distribute segments of the fiber to be removed
	 */
	victim = fiber_at(vec, victim_pos);

	for(i = 0; i < victim_pos; i++)
		for(j = 0; j < shortage[i]; j++)
			tab[victim[off ++]] = i;
	/*
	 * internal id of all partitions to the right of the victim
	 * get decremented after removal, so
	 */
	for(i = victim_pos + 1; i < old_numb; i++) {
		for(j = 0; j < new_weights[i]; j++) {
			u32 *fib;
			fib = fiber_at(vec, i);
			assert("edward-1903", tab[fib[j]] == i);
			tab[fib[j]] = i - 1;
		}
		for(j = 0; j < shortage[i]; j++) {
			tab[victim[off ++]] = i - 1;
		}
	}
 exit:
	if (shortage)
		mem_free(shortage);
	return ret;
}

/**
 * balance fibers after splitting segments with factor (1 << @fact_bits)
 * in the hash space
 */
static int balance_fibers_split(u32 numb, u32 nums_bits,
			    u32 **tabp,
			    u32 *old_weights, u32 *new_weights,
			    u32 fact_bits,
			    void *vec,
			    void *(*fiber_at)(void *vec, u64 idx),
			    void (*fiber_set_at)(void *vec, u64 idx, void *fib),
			    u64 *(*fiber_lenp_at)(void *vec, u64 idx))
{
	u32 ret = 0;
	u32 i,j,k = 0;
	u32 nums;

	u32 *new_tab = NULL;
	u32 *excess = NULL;
	u32 num_excess;
	u32 *shortage = NULL;
	u32 num_shortage;
	u32 *reloc = NULL;
	u32 num_reloc;
	u32 factor;

	assert("edward-1904", numb <= MAX_BUCKETS);
	assert("edward-1905", nums_bits + fact_bits <= MAX_SHIFT);

	nums = 1 << nums_bits;
	factor = 1 << fact_bits;

	num_excess = nums % numb;
	num_shortage = numb - num_excess;

	if (num_excess) {
		excess = mem_alloc(numb * WORDSIZE);
		if (!excess)
			goto error;

		shortage = excess + num_excess;

		for(i = 0; i < num_excess; i++)
			excess[i] = factor * old_weights[i] - new_weights[i];
		for(i = 0; i < num_shortage; i++)
			shortage[i] = new_weights[i] - factor * old_weights[i];
	}
	/*
	 * "stretch" system table with the factor @factor
	 */
	new_tab = mem_alloc(nums * factor * WORDSIZE);
	if (!new_tab) {
		ret = ENOMEM;
		goto error;
	}
	for(i = 0; i < nums; i++)
		for(j = 0; j < factor; j++)
			new_tab[i * factor + j] = (*tabp)[i];
	mem_free(*tabp);
	*tabp = NULL;

	if (!num_excess)
		/* everything is balanced */
		goto final_update;
	/*
	 * Build "stretched" fibers, which are still disbalanced
	 */
	for (i = 0; i < numb; i++)
		old_weights[i] *= factor;

	ret = replace_fibers(nums_bits + fact_bits, new_tab, numb, old_weights,
			     vec, fiber_at, fiber_set_at, fiber_lenp_at);
	if (ret)
		goto error;
	/*
	 * calculate number of segments to be relocated
	 */
	for (i = 0, num_reloc = 0; i < num_excess; i++)
		num_reloc += excess[i];
	/*
	 * allocate array of segments to be relocated
	 */
	reloc = mem_alloc(num_reloc * WORDSIZE);
	if (!reloc)
		goto error;
	/*
	 * assemble segments, which are to be relocated
	 */
	for (i = 0, k = 0; i < num_excess; i++)
		for (j = 0; j < excess[i]; j++) {
			u32 *fib;
			fib = fiber_at(vec, i);
			reloc[k++] = fib[new_weights[i] + j];
		}
	/*
	 * distribute segments
	 */
	for (i = 0, k = 0; i < num_shortage; i++)
		for (j = 0; j < shortage[i]; j++)
			new_tab[reloc[k++]] = num_excess + i;
 final_update:
	ret = replace_fibers(nums_bits + fact_bits, new_tab, numb, new_weights,
			     vec, fiber_at, fiber_set_at, fiber_lenp_at);
	if (ret)
		goto error;
	*tabp = new_tab;
	goto exit;
 error:
	if (new_tab)
		mem_free(new_tab);
 exit:
	if (excess)
		mem_free(excess);
	if (reloc)
		mem_free(reloc);
	return ret;
}

void fsw32_done(reiser4_aid *p)
{
	struct fsw32_aid *aid = &p->fsw32;

	if (aid->weights)
		mem_free(aid->weights);
	if (aid->tab)
		mem_free(aid->tab);
	mem_free(p);
}

/*
 * Allocate and initialize aid descriptor by an array of abstract buckets
 */
int fsw32_init(void *buckets,
	       u64 numb, int nums_bits,
	       struct reiser4_aid_ops *ops,
	       reiser4_aid **new)
{
	u32 i;
	int ret = -ENOMEM;
	u32 nums;
	struct fsw32_aid *aid;

	assert("edward-1906", *new == NULL);

	if (numb == 0 || nums_bits >= MAX_SHIFT)
		return -EINVAL;

	nums = 1 << nums_bits;
	if (numb >= nums)
		return -EINVAL;

	*new = mem_alloc(sizeof(**new));
	if (*new == NULL)
		return -ENOMEM;

	aid = fsw32_private(*new);

	aid->weights = mem_alloc(numb * WORDSIZE);
	if (!aid->weights)
		goto error;

	/* set weights */
	normalize_vector(numb, aid->buckets,
			 aid->ops->bucket_cap_get, nums, aid->weights);

	for (i = 0; i < numb; i++)
		assert("edward-1907",
		       aid->weights[i] == *(ops->bucket_fib_lenp(buckets, i)));

	aid->tab = mem_alloc(nums * WORDSIZE);
	if (!aid->tab) {
		ret = -ENOMEM;
		goto error;
	}
	/* set system table */
	init_tab_by_fibers(numb, aid->tab,
			   buckets, ops->bucket_fib_get, aid->weights);

	aid->numb = numb;
	aid->nums_bits = nums_bits;
	aid->buckets = buckets;
	aid->ops = ops;
	return 0;
 error:
	fsw32_done(*new);
	*new = NULL;
	return ret;
}

u64 fsw32m_lookup_bucket(reiser4_aid *data, const char *str,
			 int len, u32 seed)
{
	u32 hash;
	struct fsw32_aid *aid = fsw32_private(data);

	hash = murmur3_x86_32(str, len, seed);
	return aid->tab[hash >> (32 - aid->nums_bits)];
}

/*
 * Add a new bucket to the end of the array
 */
int fsw32_add_bucket(reiser4_aid *data, void *bucket)
{
	int ret = 0;
	u32 *new_weights;
	u32 nums;
	struct fsw32_aid *aid = fsw32_private(data);

	if (aid->numb == MAX_BUCKETS)
		return EINVAL;

	nums = 1 << aid->nums_bits;
	if (aid->numb >= nums)
		/*
		 * In this case adding a bucket is undefined operation.
		 * Suggest the caller to increase granularity (number of
		 * segments) by ->split() operation.
		 */
		return EINVAL;

	new_weights = mem_alloc((aid->numb + 1) * WORDSIZE);
	if (!new_weights) {
		ret = ENOMEM;
		goto error;
	}
	/*
	 * release old fibers
	 */
	release_fibers(aid->numb,
		       aid->buckets, aid->ops->bucket_fib_get);
	/*
	 * create new set of buckets, destroy old set
	 */
	ret = aid->ops->bucket_add(&aid->buckets, bucket);
	if (ret)
		goto error;
	/*
	 * calculate new weights.
	 * Buckets set should be already updated
	 */
	normalize_vector(aid->numb + 1, aid->buckets,
			 aid->ops->bucket_cap_get, nums, new_weights);
	/*
	 * update system table
	 */
	ret = balance_fibers_add(aid->numb, aid->tab,
				 aid->weights, new_weights,
				 aid->buckets, aid->ops->bucket_fib_get);
	if (ret)
		goto error;

	/* create new fibers */
	ret = create_fibers(aid->nums_bits, aid->tab,
			    aid->numb + 1, new_weights,
			    aid->buckets,
			    aid->ops->bucket_fib_get,
			    aid->ops->bucket_fib_set,
			    aid->ops->bucket_fib_lenp);
	if (ret)
		goto error;

	mem_free(aid->weights);
	aid->weights = new_weights;
	aid->numb += 1;
	return 0;

 error:
	if (new_weights)
		mem_free(new_weights);
	return ret;
}

/*
 * Remove a bucket at @victim_pos from the array
 */
int fsw32_remove_bucket(reiser4_aid *data, u64 victim_pos)
{
	int ret = 0;
	u32 nums;
	u32 *new_weights;
	struct fsw32_aid *aid = fsw32_private(data);

	assert("edward-1908", aid->numb >= 1);
	assert("edward-1909", aid->numb <= MAX_BUCKETS);

	if (unlikely(aid->numb == 1))
		/*
		 * Can not remove the single bucket.
		 * It is undefined and meaningless
		 */
		return EINVAL;

	nums = 1 << aid->nums_bits;
	new_weights = mem_alloc((aid->numb - 1) * WORDSIZE);
	if (!new_weights) {
		ret = ENOMEM;
		goto error;
	}
	/*
	 * release old fibers
	 */
	release_fibers(aid->numb,
		       aid->buckets, aid->ops->bucket_fib_get);
	/*
	 * delete the bucket
	 */
	ret = aid->ops->bucket_del(&aid->buckets, victim_pos);
	if (ret)
		goto error;
	/*
	 * calculate new weights.
	 * The set of buckets should be already updated
	 */
	normalize_vector(aid->numb - 1, aid->buckets,
			 aid->ops->bucket_cap_get, nums, new_weights);
	/*
	 * update system table
	 */
	ret = balance_fibers_remove(aid->numb, aid->tab,
				    aid->weights, new_weights,
				    victim_pos,
				    aid->buckets, aid->ops->bucket_fib_get);
	if (ret)
		goto error;
	/*
	 * create new fibers
	 */
	ret = create_fibers(aid->nums_bits, aid->tab,
			    aid->numb - 1, new_weights, aid->buckets,
			    aid->ops->bucket_fib_get, aid->ops->bucket_fib_set,
			    aid->ops->bucket_fib_lenp);
	if (ret)
		goto error;

	mem_free(aid->weights);
	aid->weights = new_weights;
	aid->numb -= 1;
	return 0;

 error:
	if (new_weights)
		mem_free(new_weights);
	return ret;
}

/*
 * Split hash space segments with specified factor (1 << @fact_bits)
 */
int fsw32_split(reiser4_aid *data, u32 fact_bits)
{
	int ret = 0;
	u32 *new_weights;
	u32 new_nums;
	struct fsw32_aid *aid = fsw32_private(data);

	if (aid->nums_bits + fact_bits > MAX_SHIFT)
		return EINVAL;

	new_nums = 1 << (aid->nums_bits + fact_bits);

	new_weights = mem_alloc(WORDSIZE * aid->numb);
	if (!new_weights) {
		ret = ENOMEM;
		goto error;
	}
	/*
	 * Calculate new weights
	 * The set of buckets should be already updated
	 */
	normalize_vector(aid->numb, aid->buckets,
			 aid->ops->bucket_cap_get, new_nums, new_weights);
	/*
	 * Update table of partitions
	 */
	ret = balance_fibers_split(aid->numb, aid->nums_bits,
				   &aid->tab,
				   aid->weights,
				   new_weights,
				   fact_bits,
				   aid->buckets,
				   aid->ops->bucket_fib_get,
				   aid->ops->bucket_fib_set,
				   aid->ops->bucket_fib_lenp);
	if (ret)
		goto error;
	mem_free(aid->weights);
	aid->weights = new_weights;
	aid->nums_bits += fact_bits;
	return 0;
 error:
	if (new_weights)
		mem_free(new_weights);
	return ret;
}

void tab32_pack(char *to, void *from, u64 count)
{
	u32 *cpu = (u32 *)from;

	for (; count > 0; count --) {
		put_unaligned(cpu_to_le32(*cpu), (d32 *)to);
		to += sizeof(u32);
		cpu ++;
	}
}

void tab32_unpack(void *to, char *from, u64 count)
{
	u32 *cpu = (u32 *)to;

	for (; count > 0; count --) {
		*cpu = le32_to_cpu(get_unaligned((d32 *)from));
		from += sizeof(u32);
		cpu ++;
	}
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
