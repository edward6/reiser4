/* -*-Mode: C;-*-
 * $Id$
 * Author: Joshua MacDonald
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* A multi-processor test of the SLPC data structure.
 */

/* There are a few printfs but otherwise there should be no stdio
 * dependencies, that's just sloppy.
 */
extern int printf(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Typedefs for the key/data structures used in this test.
 */
typedef struct _test_key  test_key;
typedef struct _test_dat  test_dat;
typedef struct _counter_t counter_t;
typedef struct _result_t  result_t;

struct _test_key
{
  int x;
};

struct _test_dat
{
  int y;
};

struct _result_t
{
  uint   skips;

  uint   insert_count;
  uint   delete_count;
  uint   search_count;
  uint   append_count;

  uint   insert_success;
  uint   delete_success;
  uint   search_success;
  uint   append_success;

  u_int64_t insert_cycle;
  u_int64_t delete_cycle;
  u_int64_t search_cycle;
  u_int64_t append_cycle;
};

/* The declaration of an SLPC data type uses these macros and defines.
 */
#define SLPC_GREATER_THAN(a,b)  ((a).x >  (b).x)
#define SLPC_GREATER_EQUAL(a,b) ((a).x >= (b).x)
#define SLPC_EQUAL_TO(a,b)      ((a).x == (b).x)

/* Define these in the MAkefile
 */
#if 0
#define SLPC_USE_SPINLOCK   1 /* 1 or 2 */
#endif

#define SLPC_PARAM_NAME     test
#define SLPC_KEY            test_key
#define SLPC_DATA           test_dat
#define SLPC_PAD_COUNT      0
#define SLPC_COUNT_RESTARTS 1
#define SLPC_COUNT_KEYS     1
#define SLPC_DEBUG          0

/* These numbers are consistently best for concurrent access on
 * x86. */
#define SLPC_MAX_COUNT      30
#define SLPC_TARGET_SIZE    256
#define SLPC_SKIP_LIMIT     250000

/* The Srinivasan/Carey paper uses the following workload:
 *
 * - Initially the tree has all the ODD keys from 1..MAX_INIT_KEY
 *   inserted into the tree
 * - Define probabilities of the four operations: insert, delete,
 *   search, append
 * - Define the multi-programming level from 1 to the number of
 *   CPUs.  Spinlocks don't work well when processes can expect
 *   to be preemted.
 * - Closed queueing model means each process executes a loop
 *   issuing operations with no "think time".
 * - Insert even keys, delete odd keys, search for any key.
 * - Measure throughput, response time for each operation category
 *   as a function of various configurations.
 */
int     TEST_PROCESSORS   = 1;
int     TEST_OPERATIONS   = (1<<24);
int     TEST_STARTKEYS;

/* Accumulated probabilities for each operation */
double  TEST_INSERT_PROB = 0.25;
double  TEST_DELETE_PROB = 0.5;
double  TEST_SEARCH_PROB = 0.75;
double  TEST_APPEND_PROB = 1.0;

#include "slpc.h"

/* An atomic counter for counting operations.
 */
struct _counter_t
{
  uint         counter;
  spinlock_t   lock;
};

static counter_t _opcount = { 0, SPIN_LOCK_UNLOCKED };
static counter_t _maxkey  = { 0, SPIN_LOCK_UNLOCKED };

/* For the rdtscll() timestamp counter
 */
#include <asm/msr.h>

/* For POSIX threads: testing at user level
 */
#include <pthread.h>

spinlock_t seed_lock = SPIN_LOCK_UNLOCKED;

/* A few helper functions:
 */
void die()
{
  printf ("SLPC testing abnormal failure\n");
  abort ();
}

uint counter_post_inc (counter_t *c)
{
  uint r;
  spin_lock (& c->lock);
  r = c->counter++;
  spin_unlock (& c->lock);
  return r;
}

uint next_any_key (struct drand48_data *randd)
{
  unsigned long x;

  if ((lrand48_r (randd, (long*) & x)) < 0)
    {
      printf ("lrand48_r failed\n");
      die ();
    }

  x %= _maxkey.counter;

  return x;
}

void* test_1_handler (void* arg);

void test_1 ()
{
  SLPC_ANCHOR_test test_s, *test = & test_s;
  SLPC_SLAB   *slab = SLPC_SLAB_CREATE ("test_1");
  SLPC_RESULT ret;
  test_key    key;
  test_dat    dat;
  pthread_t   tids[TEST_PROCESSORS];
  result_t    result_total;
  int         init_levels;
  int         init_keys;
  int         end_levels;
  int         end_keys;
  u_int64_t      start, stop;
  int i;

  slpc_anchor_init_test (test, slab);

  memset (& result_total, 0, sizeof (result_total));

  for (i = 0; i < _maxkey.counter; i += 2)
    {
      key.x = i;
      dat.y = i;

      if ((ret = slpc_insert_test (test, & key, & dat)) != SLPC_OKAY)
	{
	  printf ("SLPC initial setup failed\n");
	  die ();
	}
    }

  init_levels = test->_height;
  init_keys   = test->_ks._key_count;

  rdtscll (start);
  for (i = 0; i < TEST_PROCESSORS; i += 1)
    {
      pthread_create (& tids[i], NULL, test_1_handler, (void*) test);
    }

  for (i = 0; i < TEST_PROCESSORS; i += 1)
    {
      result_t *one_result;

      pthread_join (tids[i], (void**) & one_result);

      result_total.skips += one_result->skips;

      result_total.insert_count += one_result->insert_count;
      result_total.search_count += one_result->search_count;
      result_total.delete_count += one_result->delete_count;
      result_total.append_count += one_result->append_count;

      result_total.insert_success += one_result->insert_success;
      result_total.search_success += one_result->search_success;
      result_total.delete_success += one_result->delete_success;
      result_total.append_success += one_result->append_success;

      result_total.insert_cycle += one_result->insert_cycle;
      result_total.search_cycle += one_result->search_cycle;
      result_total.delete_cycle += one_result->delete_cycle;
      result_total.append_cycle += one_result->append_cycle;
    }
  rdtscll (stop);

  end_levels = test->_height;
  end_keys   = test->_ks._key_count;

  SLPC_SLAB_DESTROY (slab);

  printf ("PROCS\tNODESZ\tTOTCYC\tINSERT\tSEARCH\tDELETE\tAPPEND\tLEVELS\tOPS\tRESTART\tSKIPS\tI/S/D/A%%\tSUCCESS\t\tLOCKING\tSTARTKEYS\n");
  printf ("%d\t%d\t%.2fe9\t%qd\t%qd\t%qd\t%qd\t" "%d/%d\t%.2fe6\t%d\t%d\t%.0f/%.0f/%.0f/%.0f\t%.0f/%.0f/%.0f/%.0f\t%s\t%d\n",
	  TEST_PROCESSORS,
	  sizeof (SLPC_NODE),
	  1.0 * (stop - start) / 1e9,
	  result_total.insert_count ? (result_total.insert_cycle / result_total.insert_count) : 0,
	  result_total.search_count ? (result_total.search_cycle / result_total.search_count) : 0,
	  result_total.delete_count ? (result_total.delete_cycle / result_total.delete_count) : 0,
	  result_total.append_count ? (result_total.append_cycle / result_total.append_count) : 0,
	  init_levels,
	  end_levels,
	  1.0 * TEST_OPERATIONS / 1e6,
	  test->_rs._restarts,
	  result_total.skips,
	  100.0 * TEST_INSERT_PROB,
	  100.0 * (TEST_SEARCH_PROB - TEST_DELETE_PROB),
	  100.0 * (TEST_DELETE_PROB - TEST_INSERT_PROB),
	  100.0 * (TEST_APPEND_PROB - TEST_SEARCH_PROB),
	  result_total.insert_count ? (100.0 * result_total.insert_success / result_total.insert_count) : 0,
	  result_total.search_count ? (100.0 * result_total.search_success / result_total.search_count) : 0,
	  result_total.delete_count ? (100.0 * result_total.delete_success / result_total.delete_count) : 0,
	  result_total.append_count ? (100.0 * result_total.append_success / result_total.append_count) : 0,
	  (SLPC_USE_SPINLOCK == 1 ? "ex" : "rw"),
	  TEST_STARTKEYS);
}

void* test_1_handler (void* arg)
{
  SLPC_ANCHOR_test *test   = (SLPC_ANCHOR_test*) arg;
  result_t         *result = calloc (1, sizeof (result_t));
  struct drand48_data randd;

  u_int64_t start, stop, diff;
  uint   c;

  test_key key;
  test_dat dat;

  SLPC_RESULT ret;

  spin_lock   (& seed_lock);
  srand48_r   (lrand48 (), & randd);
  spin_unlock (& seed_lock);

  while ((c = counter_post_inc (& _opcount) < TEST_OPERATIONS))
    {
      double d;

      if (drand48_r (& randd, & d) < 0)
	{
	  printf ("drand48_r failed\n");
	  die ();
	}

      if (d < TEST_INSERT_PROB)
	{
	  key.x = dat.y = next_any_key (& randd);

	  rdtscll (start);
	  if ((ret = slpc_insert_test (test, & key, & dat)) != SLPC_OKAY && ret != SLPC_KEY_EXISTS)
	    {
	      printf ("SLPC insert failed\n");
	      die ();
	    }
	  rdtscll (stop);
	  diff = stop - start;
	  if (diff < SLPC_SKIP_LIMIT)
	    {
	      result->insert_count += 1;
	      result->insert_cycle += diff;
	      result->insert_success += (ret == SLPC_OKAY);
	    }
	  else
	    {
	      result->skips += 1;
	    }
	}
      else if (d < TEST_DELETE_PROB)
	{
	  key.x = next_any_key (& randd);

	  rdtscll (start);
	  if ((ret = slpc_delete_key_test (test, & key, & dat)) != SLPC_OKAY && ret != SLPC_KEY_NOTFOUND)
	    {
	      printf ("SLPC delete failed\n");
	      die ();
	    }
	  rdtscll (stop);
	  diff = stop - start;
	  if (diff < SLPC_SKIP_LIMIT)
	    {
	      result->delete_count += 1;
	      result->delete_cycle += diff;
	      result->delete_success += (ret == SLPC_OKAY);
	    }
	  else
	    {
	      result->skips += 1;
	    }
	}
      else if (d < TEST_SEARCH_PROB)
	{
	  key.x = next_any_key (& randd);

	  rdtscll (start);
	  if ((ret = slpc_search_test (test, & key, & dat)) != SLPC_OKAY && ret != SLPC_KEY_NOTFOUND)
	    {
	      printf ("SLPC search failed\n");
	      die ();
	    }
	  rdtscll (stop);
	  diff = stop - start;
	  if (diff < SLPC_SKIP_LIMIT)
	    {
	      result->search_count += 1;
	      result->search_cycle += diff;
	      result->search_success += (ret == SLPC_OKAY);
	    }
	  else
	    {
	      result->skips += 1;
	    }
	}
      else
	{
	  key.x = counter_post_inc (& _maxkey);

	  rdtscll (start);
	  if ((ret = slpc_insert_test (test, & key, & dat)) != SLPC_OKAY && ret != SLPC_KEY_NOTFOUND)
	    {
	      printf ("SLPC append failed\n");
	      die ();
	    }
	  rdtscll (stop);
	  diff = stop - start;
	  if (diff < SLPC_SKIP_LIMIT)
	    {
	      result->append_count += 1;
	      result->append_cycle += diff;
	      result->append_success += (ret == SLPC_OKAY);
	    }
	  else
	    {
	      result->skips += 1;
	    }
	}
    }

  return result;
}

int main (int argc, char **argv)
{
  double total;

  if (argc != 8)
    {
      printf ("usage: testslpc2 PROCS STARTKEYS OPERATIONS INSERT%% SEARCH%% DELETE%% APPEND%%\n");
      die ();
    }

  TEST_PROCESSORS  = atoi (argv[1]);
  TEST_STARTKEYS   = atoi (argv[2]);
  TEST_OPERATIONS  = atoi (argv[3]);
  TEST_INSERT_PROB = atoi (argv[4]) / 100.0;
  TEST_SEARCH_PROB = atoi (argv[5]) / 100.0;
  TEST_DELETE_PROB = atoi (argv[6]) / 100.0;
  TEST_APPEND_PROB = atoi (argv[7]) / 100.0;

  _maxkey.counter = TEST_STARTKEYS * 2; /* Start with half the key space, double start keys. */

#if 0
  printf ("workload mix: i%.2f s%.2f d%.2f a%.2f\n",
	  TEST_INSERT_PROB,
	  TEST_SEARCH_PROB,
	  TEST_DELETE_PROB,
	  TEST_APPEND_PROB);
#endif

  TEST_DELETE_PROB += TEST_INSERT_PROB;
  TEST_SEARCH_PROB += TEST_DELETE_PROB;
  TEST_APPEND_PROB += TEST_SEARCH_PROB;

  total = TEST_APPEND_PROB - 1.0;

  if (total > 0.00001 && total < -0.00001)
    {
      printf ("workload mix does not add up to 100%%: %f %f %f %f %f\n",
	      TEST_INSERT_PROB,
	      TEST_DELETE_PROB,
	      TEST_SEARCH_PROB,
	      TEST_APPEND_PROB,
	      TEST_APPEND_PROB - 1);
      die ();
    }

  test_1 ();

  return 0;
}


/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
