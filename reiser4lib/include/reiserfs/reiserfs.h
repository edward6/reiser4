/*
 	reiserfs.h -- the central libreiserfs include.
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef REISERFS_H
#define REISERFS_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

#include "exception.h"
	
typedef void *(*libreiserfs_malloc_handler_t) (size_t);
typedef void *(*libreiserfs_realloc_handler_t) (void *, size_t);
typedef void (*libreiserfs_free_handler_t) (void *);

extern int libreiserfs_get_max_interface_version(void);
extern int libreiserfs_get_min_interface_version(void);
extern const char *libreiserfs_get_version(void);

extern void libreiserfs_malloc_set_handler(libreiserfs_malloc_handler_t handler);
extern libreiserfs_malloc_handler_t libreiserfs_malloc_handler(void);
extern void *libreiserfs_malloc(size_t size);
extern void *libreiserfs_calloc(size_t size, char c);

extern void libreiserfs_realloc_set_handler(libreiserfs_realloc_handler_t handler);
extern libreiserfs_realloc_handler_t libreiserfs_realloc_handler(void);
extern int libreiserfs_realloc(void** old, size_t size);

extern void libreiserfs_free_set_handler(libreiserfs_free_handler_t handler);
extern libreiserfs_free_handler_t libreiserfs_free_handler(void);
extern void libreiserfs_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif

