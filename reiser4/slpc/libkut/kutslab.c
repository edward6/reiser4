/* -*-Mode: C;-*-
 * $Id$
 * Author: Joshua MacDonald
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "kutslab.h"

#include <stdlib.h>
#include <assert.h>

static void
kut_add_free (KUT_SLAB *slab, void *object)
{
  KUT_FREE *free, *last;

 again:

  for (free = slab->_free, last = NULL; free; last = free, free = free->_next)
    {
      if (free->_count < free->_max)
	{
	  free->_ptrs[free->_count++] = object;
	  object = NULL;
	  break;
	}
    }

  if (object != NULL)
    {
      /* No free ptr space */
      free = (KUT_FREE*) malloc (sizeof (KUT_FREE));

      free->_next  = slab->_free;
      slab->_free  = free;
      free->_count = 0;
      free->_max   = sizeof (free->_ptrs) / sizeof (void*);
      goto again;
    }
  else if (last != NULL)
    {
      /* Adjust head of free ptr list */
      assert (free != NULL && last->_next == free);
      last->_next = free->_next->_next;
      free->_next = slab->_free;
      slab->_free = free;
    }
}

static void
kut_slab_grow (KUT_SLAB *slab)
{
  int i;
  char     *page = malloc (KUT_PAGE_SIZE);
  KUT_PLNK *plnk = (KUT_PLNK*) malloc (sizeof (KUT_PLNK));

  plnk->_next  = slab->_pages;
  slab->_pages = plnk;

  for (i = 0; i < slab->_perpage; i += 1, page += slab->_obsize)
    {
      kut_add_free (slab, page);

      if (slab->_ctor)
	{
	  slab->_ctor  (page);
	}
    }
}

void*
kut_slab_new (KUT_SLAB *slab)
{
  void* object = NULL;
  KUT_FREE *free, *last;

#if KUT_USE_LOCKS
  spin_lock (& slab->_lock);
#endif

 again:

  for (free = slab->_free, last = NULL; free; last = free, free = free->_next)
    {
      if (free->_count > 0)
	{
	  object = free->_ptrs[--free->_count];
	  break;
	}
    }

  if (object == NULL)
    {
      /* No free items. */
      kut_slab_grow (slab);
      goto again;
    }
  else if (last != NULL)
    {
      /* Move first free page to head of list. */
      assert (free != NULL && last->_next == free);
      last->_next = free->_next->_next;
      free->_next = slab->_free;
      slab->_free = free;
    }

#if KUT_USE_LOCKS
  spin_unlock (& slab->_lock);
#endif

  return object;
}

void
kut_slab_free (KUT_SLAB    *slab,
	       void        *object)
{
#if KUT_USE_LOCKS
  spin_lock    (& slab->_lock);
#endif

  kut_add_free (slab, object);

  if (slab->_dtor && !(slab->_flags & KUT_CTOR_ONCE))
    {
      slab->_dtor (object);
    }

#if KUT_USE_LOCKS
  spin_unlock  (& slab->_lock);
#endif
}

void
kut_slab_destroy (KUT_SLAB *slab)
{
  while (slab->_pages)
    {
      KUT_PLNK *tmp = slab->_pages;

      slab->_pages = tmp->_next;

      free (tmp->_page);
      free (tmp);
    }

  while (slab->_free)
    {
      KUT_FREE *tmp = slab->_free;

      slab->_free = tmp->_next;

      free (tmp);
    }

  free (slab);
}

KUT_SLAB*
kut_slab_create  (const char  *name,
		  int          obsize,
		  int          flags,
		  KUT_CTOR    *ctor,
		  KUT_DTOR    *dtor)
{
  KUT_SLAB *slab = (KUT_SLAB*) malloc (sizeof (KUT_SLAB));

  assert (sizeof (KUT_FREE) == KUT_PAGE_SIZE);

  slab->_name    = name;
  slab->_flags   = flags;
  slab->_obsize  = obsize;
  slab->_perpage = KUT_PAGE_SIZE / obsize;
  slab->_ctor    = ctor;
  slab->_dtor    = dtor;
  slab->_pages   = NULL;
  slab->_free    = NULL;

#if KUT_USE_LOCKS
  spin_lock_init (& slab->_lock);
#endif

  kut_slab_grow (slab);

  return slab;
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
 /
