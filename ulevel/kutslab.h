/* -*-Mode: C;-*-
 * $Id$
 * Author: Joshua MacDonald
 * Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

/* This file, part of KUTLIB (Kernel-User Test Library), provides an
 * interface somewhat compatible with the kmem_slab_cache.  This
 * cannot be optimized in the same way and it never shrinks. */

#ifndef __KUTSLAB_H__
#define __KUTSLAB_H__

#ifndef KUT_USE_LOCKS
#define KUT_USE_LOCKS 1
#endif

#if KUT_USE_LOCKS
#include "kutlock.h"
#endif

#define KUT_PAGE_SIZE (1<<14)

typedef void (KUT_CTOR) (void* ob);
typedef void (KUT_DTOR) (void* ob);

typedef struct _KUT_SLAB KUT_SLAB;
typedef struct _KUT_PLNK KUT_PLNK;
typedef struct _KUT_FREE KUT_FREE;

typedef enum
{
  KUT_CTOR_ONCE = (1 << 0),
} KUT_FLAGS;

struct _KUT_SLAB
{
  const char *_name;
  int         _flags;
  int         _obsize;
  int         _perpage;
  KUT_CTOR   *_ctor;
  KUT_DTOR   *_dtor;
  KUT_PLNK   *_pages;
  KUT_FREE   *_free;

#if KUT_USE_LOCKS
  spinlock_t  _lock;
#endif
};

struct _KUT_PLNK
{
  KUT_PLNK  *_next;
  void      *_page;
};

struct _KUT_FREE
{
  KUT_FREE  *_next;
  int        _count;
  int        _max;
  void      *_ptrs[(KUT_PAGE_SIZE - 2*sizeof (int) - sizeof (KUT_FREE*)) / sizeof (void*)];
};

extern KUT_SLAB* kut_slab_create  (const char  *name,
				   int          obsize,
				   int          flags,
				   KUT_CTOR    *ctor,
				   KUT_DTOR    *dtor);
extern void*     kut_slab_new     (KUT_SLAB    *slab);
extern void      kut_slab_free    (KUT_SLAB    *slab,
				   void        *object);
extern void      kut_slab_destroy (KUT_SLAB    *slab);

#endif /* __KUTSLAB_H__ */
