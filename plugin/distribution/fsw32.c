/*
  Balanced Fiber-Striped array with Weights.
  Inventor, Author: Eduard O. Shishkin
  Implementation over 32-bit hash.

  Copyright (c) 2014-2018 Eduard O. Shishkin

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

#define MAX_SHIFT      (31)
#define MAX_BUCKETS    (1u << MAX_SHIFT)
#define MIN_NUMS_BITS  (10)
#define WORDSIZE       (sizeof(u32))
#define FSW32_PRECISE  (0)

static inline void *mem_alloc(u64 len)
{
	void *result = reiser4_vmalloc(len);
	if (result)
		memset(result, 0, len);
	return result;
}

static inline void mem_free(void *p)
{
	vfree(p);
}

static inline u32 **cur_tabp(struct fsw32_aid *aid)
{
	return aid->ops->tabp();
}

static inline u32 *cur_tab(struct fsw32_aid *aid)
{
	return *cur_tabp(aid);
}

static inline struct fsw32_aid *fsw32_private(reiser4_aid *aid)
{
	return &aid->fsw32;
}

static void init_fibers_by_tab(u32 numb,
			       u32 nums_bits,
			       u32 *tab,
			       bucket_t *vec,
			       void *(*fiber_at)(bucket_t *vec, u64 idx),
			       u32 (*id2idx)(u64 id),
			       u32 *weights)
{
	u32 i;
	u32 nums = 1 << nums_bits;

	for(i = 0; i < numb; i++)
		weights[i] = 0;

	for(i = 0; i < nums; i++) {
		u32 *fib;

		fib = fiber_at(vec, id2idx(tab[i]));
		fib[(weights[id2idx(tab[i])])++] = i;
	}
}

static void init_tab_by_fibers(u32 numb,
			       u32 *tab,
			       bucket_t *vec,
			       void *(*fiber_at)(bucket_t *vec, u64 idx),
			       u64 (*idx2id)(u32 idx),
			       u32 *weights)
{
	u32 i, j;

	for(i = 0; i < numb; i++)
		for (j = 0; j < weights[i]; j++) {
			u32 *fib;
			fib = fiber_at(vec, i);
			tab[fib[j]] = idx2id(i);
		}
}

u32 *init_tab_from_scratch(u32 *weights, u32 numb, u32 nums_bits,
			   u64 (*idx2id)(u32 idx))
{
	u32 i, j, k;
	u32 *tab;
	u32 nums = 1 << nums_bits;

	tab = mem_alloc(nums * WORDSIZE);
	if (!tab)
		return NULL;
	for (i = 0, k = 0; i < numb; i++)
		for (j = 0; j < weights[i]; j++)
			tab[k++] = idx2id(i);
	return tab;
}

/*
 * Construct a "similar" integer vector, which have
 * specified sum @val of its components
 */
static void calibrate(u64 num, u64 val,
		      bucket_t *vec, u64 (*vec_el_get)(bucket_t *vec, u64 idx),
		      void *ret, u64 (*ret_el_get)(void *ret, u64 idx),
		      void (ret_el_set)(void *ret, u64 idx, u64 value))
{
	u64 i;
	u64 rest;
	u64 sum_scaled = 0;
	u64 sum_not_scaled = 0;

	for (i = 0; i < num; i++)
		sum_not_scaled += vec_el_get(vec, i);
	for (i = 0; i < num; i++) {
		u64 q;
		u64 result;

		q = val * vec_el_get(vec, i);
		result = div64_u64(q, sum_not_scaled);
		ret_el_set(ret, i, result);
		sum_scaled += result;
	}
	rest = val - sum_scaled;
	/*
	 * Don't modify this: it will be a format change!
	 */
	for (i = 0; i < rest; i++)
		ret_el_set(ret, i, ret_el_get(ret, i) + 1);
	return;
}

static u64 array32_el_get(void *array, u64 idx)
{
	return ((u32 *)array)[idx];
}

static void array32_el_set(void *array, u64 idx, u64 val)
{
	((u32 *)array)[idx] = val;
}

static u64 array64_el_get(void *array, u64 idx)
{
	return ((u64 *)array)[idx];
}

