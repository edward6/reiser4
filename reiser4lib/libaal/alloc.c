/*
	alloc.c -- memory allocation functions.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE

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

aal_malloc_handler_t aal_malloc_handler(void) {
	return malloc_handler;
}

void *aal_malloc(size_t size) {
	void *mem;

	if (!malloc_handler) {
		aal_printf("Invalid \"malloc\" handler.\n");
		return NULL;
	}

	if (!(mem = malloc_handler(size))) {
		aal_printf("Out of memory.\n");
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

aal_realloc_handler_t aal_realloc_handler(void) {
	return realloc_handler;
}

int aal_realloc(void** old, size_t size) {
	void *mem;

	if (!realloc_handler) {
		aal_printf("Invalid \"realloc\" handler.\n");
		return 0;
	}

	if (!(mem = (void *)realloc_handler(*old, size))) {
		aal_printf("Out of memory.\n");
		return 0;
	}
	*old = mem;
	return 1;
}

void aal_free_set_handler(aal_free_handler_t handler) {
	free_handler = handler;
}

aal_free_handler_t aal_free_handler(void) {
	return free_handler;
}

void aal_free(void *ptr) {
	if (!free_handler) {
		aal_printf("Invalid \"free\" handler.\n");
		return;
	}
	free_handler(ptr);
}
