/* -*-Mode: C;-*-
 * $Id: testslpc1.c,v 1.9 2002/03/18 20:46:45 flx Exp $
 * Author: Joshua MacDonald
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* A timing/debug test of the SLPC data structure.
 */

/* There are a few printfs but otherwise there should be no stdio
 * dependencies, that's just sloppy.
 */
extern int printf(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>


/* The number of keys used in testing.
 */
#define LARGE_NUMBER (1<<20)

/* Typedefs for the key/data structures used in this test.
 */
typedef struct _test_pair test_pair;

typedef int test_key;
typedef int test_dat;

struct _test_pair
{
  test_key k;
  test_dat d;
};

/* The declaration of an SLPC data type uses these macros and defines.
 */
#define SLPC_GREATER_THAN(a,b)  ((a) >  (b))
#define SLPC_GREATER_EQUAL(a,b) ((a) >= (b))
#define SLPC_EQUAL_TO(a,b)      ((a) == (b))

/* Define these in the Makefile */
#if 0
#define SLPC_USE_SPINLOCK
#define SLPC_MAX_COUNT
#define SLPC_TARGET_SIZE
#endif

#define SLPC_PARAM_NAME   test
#define SLPC_KEY          test_key
#define SLPC_DATA         test_dat
#define SLPC_PAD_COUNT    0
#define SLPC_COUNT_NODES  1

/* The DOUBLE_CHECK CPP directive causes extra checking of the
 * intermediate tree state--it checks that all keys supposedly in the
 * tree can be located before and after each modification.
 */
#define SLPC_DEBUG   0
#define SLPC_DEBUG3  0
#define PROGRESS     0

#define DOUBLE_CHECK 0
#define SLPC_DEBUG2  0

/* With these definitions, including SLPC.H defines the implementation
 * of a specific tree with fixed parameters.  All symbol names defined
 * end in SLPC_PARAM_NAME ("_test").
 */
#include "slpc.h"

/* For the rdtscll() timestamp counter
 */
#include <asm/msr.h>

#include <stdio.h>

/* Two helper functions:
 */
void die()
{
  printf ("SLPC testing abnormal failure\n");
  abort ();
}

int next_random ()
{
  return lrand48 ();
}

void test_1 ()
{
  SLPC_SLAB *slab = SLPC_SLAB_CREATE ("test_1");
  SLPC_ANCHOR_test test_s, *test = & test_s;
  test_pair *pairs = malloc (sizeof(test_pair) * LARGE_NUMBER);
  int i, c;
  SLPC_RESULT ret;
  test_dat d1;

  /* Timing variables */
  u_int64_t t1, t2, td;
  u_int64_t cycle_total = 0;
  int    cycle_count = 0;

  /* Result variables */
  double space_utilization;
  int    full_insert_nodes;
  int    full_insert_levels;
  u_int64_t cycles_per_insert;
  u_int64_t cycles_per_search;
  u_int64_t cycles_per_delete;
  int    interruptions = 0;

#if DOUBLE_CHECK
  int j;
#endif

  slpc_anchor_init_test (test, slab);

  /* INSERTION PHASE */
  for (i = 0; i < LARGE_NUMBER; i += 1)
    {
#if PROGRESS
      if ((i % 1000) == 0)
	{
	  fprintf (stderr, "i");
	}
#endif
    again:
      pairs[i].k = next_random ();
      pairs[i].d = next_random ();

      rdtscll (t1);
      if ((ret = slpc_insert_test (test, & pairs[i].k, & pairs[i].d)) != SLPC_OKAY)
	{
	  if (ret == SLPC_KEY_EXISTS)
	    {
	      goto again;
	    }

	  printf ("SLPC test 1 insertion failed\n");
	  die ();
	}
      rdtscll (t2);

      /* Compute difference in time, test for preemption.  I have
       * observed that the average for LARGE_NUMBER=(1<<20) that the
       * average insertion is under 3000 cycles and a single split is
       * about 15000 cycles.  Every once in a while you see an
       * insertion taking 10M cycles, which indicates a context
       * switch.  On my 850MHz laptop that means an insertion averages
       * about 3.5us. */
      td = t2 - t1;

      if (td <= 1000000)
	{
	  cycle_total += td;
	  cycle_count += 1;
	}
      else
	{
	  interruptions += 1;
	}

#if SLPC_DEBUG2
      slpc_debug_structure_test (test);
#endif

#if DOUBLE_CHECK2
      for (j = 0; j <= i; j += 1)
	{
	  if (slpc_search_test (test, & pairs[j].k, & d1) != SLPC_OKAY || d1 != pairs[j].d)
	    {
	      printf ("SLPC test 1 search failed--can't find recently inserted key\n");
	      die ();
	    }
	}
#endif
    }

  cycles_per_insert  = cycle_total / cycle_count;
  full_insert_nodes  = atomic_read (& test->_node_count);
  full_insert_levels = test->_height;
  space_utilization  = 100.0 * (LARGE_NUMBER * sizeof (test_pair)) / (double) (atomic_read (& test->_node_count) * sizeof (SLPC_NODE));

  cycle_count = 0;
  cycle_total = 0;

  /* SEARCH PHASE */
  for (i = 0; i < LARGE_NUMBER; i += 1)
    {
#if PROGRESS
      if ((i % 1000) == 0)
	{
	  fprintf (stderr, "s");
	}
#endif
      rdtscll (t1);
      if ((ret = slpc_search_test (test, & pairs[i].k, & d1)) != SLPC_OKAY)
	{
	  printf ("SLPC test 1 failed at i=%d key=%d NOTFOUND\n", i, pairs[i].k);
	  die ();
	}
      rdtscll (t2);

      td = t2 - t1;

      if (td <= 1000000)
	{
	  cycle_total += td;
	  cycle_count += 1;
	}
      else
	{
	  interruptions += 1;
	}
    }

  cycles_per_search = cycle_total / cycle_count;

  cycle_count = 0;
  cycle_total = 0;

  /* DELETION PHASE */
  for (c = 0, i = LARGE_NUMBER-1; i >= 0; i -= 1, c += 1)
    {
#if PROGRESS
      if ((c % 1000) == 0)
	{
	  fprintf (stderr, "d");
	}
#endif      
      rdtscll (t1);
      if ((ret = slpc_delete_key_test (test, & pairs[i].k, & d1)) != SLPC_OKAY)
	{
	  printf ("SLPC test 1 failed at i=%d key=%d NOTFOUND\n", i, pairs[i].k);
	  die ();
	}
      rdtscll (t2);

      if (d1 != pairs[i].d)
	{
	  printf ("SLPC test 1 deleted wrong value at i=%d key=%d got=%d expect=%d\n", i, pairs[i].k, d1, pairs[i].d);
	  die ();
	}

      td = t2 - t1;

      if (td <= 1000000)
	{
	  cycle_total += td;
	  cycle_count += 1;
	}
      else
	{
	  interruptions += 1;
	}

#if SLPC_DEBUG2
      slpc_debug_structure_test (test);
#endif

#if DOUBLE_CHECK
      for (j = 0; j < i; j += 1)
	{
	  if (slpc_search_test (test, & pairs[j].k, & d1) != SLPC_OKAY || d1 != pairs[j].d)
	    {
	      printf ("SLPC test 1 search failed--can't find inserted key #%d after %dth deletion (of key #%d)\n", j, c, i);
	      die ();
	    }
	}
#endif
    }

  cycles_per_delete = cycle_total / cycle_count;

  SLPC_SLAB_DESTROY (slab);

  printf ("NODESZ\tLOCKING\tFANOUT\tINSERT\tSEARCH\tDELETE\tNODES\tLEVELS\tSPACE\tKEYS\tPAIRSZ\n");
  printf ("%d\t%s\t%d-%d\t%qd\t%qd\t%qd\t%d\t%d\t%.0f%%\t%d\t%d\n",
	  sizeof (SLPC_NODE),
	  SLPC_USE_SPINLOCK > 1 ? "rw" : (SLPC_USE_SPINLOCK > 0 ? "ex" : "no"),
	  SLPC_MIN_COUNT,
	  SLPC_MAX_COUNT,
	  cycles_per_insert,
	  cycles_per_search,
	  cycles_per_delete,
	  full_insert_nodes,
	  full_insert_levels,
	  space_utilization,
	  LARGE_NUMBER,
	  sizeof (test_pair));
}

int main()
{
  /*test_basic ();*/

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