static void array64_el_set(void *array, u64 idx, u64 val)
{
	((u64 *)array)[idx] = val;
}

static void calibrate32(u32 num, u32 val, bucket_t *vec,
			u64 (*vec_el_get)(bucket_t *vec, u64 idx),
			u32 *ret)
{
	calibrate(num, val, vec, vec_el_get,
		  ret, array32_el_get, array32_el_set);
}

static void calibrate64(u64 num, u64 val, bucket_t *vec,
			u64 (*vec_el_get)(bucket_t *vec, u64 idx),
			u64 *ret)
{
	calibrate(num, val, vec, vec_el_get,
		  ret, array64_el_get, array64_el_set);
}

int create_systab(u32 nums_bits, u32 **tab,
			 u32 numb, u32 *weights, bucket_t *vec,
			 void *(*fiber_at)(bucket_t *vec, u64 idx),
			 u64 (*idx2id)(u32 idx))
{
	u32 nums = 1 << nums_bits;

	*tab = mem_alloc(nums * WORDSIZE);
	if (!tab)
		return -ENOMEM;

	init_tab_by_fibers(numb, *tab, vec, fiber_at, idx2id, weights);
	return 0;
}

static int clone_systab(struct fsw32_aid *aid)
{
	assert("edward-2169", aid != NULL);
	assert("edward-2170", cur_tab(aid) != NULL);
	assert("edward-2171", aid->tab == NULL);

	aid->tab = mem_alloc((1 << aid->nums_bits) * WORDSIZE);
	if (!aid->tab)
		return ENOMEM;
	memcpy(aid->tab, cur_tab(aid), (1 << aid->nums_bits) * WORDSIZE);
	return 0;
}

static void free_cloned_systab(struct fsw32_aid *aid)
{
	if (aid->tab) {
		mem_free(aid->tab);
		aid->tab = NULL;
	}
}

static int create_fibers(u32 nums_bits, u32 *tab,
			 u32 new_numb, u32 *new_weights, bucket_t *vec,
			 void *(*fiber_at)(bucket_t *vec, u64 idx),
			 void (*fiber_set_at)(bucket_t *vec, u64 idx, void *fib),
			 u64 *(*fiber_lenp_at)(bucket_t *vec, u64 idx),
			 u32 (*id2idx)(u64 id))
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
	init_fibers_by_tab(new_numb,
			   nums_bits, tab, vec, fiber_at, id2idx, new_weights);

	for (i = 0; i < new_numb; i++)
		assert("edward-1901",
		       new_weights[i] == *(fiber_lenp_at(vec, i)));
	return 0;
}

#if REISER4_DEBUG
void print_fiber(u32 id, bucket_t *vec,
		 void *(*fiber_at)(bucket_t *vec, u64 idx),
		 u64 *(*fiber_lenp_at)(bucket_t *vec, u64 idx))
{
	u32 i;
	u32 *fib = fiber_at(vec, id);
	u32 fib_len = *fiber_lenp_at(vec, id);

	printk("fiber %d (len %d):", id, fib_len);
	for (i = 0; i < fib_len; i++)
                printk("%d", fib[i]);
	printk("end of fiber %d", id);
	return;
}
#endif

static void release_fibers(u32 numb, bucket_t *vec,
			   void *(*fiber_at)(bucket_t *vec, u64 idx),
			   void (*fiber_set_at)(bucket_t *vec, u64 idx,
						void *fib))
{
	u32 i;

	for(i = 0; i < numb; i++) {
		u32 *fib;
		fib = fiber_at(vec, i);
		mem_free(fib);
		fiber_set_at(vec, i, NULL);
	}
}

static int replace_fibers(u32 nums_bits, u32 *tab,
			  u32 old_numb, u32 new_numb,
			  u32 *new_weights, bucket_t *vec,
			  void *(*fiber_at)(bucket_t *vec, u64 idx),
			  void (*fiber_set_at)(bucket_t *vec, u64 idx, void *fib),
			  u64 *(*fiber_lenp_at)(bucket_t *vec, u64 idx),
			  u32 (*id2idx)(u64 id))
{
	release_fibers(old_numb, vec, fiber_at, fiber_set_at);
	return create_fibers(nums_bits, tab, new_numb, new_weights, vec,
			     fiber_at, fiber_set_at, fiber_lenp_at, id2idx);
}

