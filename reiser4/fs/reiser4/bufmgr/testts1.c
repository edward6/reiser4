/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "tslist.h"

/* This is less of a test on the code as it is a test of the C
 * preprocessor wickedness.  */
#include <stdlib.h>
#include <assert.h>

/* Declare two separate list classes. */
TS_LIST_DECLARE(test1);

TS_LIST_DECLARE(test2);

/* Define a single object type with separate links for each list. */
typedef struct _test_obj test_obj;
struct _test_obj
{
  test1_list_link  _link1;
  test2_list_link  _link2;
};

/* Define both list classes. */
TS_LIST_DEFINE(test1, test_obj, _link1);
TS_LIST_DEFINE(test2, test_obj, _link2);

#define TESTNUM 128

int main()
{
  test1_list_head  head1;
  test2_list_head  head2;
  test_obj        *objs = (test_obj*) malloc (sizeof (test_obj) * TESTNUM);
  int i;

  test1_list_init (& head1);
  test2_list_init (& head2);

  assert (test1_list_empty (& head1));
  assert (test2_list_empty (& head2));

  for (i = 0; i < TESTNUM; i += 1)
    {
      test1_list_push_front (& head1, & objs[i]);
      test2_list_push_back  (& head2, & objs[i]);
    }

  assert (! test1_list_empty (& head1));
  assert (! test2_list_empty (& head2));

  for (i = 0; i < TESTNUM; i += 1)
    {
      test_obj *it = test1_list_pop_back (& head1);

      assert (it == & objs[i]);
    }

  for (i = 0; i < TESTNUM; i += 1)
    {
      test_obj *it = test2_list_pop_front (& head2);

      assert (it == & objs[i]);
    }

  assert (test1_list_empty (& head1));
  assert (test2_list_empty (& head2));

  return 0;
}
