/*
    malloc.c -- memory allocation functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
    Checking whether allone mode is in use. If so, initializes memory working
    handlers as NULL, because application that is use libreiser4 and libaal must
    set it especialy.
*/
#ifndef ENABLE_COMPACT

#include <stdlib.h>
static aal_malloc_handler_t malloc_handler = malloc;
static aal_realloc_handler_t realloc_handler = realloc;
static aal_free_handler_t free_handler = free;
#else
static aal_malloc_handler_t malloc_handler = NULL;
static aal_realloc_handler_t realloc_handler = NULL;
static aal_free_handler_t free_handler = NULL;

#endif

void aal_malloc_set_handler(aal_malloc_handler_t handler) {
    malloc_handler = handler;
}

aal_malloc_handler_t aal_malloc_get_handler(void) {
    return malloc_handler;
}

/*
    The wrapper for malloc function. It checks for result memory allocation and
    if it failed then reports about this.
*/
void *aal_malloc(size_t size) {
    void *mem;

    /* 
	We are using simple printf function instead of exception, because exception
	initialization is needed correctly worked memory allocation handler.
    */
    if (!malloc_handler) {
	aal_printf("Fatal: Invalid \"malloc\" handler.\n");
	return NULL;
    }

    /* The same as previous one */
    if (!(mem = malloc_handler(size))) {
	aal_printf("Fatal: Out of memory.\n");
	return NULL;
    }
    return mem;
}

void *aal_calloc(size_t size, char c) {
    void *mem;

    if (!(mem = aal_malloc(size)))
	return NULL;

    aal_memset(mem, c, size);
    return mem;
}

void aal_realloc_set_handler(aal_realloc_handler_t handler) {
    realloc_handler = handler;
}

aal_realloc_handler_t aal_realloc_get_handler(void) {
    return realloc_handler;
}

/*
    The wrapper for realloc function. It checks for result memory allocation and
    if it failed then reports about this.
*/
int aal_realloc(void** old, size_t size) {
    void *mem;

    if (!realloc_handler) {
	aal_printf("Fatal: Invalid \"realloc\" handler.\n");
	return 0;
    }

    if (!(mem = (void *)realloc_handler(*old, size))) {
	aal_printf("Fatal: Out of memory.\n");
	return 0;
    }
    *old = mem;
    return 1;
}

void aal_free_set_handler(aal_free_handler_t handler) {
    free_handler = handler;
}

aal_free_handler_t aal_free_get_handler(void) {
    return free_handler;
}

/*
    The wrapper for free function. It checks for passed memory pointer and
    if it is invalid then reports about this.
*/
void aal_free(void *ptr) {
    if (!free_handler) {
	aal_printf("Fatal: Invalid \"free\" handler.\n");
	return;
    }
    free_handler(ptr);
}

