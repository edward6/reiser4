/*
    alloc.h -- reiser4 block allocator functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC_H
#define ALLOC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/filesystem.h>

extern reiser4_alloc_t *reiser4_alloc_open(reiser4_format_t *format, 
    count_t len);

#ifndef ENABLE_COMPACT

extern reiser4_alloc_t *reiser4_alloc_create(reiser4_format_t *format, 
    count_t len);

extern errno_t reiser4_alloc_sync(reiser4_alloc_t *alloc);

extern void reiser4_alloc_mark(reiser4_alloc_t *alloc, blk_t blk);
extern void reiser4_alloc_dealloc(reiser4_alloc_t *alloc, blk_t blk);
extern blk_t reiser4_alloc_alloc(reiser4_alloc_t *alloc);

#endif

extern errno_t reiser4_alloc_valid(reiser4_alloc_t *alloc);
extern void reiser4_alloc_close(reiser4_alloc_t *alloc);

extern count_t reiser4_alloc_free(reiser4_alloc_t *alloc);
extern count_t reiser4_alloc_used(reiser4_alloc_t *alloc);

extern int reiser4_alloc_test(reiser4_alloc_t *alloc, blk_t blk);

#endif