/**
 * If @new is true, then a new bucket has been inserted at @target_pos.
 * @vec points to a new set of buckets
 */
static int balance_fibers_inc(struct fsw32_aid *aid,
			      u32 new_numb, u32 *tab,
			      u32 *old_weights, u32 *new_weights,
			      u32 target_pos,
			      bucket_t *vec,
			      void *(*fiber_at)(bucket_t *vec, u64 idx),
			      u64 (*idx2id)(u32 idx),
			      bucket_t new)
{
	int ret = 0;
	u32 i, j;
	u32 *excess = NULL;

	excess = mem_alloc(new_numb * WORDSIZE);
	if (!excess) {
		ret = ENOMEM;
		goto exit;
	}
	aid->excess = excess;

	for (i = 0; i < target_pos; i++)
		excess[i] = old_weights[i] - new_weights[i];

	for(i = target_pos + 1; i < new_numb; i++) {
		if (new)
			excess[i] = old_weights[i-1] - new_weights[i];
		else
			excess[i] = old_weights[i] - new_weights[i];
	}
	assert("edward-1910", excess[target_pos] == 0);
	/*
	 * steal segments of all fibers to the left of target_pos
	 */
	for(i = 0; i < target_pos; i++)
		for(j = 0; j < excess[i]; j++) {
			u32 *fib;
			fib = fiber_at(vec, i);

			assert("edward-1902",
			       tab[fib[new_weights[i] + j]] == idx2id(i));

			tab[fib[new_weights[i] + j]] = idx2id(target_pos);
		}
	/*
	 * steal segments of all fibers to the right of target_pos
	 */
	for(i = target_pos + 1; i < new_numb; i++)
		if (new && FSW32_PRECISE) {
			/*
			 * FSW modification when idx2id() and id2idx
			 * are identical functions.
			 * After inserting a new bucket, internal IDs
			 * of all buckets to the right of @target_pos
			 * (in the new set of buckets!) get incremented,
			 * thus system table needs corrections
			 */
			for(j = 0; j < new_weights[i]; j++) {
				u32 *fib;
				fib = fiber_at(vec, i);
				assert("edward-1911", tab[fib[j]] == i - 1);
				tab[fib[j]] = i;
			}
			for(j = 0; j < excess[i]; j++) {
				u32 *fib;
				fib = fiber_at(vec, i);
				assert("edward-1912",
				       tab[fib[new_weights[i] + j]] == i - 1);
				tab[fib[new_weights[i] + j]] = target_pos;
			}
		} else {
			for(j = 0; j < new_weights[i]; j++) {
				u32 *fib;
				fib = fiber_at(vec, i);
				assert("edward-1913", tab[fib[j]] == idx2id(i));
			}
			for(j = 0; j < excess[i]; j++) {
				u32 *fib;
				fib = fiber_at(vec, i);
				assert("edward-1914",
				       tab[fib[new_weights[i] + j]] == idx2id(i));
				tab[fib[new_weights[i] + j]] = idx2id(target_pos);
			}
		}
 exit:
	if (excess)
		mem_free(excess);
	return ret;
}

/**
 * @removeme (if not NULL) points to the bucket, which is to be
 * removed from @vec, that points to the old array of buckets.
 * @target_pos is index (in @vec) of bucket to remove (shrink).
 */
