/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "zipf.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

/* Read this page if you want to make sense of Zipf and Pareto
 * distributions:
 *
 * http://www.parc.xerox.com/istl/groups/iea/papers/ranking/ranking.html#ap2
 */

/* These define a SLPC for looking up elements of a Zipf distribution.
 * DOUBLE->UINT32. */

#define SLPC_GREATER_THAN(a,b)  ((a) >  (b))
#define SLPC_GREATER_EQUAL(a,b) ((a) >= (b))
#define SLPC_EQUAL_TO(a,b)      ((a) == (b))
#define SLPC_USE_SPINLOCK 0
#define SLPC_MAX_COUNT    20
#define SLPC_TARGET_SIZE  256
#define SLPC_PARAM_NAME   zipf
#define SLPC_KEY          double
#define SLPC_DATA         u_int32_t
#define SLPC_PAD_COUNT    8

#include <slpc/slpc.h>

#define KUT_LOCK_POSIX 1
#include <libkut/kutlock.h>

struct _zipf_table
{
  SLPC_ANCHOR_zipf  _probs;
};

static SLPC_SLAB           *_zipfslab;
static struct drand48_data  _rand_data;
static spinlock_t           _rand_lock;

/****************************************************************************************
				   RANDOM DISTRIBUTIONS
 ****************************************************************************************/

void
sys_rand_init (void)
{
  spin_lock_init (& _rand_lock);

  srand48_r (SYS_RAND_SEED, & _rand_data);

  _zipfslab = SLPC_SLAB_CREATE ("zipf");
}

u_int32_t
sys_lrand (u_int32_t max)
{
  u_int32_t d;
  spin_lock   (&_rand_lock);
  lrand48_r   (& _rand_data, (long*) & d);
  spin_unlock (& _rand_lock);
  return d % max;
}

double
sys_drand ()
{
  double d;
  spin_lock   (& _rand_lock);
  drand48_r   (& _rand_data, & d);
  spin_unlock (& _rand_lock);
  return d;
}

u_int32_t
sys_erand (u_int32_t mean)
{
  double lfact = log (1.0 / sys_drand ());

  return (u_int32_t) ((double) (mean * lfact) + 0.5);
}

u_int32_t*
sys_rand_permutation (u_int32_t elts)
{
  u_int32_t *perm = (u_int32_t*) malloc (sizeof (u_int32_t) * elts);
  u_int32_t  i;

  /* Fill an array of consecutive ints. */
  for (i = 0; i < elts; i += 1)
    {
      perm[i] = i;
    }

  /* Permute the array. */
  for (i = elts-1; i > 1; i -= 1)
    {
      u_int32_t tmp  = perm[i];
      u_int32_t pick = sys_lrand (i);
      assert (pick < i);
      perm[i]    = perm[pick];
      perm[pick] = tmp;
    }

  return perm;
}

/****************************************************************************************
				    ZIPF DISTRIBUTIONS
 ****************************************************************************************/

static zipf_table*
zipf_compute_table_int (u_int32_t elts, double alpha, int *perm)
{
  zipf_table *table = (zipf_table*) malloc (sizeof (zipf_table));
  double      sum, tot, p_i;
  u_int32_t   elt_i, i;

  assert (_zipfslab != NULL);
  slpc_anchor_init_zipf (& table->_probs, _zipfslab);

  /* Compute the normalization factor. */
  for (sum = 0.0, i = 1; i <= elts; i += 1)
    {
      sum += pow (i, alpha);
    }

  /* Populate probability table with permuted elts */
  for (tot = 1.0, i = 1; i <= elts; i += 1, tot -= p_i)
    {
      p_i   = pow (i, alpha) / sum;
      elt_i = perm ? perm[elts-i] : elts-i;

      slpc_insert_zipf (& table->_probs, & tot, & elt_i);
    }

  return table;
}

zipf_table*
zipf_permute_table (u_int32_t elts, double alpha)
{
  zipf_table    *table;
  u_int32_t     *perm;

  perm  = sys_rand_permutation   (elts);
  table = zipf_compute_table_int (elts, alpha, perm);

  free (perm);

  return table;
}

zipf_table*
zipf_compute_table (u_int32_t elts, double alpha)
{
  return zipf_compute_table_int (elts, alpha, NULL);
}

u_int32_t
zipf_choose_cdf (zipf_table *table,
		 double      prob,
		 double     *cdf)
{
  SLPC_RESULT res;
  u_int32_t   elt;

  assert (prob >= 0.0 && prob < 1.0);

  res = slpc_search_lub_zipf (& table->_probs, & prob, & elt);

  if (cdf != NULL)
    {
      (*cdf) = prob;
    }

  assert (res != SLPC_KEY_NOTFOUND);

  return elt;
}

u_int32_t
zipf_choose_elt (zipf_table *table)
{
  return zipf_choose_cdf (table, sys_drand (), NULL);
}
