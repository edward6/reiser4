/*
	alloc.h -- memory allocation functions.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef ALLOC_H
#define ALLOC_H

#include <sys/types.h>

typedef void *(*agl_malloc_handler_t) (size_t);
typedef void *(*agl_realloc_handler_t) (void *, size_t);
typedef void (*agl_free_handler_t) (void *);

extern void agl_malloc_set_handler(agl_malloc_handler_t handler);
extern agl_malloc_handler_t agl_malloc_handler(void);
extern void *agl_malloc(size_t size);
extern void *agl_calloc(size_t size, char c);

extern void agl_realloc_set_handler(agl_realloc_handler_t handler);
extern agl_realloc_handler_t agl_realloc_handler(void);
extern int agl_realloc(void** old, size_t size);

extern void agl_free_set_handler(agl_free_handler_t handler);
extern agl_free_handler_t agl_free_handler(void);
extern void agl_free(void* ptr);

#endif