static int balance_fibers_dec(struct fsw32_aid *aid,
			      u32 old_numb, u32 *tab,
			      u32 *old_weights, u32 *new_weights,
			      u32 target_pos,
			      bucket_t *vec,
			      void *(*fiber_at)(bucket_t *vec, u64 idx),
			      void *(*fiber_of)(bucket_t bucket),
			      u64 (*idx2id)(u32 idx),
			      bucket_t removeme)
{
	int ret = 0;
	u32 i, j;
	u32 off_in_target = 0;
	u32 *shortage;
	u32 *target;

	assert("edward-2193",
	       ergo(removeme != NULL, vec[target_pos] == removeme));

	shortage = mem_alloc(old_numb * WORDSIZE);
	if (!shortage) {
		ret = ENOMEM;
		goto exit;
	}
	aid->shortage = shortage;

	for(i = 0; i < target_pos; i++)
		shortage[i] = new_weights[i] - old_weights[i];

	for(i = target_pos + 1; i < old_numb; i++) {
		if (removeme)
			shortage[i] = new_weights[i-1] - old_weights[i];
		else
			shortage[i] = new_weights[i]   - old_weights[i];
	}
	assert("edward-1915", shortage[target_pos] == 0);

	if (removeme) {
		target = fiber_of(removeme);
		off_in_target = 0;
	} else {
		target = fiber_at(vec, target_pos);
		off_in_target =
			old_weights[target_pos] - new_weights[target_pos];
	}
	/*
	 * distribute segments among all fibers to the left of target_pos
	 */
	for(i = 0; i < target_pos; i++)
		for(j = 0; j < shortage[i]; j++) {
			assert("edward-1916",
			       tab[target[off_in_target]] == idx2id(target_pos));
			tab[target[off_in_target ++]] = idx2id(i);
		}
	/*
	 * distribute segments among all fibers to the right of target_pos
	 */
	for(i = target_pos + 1; i < old_numb; i++) {
		if (removeme && FSW32_PRECISE) {
			/*
			 * FSW modification when idx2id() and id2idx
			 * are identical functions.
			 * Internal ID of all buckets to the right of
			 * target_pos get decremented, so that system
			 * table needs corrections
			 */
			for(j = 0; j < old_weights[i]; j++) {
				u32 *fib;
				fib = fiber_at(vec, i);
				assert("edward-1903", tab[fib[j]] == i);
				tab[fib[j]] = i - 1;
			}
			for(j = 0; j < shortage[i]; j++) {
				assert("edward-1917",
				       tab[target[off_in_target]] == target_pos);
				tab[target[off_in_target ++]] = i - 1;
			}
		}
		else {
			for(j = 0; j < old_weights[i]; j++) {
				u32 *fib;
				fib = fiber_at(vec, i);
				assert("edward-1903", tab[fib[j]] == idx2id(i));
			}
			for(j = 0; j < shortage[i]; j++) {
				assert("edward-1918",
				       tab[target[off_in_target]] ==
				       idx2id(target_pos));
				tab[target[off_in_target ++]] = idx2id(i);
			}
		}
	}
 exit:
	if (shortage)
		mem_free(shortage);
	return ret;
}

/**
 * Fix up system table after splitting segments with factor (1 << @fact_bits)
 */
static int balance_fibers_spl(u32 numb, u32 nums_bits,
			      u32 **tabp,
			      u32 *old_weights, u32 *new_weights,
			      u32 fact_bits,
			      void *vec,
			      void *(*fiber_at)(bucket_t *vec, u64 idx),
			      void (*fiber_set_at)(bucket_t *vec,
						   u64 idx, void *fib),
			      u64 *(*fiber_lenp_at)(bucket_t *vec, u64 idx),
			      u32 (*id2idx)(u64 id), u64 (*idx2id)(u32 idx))
{
	u32 ret = 0;
	u32 i,j,k = 0;
	u32 nums;

	u32 *tab = NULL;
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
	 * "stretch" system table with the @factor
	 */
	tab = mem_alloc(nums * factor * WORDSIZE);
	if (!tab) {
		ret = ENOMEM;
		goto error;
	}
	for(i = 0; i < nums; i++)
		for(j = 0; j < factor; j++)
			tab[i * factor + j] = (*tabp)[i];
	mem_free(*tabp);
	*tabp = NULL;

	if (!num_excess)
		/* everything is balanced */
		goto release;
	/*
	 * Build "stretched" fibers, which are still disbalanced
	 */
	for (i = 0; i < numb; i++)
		old_weights[i] *= factor;

	ret = replace_fibers(nums_bits + fact_bits, tab,
			     numb, numb, old_weights, vec,
			     fiber_at, fiber_set_at, fiber_lenp_at, id2idx);
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
			tab[reloc[k++]] = idx2id(num_excess + i);
 release:
	release_fibers(numb, vec, fiber_at, fiber_set_at);
	*tabp = tab;
	goto exit;
 error:
	if (tab)
		mem_free(tab);
 exit:
	if (excess)
		mem_free(excess);
	if (reloc)
		mem_free(reloc);
	return ret;
}

