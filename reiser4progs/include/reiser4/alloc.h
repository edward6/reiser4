/*
    alloc.h -- reiserfs block allocator functions.
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

extern reiserfs_alloc_t *reiserfs_alloc_open(reiserfs_format_t *format);

#ifndef ENABLE_COMPACT

extern reiserfs_alloc_t *reiserfs_alloc_create(reiserfs_format_t *format);

extern errno_t reiserfs_alloc_sync(reiserfs_alloc_t *alloc);

extern void reiserfs_alloc_mark(reiserfs_alloc_t *alloc, blk_t blk);
extern void reiserfs_alloc_dealloc(reiserfs_alloc_t *alloc, blk_t blk);
extern blk_t reiserfs_alloc_alloc(reiserfs_alloc_t *alloc);

#endif

extern errno_t reiserfs_alloc_valid(reiserfs_alloc_t *alloc, int flags);
extern void reiserfs_alloc_close(reiserfs_alloc_t *alloc);

extern count_t reiserfs_alloc_free(reiserfs_alloc_t *alloc);
extern count_t reiserfs_alloc_used(reiserfs_alloc_t *alloc);

extern int reiserfs_alloc_test(reiserfs_alloc_t *alloc, blk_t blk);

#endif

