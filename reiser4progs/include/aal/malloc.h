/*
    malloc.h -- memory allocation functions. ctualy they are memory allocation
    handlers which may be set by caller. In the allone mode they will point to
    corresponding memory allocation functions, which are used in certain alone
    application (bootloaders, etc). In the standard mode they are pointed to 
    libc memory allocation functions (malloc, free, etc).
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef MALLOC_H
#define MALLOC_H

#include <sys/types.h>

typedef void *(*aal_malloc_handler_t) (size_t);
typedef void *(*aal_realloc_handler_t) (void *, size_t);
typedef void (*aal_free_handler_t) (void *);

extern void aal_malloc_set_handler(aal_malloc_handler_t handler);
extern aal_malloc_handler_t aal_malloc_get_handler(void);
extern void *aal_malloc(size_t size);
extern void *aal_calloc(size_t size, char c);

extern void aal_realloc_set_handler(aal_realloc_handler_t handler);
extern aal_realloc_handler_t aal_realloc_get_handler(void);
extern int aal_realloc(void** old, size_t size);

extern void aal_free_set_handler(aal_free_handler_t handler);
extern aal_free_handler_t aal_free_get_handler(void);
extern void aal_free(void* ptr);

#endif