void donev_fsw32(reiser4_aid *raid)
{
	struct fsw32_aid *aid;

	assert("edward-1920", raid != NULL);

	aid = fsw32_private(raid);

	if (aid->weights != NULL) {
		mem_free(aid->weights);
		aid->weights = NULL;
	}
	if (aid->buckets) {
		kfree(aid->buckets);
		aid->buckets = NULL;
	}
}

/**
 * Release AID descriptor allocated for regular operations.
 * Normally it is called at umount time.
 */
void doner_fsw32(reiser4_aid *raid)
{
	struct fsw32_aid *aid = fsw32_private(raid);

	if (!aid->ops)
		return;
	if (!cur_tab(aid))
		return;
	mem_free(cur_tab(aid));
	*cur_tabp(aid) = NULL;
}

/**
 * Set newly created distribution table to @target
 */
void replace_fsw32(reiser4_aid *raid, void **target)
{
	struct fsw32_aid *aid = fsw32_private(raid);

	assert("edward-2236", target != NULL);
	assert("edward-2237", *target == NULL);

	*target = aid->tab;
	aid->tab = NULL;
}

/**
 * Free system table specified by @tab
 */
void free_fsw32(void *tab)
{
	assert("edward-2238", tab != NULL);
	mem_free(tab);
}

/**
 * Initialize distribution context for regular file operations
 */
int initr_fsw32(reiser4_aid *raid, struct bucket_ops *ops,
		int numb, int nums_bits)
{
	struct fsw32_aid *aid = fsw32_private(raid);

	aid->ops = ops;

	if (cur_tab(aid))
		return 0;

	if (nums_bits < MIN_NUMS_BITS) {
		warning("edward-1953",
			"Invalid minimal number of bricks (%llu). "
			"It should be not less than %llu",
			1ull << nums_bits, 1ull << MIN_NUMS_BITS);
		return -EINVAL;
	}
	*cur_tabp(aid) = mem_alloc((1 << nums_bits) * sizeof(u32));
	if (*cur_tabp(aid) == NULL)
		return -ENOMEM;

	aid->numb = numb;
	aid->nums_bits = nums_bits;

	return 0;
}

/**
 * Initialize distribution context for volume operations
 *
 * @buckets: set of abstract buckets;
 * @ops: operations to access the buckets;
 * @raid: AID descriptor to initialize.
 */
int initv_fsw32(bucket_t *vec,
		u64 numb, int nums_bits,
		struct bucket_ops *ops,
		reiser4_aid *raid)
{
	int ret = -ENOMEM;
	u32 nums;
	struct fsw32_aid *aid;

	if (numb == 0 || nums_bits >= MAX_SHIFT)
		return -EINVAL;

	nums = 1 << nums_bits;
	if (numb >= nums)
		return -EINVAL;

	aid = fsw32_private(raid);

	assert("edward-2172", aid->tab == NULL);
	assert("edward-1922", aid->weights == NULL);

	aid->buckets = vec;
	aid->ops = ops;

	aid->weights = mem_alloc(numb * WORDSIZE);
	if (!aid->weights)
		goto error;

	calibrate32(numb, nums, vec, ops->cap_at, aid->weights);

	if (!cur_tab(aid)) {
		u32 i;
		assert("edward-2201", numb == 1);
		ret = initr_fsw32(raid, ops, numb, nums_bits);
		if (ret)
			goto error;
		for (i = 0; i < nums; i++)
			cur_tab(aid)[i] = ops->idx2id(0);
	}
	assert("edward-2173", cur_tab(aid) != NULL);

	ret = create_fibers(nums_bits, cur_tab(aid),
			    numb, aid->weights, vec,
			    ops->fib_at,
			    ops->fib_set_at,
			    ops->fib_lenp_at,
			    ops->id2idx);
	if (ret)
		goto error;
	return 0;
 error:
	doner_fsw32(raid);
	donev_fsw32(raid);
	return ret;
}

