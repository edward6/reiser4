/* A timing/debug test of the SLPC data structure.
 */

/* There are a few printfs but otherwise there should be no stdio
 * dependencies, that's just sloppy.
 */
extern int printf(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#include <stdlib.h>

/* The DOUBLE_CHECK CPP directive causes extra checking of the
 * intermediate tree state--it checks that all keys supposedly in the
 * tree can be located before and after each modification.
 */
#define DOUBLE_CHECK 0

/* The number of keys used in testing.
 */
#define LARGE_NUMBER (1<<20)

/* Typedefs for the key/data structures used in this test.
 */
typedef struct _test_key  test_key;
typedef struct _test_dat  test_dat;
typedef struct _test_pair test_pair;

struct _test_key
{
  int x;
};

struct _test_dat
{
  int y;
};

struct _test_pair
{
  test_key k;
  test_dat d;
};

/* The declaration of an SLPC data type uses these macros and defines.
 */
#define SLPC_GREATER_THAN(a,b) ((a).x >  (b).x)
#define SLPC_EQUAL_TO(a,b)     ((a).x == (b).x)

#define SLPC_PARAM_NAME   test
#define SLPC_KEY          test_key
#define SLPC_DATA         test_dat
#define SLPC_MAX_COUNT    126
#define SLPC_PAD_COUNT    0
#define SLPC_TARGET_SIZE  1024
#define SLPC_USE_SPINLOCK 1
#define SLPC_DEBUG        0

/* With these definitions, including SLPC.H defines the implementation
 * of a specific tree with fixed parameters.  All symbol names defined
 * end in SLPC_PARAM_NAME ("_test").
 */
#include "slpc.h"

/* For the rdtscll() timestamp counter
 */
#include <asm/msr.h>

typedef unsigned long long uint64;

/* Two helper functions:
 */
void die()
{
  printf ("SLPC testing abnormal failure\n");
  abort ();
}

int next_random ()
{
  return lrand48 () >> 10;
}

/* The first test checks just a few conditions: the pre- and
 * post-conditions of a few basic insertion and deletion operations
 * are checked using search.
 */
void test_basic ()
{
  SLPC_ANCHOR_test *test;

  test_key key1, key2, key3;
  test_dat dat1, dat2, dat3;

  key1.x = 42;
  key2.x = 43;
  key3.x = 44;
  dat1.y = 24;
  dat2.y = 25;
  dat2.y = 26;

  test = slpc_anchor_new_test ();

  if (slpc_insert_test     (test, & key1, & dat1) != SLPC_OKAY) die ();
  if (slpc_insert_test     (test, & key1, & dat1) != SLPC_KEY_EXISTS) die ();

  if (slpc_search_test     (test, & key1, & dat2) != SLPC_OKAY || dat1.y != dat2.y) die ();
  if (slpc_search_test     (test, & key2, & dat3) != SLPC_KEY_NOTFOUND) die ();

  if (slpc_delete_key_test (test, & key1, & dat3) != SLPC_OKAY || dat1.y != dat3.y) die ();
  if (slpc_delete_key_test (test, & key1, & dat3) != SLPC_KEY_NOTFOUND) die ();

  if (slpc_insert_test     (test, & key1, & dat1) != SLPC_OKAY) die ();
  if (slpc_delete_min_test (test, & key3, & dat2) != SLPC_OKAY || !SLPC_EQUAL_TO(key1,key3)) die ();

  slpc_anchor_free_test (test);
}

void test_1 ()
{
  SLPC_ANCHOR_test *test = slpc_anchor_new_test ();
  test_pair *pairs = SLPC_MALLOC (sizeof(test_pair) * LARGE_NUMBER);
  int i, c;
  SLPC_RESULT ret;
  test_dat d1;

  /* Timing variables */
  uint64 t1, t2, td;
  uint64 cycle_total = 0;
  int    cycle_count = 0;

  /* Result variables */
  double space_utilization;
  int    full_insert_nodes;
  int    full_insert_levels;
  uint64 cycles_per_insert;
  uint64 cycles_per_search;
  uint64 cycles_per_delete;
  int    interruptions = 0;

#if DOUBLE_CHECK
  int j;
#endif

  /* INSERTION PHASE */
  for (i = 0; i < LARGE_NUMBER; i += 1)
    {
    again:
      pairs[i].k.x = next_random ();
      pairs[i].d.y = next_random ();

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

#if DOUBLE_CHECK
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
  full_insert_nodes  = test->_node_count;
  full_insert_levels = test->_height;
  space_utilization  = 100.0 * (LARGE_NUMBER * sizeof (test_pair)) / (double) (test->_node_count * sizeof (SLPC_NODE));

  cycle_count = 0;
  cycle_total = 0;

  /* SEARCH PHASE */
  for (i = 0; i < LARGE_NUMBER; i += 1)
    {
      rdtscll (t1);
      if ((ret = slpc_search_test (test, & pairs[i].k, & d1)) != SLPC_OKAY)
	{
	  printf ("SLPC test 1 failed at i=%d key=%d NOTFOUND\n", i, pairs[i].k.x);
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
      rdtscll (t1);
      if ((ret = slpc_delete_key_test (test, & pairs[i].k, & d1)) != SLPC_OKAY)
	{
	  printf ("SLPC test 1 failed at i=%d key=%d NOTFOUND\n", i, pairs[i].k.x);
	  die ();
	}
      rdtscll (t2);

      if (d1.y != pairs[i].d.y)
	{
	  printf ("SLPC test 1 deleted wrong value at i=%d key=%d got=%d expect=%d\n", i, pairs[i].k.x, d1.y, pairs[i].d.y);
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

  slpc_anchor_free_test (test);

  printf ("NODESZ\tLOCKING\tFANOUT\tINSERT\tSEARCH\tDELETE\tNODES\tLEVELS\tSPACE\tKEYS\tPAIRSZ\n");
  printf ("%d\t%s\t%d-%d\t%qd\t%qd\t%qd\t%d\t%d\t%.0f%%\t%d\t%d\n",
	  sizeof (SLPC_NODE),
	  SLPC_USE_SPINLOCK ? "yes" : "no",
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
  test_basic ();

  test_1 ();

  return 0;
}
