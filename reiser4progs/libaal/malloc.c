/*
    malloc.c -- hanlders for memory allocation functions.
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
    set it up.
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

/* 
    Sets new handler for malloc function. This is useful for alone mode, because
    all application which are working in alone mode (without libc, probably in real
    mode of processor, etc) have own memory allocation factory. That factory usualy
    operates on some static memory heap. And all allocation function just mark some 
    piece of heap as used. And all deallocation function marks corresponding piece
    as unused.
*/
void aal_malloc_set_handler(
    aal_malloc_handler_t handler    /* new handler to be set */
) {
    malloc_handler = handler;
}

/* Returns allocation handler */
aal_malloc_handler_t aal_malloc_get_handler(void) {
    return malloc_handler;
}

/*
    The wrapper for malloc function. It checks for result memory allocation and
    if it failed then reports about this.
*/
void *aal_malloc(
    size_t size			    /* size of memory piece to be allocated */
) {
    void *mem;

    /* 
	We are using simple printf function instead of exception, because exception
	initialization is needed correctly worked memory allocation handler.
    */
    if (!malloc_handler)
	return NULL;

    if (!(mem = malloc_handler(size)))
	return NULL;

    return mem;
}

/* Allocates memory piese and fills it by specified byte */
void *aal_calloc(
    size_t size,		    /* size of memory piece to be allocated */
    char c
) {
    void *mem;

    if (!(mem = aal_malloc(size)))
	return NULL;

    aal_memset(mem, c, size);
    return mem;
}

/* 
    Sets new handler for "realloc" operation. The same as in malloc case. See above
    for details.
*/
void aal_realloc_set_handler(
    aal_realloc_handler_t handler   /* new handler for realloc */
) {
    realloc_handler = handler;
}

/* Returns realloc handler */
aal_realloc_handler_t aal_realloc_get_handler(void) {
    return realloc_handler;
}

/*
    The wrapper for realloc function. It checks for result memory allocation and
    if it failed then reports about this.
*/
int aal_realloc(
    void** old,			    /* pointer to previously allocated piece */
    size_t size			    /* new size */
) {
    void *mem;

    if (!realloc_handler)
	return 0;

    if (!(mem = (void *)realloc_handler(*old, size)))
	return 0;
    
    *old = mem;
    return 1;
}

/* Sets new handle for "free" operation */
void aal_free_set_handler(
    aal_free_handler_t handler	    /* new "free" operation handler */
) {
    free_handler = handler;
}

/* Returns hanlder for "free" opetration */
aal_free_handler_t aal_free_get_handler(void) {
    return free_handler;
}

/*
    The wrapper for free function. It checks for passed memory pointer and
    if it is invalid then reports about this.
*/
void aal_free(
    void *ptr			    /* pointer onto memory to be released */
) {
    if (!free_handler)
	return;

    free_handler(ptr);
}