u64 lookup_fsw32m(reiser4_aid *raid, const char *str,
		  int len, u32 seed, void *tab)
{
	u32 hash;
	struct fsw32_aid *aid = fsw32_private(raid);

	hash = murmur3_x86_32(str, len, seed);
	return ((u32 *)tab)[hash >> (32 - aid->nums_bits)];
}

/**
 * Increase capacity of an array.
 * If @new != NULL, then the whole bucket @new has been inserted
 * to the array at @target_pos
 */
int inc_fsw32(reiser4_aid *raid, u64 target_pos, bucket_t new)
{
	int ret = 0;
	u32 *new_weights;
	u32 old_numb, new_numb, nums;
	struct fsw32_aid *aid = fsw32_private(raid);

	new_numb = old_numb = aid->numb;
	if (new) {
		if (old_numb == MAX_BUCKETS)
			return EINVAL;
		aid->ops->insert_bucket(aid->buckets, new,
					old_numb, target_pos);
		new_numb ++;
	}
	nums = 1 << aid->nums_bits;
	if (new_numb >= nums)
		return EINVAL;

	new_weights = mem_alloc(new_numb * WORDSIZE);
	if (!new_weights) {
		ret = ENOMEM;
		goto error;
	}
	aid->new_weights = new_weights;

	ret = clone_systab(aid);
	if (ret)
		goto error;

	calibrate32(new_numb, nums,
		    aid->buckets, aid->ops->cap_at, new_weights);
	ret = balance_fibers_inc(aid,
				 new_numb, aid->tab,
				 aid->weights, new_weights, target_pos,
				 aid->buckets, aid->ops->fib_at,
				 aid->ops->idx2id, new);
	if (ret)
		goto error;

	release_fibers(old_numb, aid->buckets,
		       aid->ops->fib_at, aid->ops->fib_set_at);

	mem_free(aid->weights);
	aid->weights = new_weights;
	aid->numb = new_numb;

	return 0;
 error:
	if (new_weights)
		mem_free(new_weights);
	free_cloned_systab(aid);
	return ret;
}

/**
 * Check if there is enough space on remaining buckets for successful
 * completion of a bucket operation.
 *
 * @numb: number of buckets upon succesfull completion.
 * @occ: total amount of space occupied on all buckets
 */
static int check_space(reiser4_aid *raid, u64 numb, u64 occ)
{
	u64 i;
	int ret = 0;
	u64 *vec_new_occ;
	struct fsw32_aid *aid = fsw32_private(raid);
	bucket_t *vec = aid->buckets;

	/*
	 * For each bucket: calculate how much space will be
	 * occupied on the bucket after successful completion
	 * of the volume operation and compare it with the
	 * bucket's capacity
	 */
	vec_new_occ = mem_alloc(numb * sizeof(u64));
	if (!vec_new_occ)
		return -ENOMEM;

	calibrate64(numb, occ, vec, aid->ops->cap_at, vec_new_occ);

	for (i = 0; i < numb; i++) {
#if REISER4_DEBUG
		notice("edward-2145",
		       "Brick %llu: data capacity: %llu, min required: %llu",
		       i, aid->ops->cap_at(vec, i), vec_new_occ[i]);
#endif
		if (aid->ops->cap_at(vec, i) < vec_new_occ[i]) {
			warning("edward-2070",
	"Not enough data capacity (%llu) of brick %llu (required %llu)",
				aid->ops->cap_at(vec, i),
				i,
				vec_new_occ[i]);
			ret = -ENOSPC;
			break;
		}
	}
	mem_free(vec_new_occ);
	return ret;
}

/**
 * Decrease capacity of an array of buckets.
 * If @removeme is not NULL, then the whole bucket at @target_pos
 * is to be removed.
 */
