/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "zipf.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

int
main (int argc, char **argv)
{
  zipf_table *zipf;
  u_int32_t   elts, c, twenty;
  double      alpha, p, twenty_prob;

  if (argc != 3)
    {
      printf ("usage: testzipf1 ELEMENTS ALPHA\n");
      return 2;
    }

  sys_rand_init ();

  elts   = atoi (argv[1]);
  alpha  = atof (argv[2]);
  zipf   = zipf_compute_table (elts, alpha);
  twenty = elts / 5;

  printf ("RANK\t\tCUM. DIST. FUNC.\tPROBABILITY\n");

  for (c = 0, p = 0.0; p < 1.0; c += 1)
    {
      double      last_p = p;
      u_int32_t   elt_i;

      /* Of course this could simply scan the list, but I'd rather
       * test the search_lub function. */
      elt_i = zipf_choose_cdf (zipf, p, & p);

      if (c == twenty)
	{
	  twenty_prob = p;
	}

      assert (c == elt_i);

      printf ("%d\t\t%.9f\t\t%.9f\n", c, p, p - last_p);
    }

  printf ("%.0f%% of requests hit 20%% of the data\n", twenty_prob * 100.0);

  assert (c == elts);

  return 0;
}
