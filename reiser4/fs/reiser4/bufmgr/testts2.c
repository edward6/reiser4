/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "tshash.h"

#include <libkut/kutmem.h>

#include <stdlib.h>
#include <assert.h>

typedef struct _test_obj  test_obj;
typedef struct _test_key1 test_key1;
typedef u_int32_t         test_key2;

TS_HASH_DECLARE(test1,test_obj);
TS_HASH_DECLARE(test2,test_obj);

struct _test_key1
{
  double d;
};

struct _test_obj
{
  test_key1        _key1;
  test_key2        _key2;

  test1_hash_link  _link1;
  test2_hash_link  _link2;
};

#define HASH1_SIZE (100U)

#define HASH2_BITS (7U)
#define HASH2_SIZE (1U<<HASH2_BITS)
#define HASH2_MASK (HASH2_SIZE-1U)

u_int32_t key1_hash (test_key1 const* key)
{
  return (u_int32_t) key->d % HASH1_SIZE;
}

int key1_equal (test_key1 const *a, test_key1 const *b)
{
  return a->d == b->d;
}

u_int32_t key2_hash (test_key2 const* key)
{
  return *key & HASH2_MASK;
}

int key2_equal (test_key2 const *a, test_key2 const *b)
{
  return (*a) == (*b);
}

TS_HASH_DEFINE(test1,test_obj,test_key1,_key1,_link1,key1_hash,key1_equal);
TS_HASH_DEFINE(test2,test_obj,test_key2,_key2,_link2,key2_hash,key2_equal);

#define TESTNUM 256

int main()
{
  int i;
  test_obj *objs = (test_obj*) malloc (sizeof (test_obj) * TESTNUM);

  test1_hash_table hash1;
  test2_hash_table hash2;

  test1_hash_init (& hash1, HASH1_SIZE);
  test2_hash_init (& hash2, HASH2_SIZE);

  for (i = 0; i < TESTNUM; i += 1)
    {
    unique:
      objs[i]._key1.d = drand48 ();
      objs[i]._key2   = lrand48 ();

      if (test1_hash_find (& hash1, & objs[i]._key1) ||
	  test2_hash_find (& hash2, & objs[i]._key2))
	{
	  goto unique;
	}

      test1_hash_insert (& hash1, & objs[i]);
      test2_hash_insert (& hash2, & objs[i]);
    }

  for (i = 0; i < TESTNUM; i += 1)
    {
      if ((& objs[i] != test1_hash_find (& hash1, & objs[i]._key1)) ||
	  (& objs[i] != test2_hash_find (& hash2, & objs[i]._key2)))
	{
	  abort ();
	}
    }

  for (i = 0; i < TESTNUM; i += 1)
    {
      if ((! test1_hash_remove (& hash1, & objs[i])) ||
	  (! test2_hash_remove (& hash2, & objs[i])))
	{
	  abort ();
	}
    }

  for (i = 0; i < TESTNUM; i += 1)
    {
      if (test1_hash_find (& hash1, & objs[i]._key1) ||
	  test2_hash_find (& hash2, & objs[i]._key2))
	{
	  abort ();
	}
    }

  for (i = 0; i < HASH1_SIZE; i += 1)
    {
      assert (hash1._table[i] == NULL);
    }

  for (i = 0; i < HASH2_SIZE; i += 1)
    {
      assert (hash2._table[i] == NULL);
    }

  return 0;
}