int dec_fsw32(reiser4_aid *raid, u64 target_pos, bucket_t removeme)
{
	int ret = 0;
	u32 nums;
	u32 old_numb, new_numb;
	u32 *new_weights = NULL;
	struct fsw32_aid *aid = fsw32_private(raid);

	assert("edward-1908", aid->numb >= 1);
	assert("edward-1909", aid->numb <= MAX_BUCKETS);
	assert("edward-1927", aid->numb > 1);

	new_numb = old_numb = aid->numb;
	if (removeme) {
		new_numb --;
		aid->ops->remove_bucket(aid->buckets, aid->numb, target_pos);
	}
	ret = check_space(raid, new_numb, aid->ops->space_occupied());
	if (ret)
		goto error;

	nums = 1 << aid->nums_bits;
	new_weights = mem_alloc(new_numb * WORDSIZE);
	if (!new_weights) {
		ret = ENOMEM;
		goto error;
	}
	aid->new_weights = new_weights;

	ret = clone_systab(aid);
	if (ret)
		goto error;

	calibrate32(new_numb, nums,
		    aid->buckets, aid->ops->cap_at, new_weights);

	if (removeme)
		aid->ops->insert_bucket(aid->buckets, removeme,
					new_numb, target_pos);

	ret = balance_fibers_dec(aid,
				 old_numb, aid->tab,
				 aid->weights, new_weights, target_pos,
				 aid->buckets, aid->ops->fib_at,
				 aid->ops->fib_of, aid->ops->idx2id,
				 removeme);
	if (ret)
		goto error;

	release_fibers(new_numb,
		       aid->buckets, aid->ops->fib_at,
		       aid->ops->fib_set_at);
	release_fibers(1,
		       &removeme, aid->ops->fib_at,
		       aid->ops->fib_set_at);

	mem_free(aid->weights);
	aid->weights = new_weights;
	aid->numb = new_numb;
	return 0;
 error:
	if (new_weights)
		mem_free(new_weights);
	free_cloned_systab(aid);
	return ret;
}

int spl_fsw32(reiser4_aid *raid, u32 fact_bits)
{
	int ret = 0;
	u32 *new_weights;
	u32 new_nums;
	struct fsw32_aid *aid = fsw32_private(raid);

	if (aid->nums_bits + fact_bits > MAX_SHIFT)
		return EINVAL;

	new_nums = 1 << (aid->nums_bits + fact_bits);

	new_weights = mem_alloc(WORDSIZE * aid->numb);
	if (!new_weights) {
		ret = ENOMEM;
		goto error;
	}
	calibrate32(aid->numb, new_nums,
		    aid->buckets, aid->ops->cap_at, new_weights);
	ret = balance_fibers_spl(aid->numb, aid->nums_bits,
				 &aid->tab,
				 aid->weights,
				 new_weights,
				 fact_bits,
				 aid->buckets,
				 aid->ops->fib_at,
				 aid->ops->fib_set_at,
				 aid->ops->fib_lenp_at,
				 aid->ops->id2idx,
				 aid->ops->idx2id);
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

void pack_fsw32(reiser4_aid *raid, char *to, u64 src_off, u64 count)
{
	u64 i;
	u32 *src;
	struct fsw32_aid *aid = fsw32_private(raid);

	assert("edward-1923", to != NULL);
	assert("edward-1924", aid->tab != NULL);

	src = aid->tab + src_off;

	for (i = 0; i < count; i++) {
		put_unaligned(cpu_to_le32(*src), (d32 *)to);
		to += sizeof(u32);
		src ++;
	}
}

void unpack_fsw32(reiser4_aid *raid, char *from, u64 dst_off, u64 count)
{
	u64 i;
	u32 *dst;
	struct fsw32_aid *aid = fsw32_private(raid);

	assert("edward-1925", from != NULL);
	assert("edward-1926", cur_tab(aid) != NULL);

	dst = cur_tab(aid) + dst_off;

	for (i = 0; i < count; i++) {
		*dst = le32_to_cpu(get_unaligned((d32 *)from));
		from += sizeof(u32);
		dst ++;
	}
}

void dump_fsw32(reiser4_aid *raid, char *to, u64 offset, u32 size)
{
	struct fsw32_aid *aid = fsw32_private(raid);
	u32 *tab = cur_tab(aid);

	memcpy(to, tab + offset, size);
}

bucket_t *get_buckets_fsw32(reiser4_aid *raid)
{
	struct fsw32_aid *aid = fsw32_private(raid);

	return aid->buckets;
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
