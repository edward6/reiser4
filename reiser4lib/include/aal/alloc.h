/*
	alloc.h -- memory allocation functions.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef ALLOC_H
#define ALLOC_H

#include <sys/types.h>

typedef void *(*aal_malloc_handler_t) (size_t);
typedef void *(*aal_realloc_handler_t) (void *, size_t);
typedef void (*aal_free_handler_t) (void *);

extern void aal_malloc_set_handler(aal_malloc_handler_t handler);
extern aal_malloc_handler_t aal_malloc_handler(void);
extern void *aal_malloc(size_t size);
extern void *aal_calloc(size_t size, char c);

extern void aal_realloc_set_handler(aal_realloc_handler_t handler);
extern aal_realloc_handler_t aal_realloc_handler(void);
extern int aal_realloc(void** old, size_t size);

extern void aal_free_set_handler(aal_free_handler_t handler);
extern aal_free_handler_t aal_free_handler(void);
extern void aal_free(void* ptr);

#endif

