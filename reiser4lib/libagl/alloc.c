/*
	alloc.c -- memory allocation functions.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <agl/agl.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE

#include <stdlib.h>
static agl_malloc_handler_t malloc_handler = malloc;
static agl_realloc_handler_t realloc_handler = realloc;
static agl_free_handler_t free_handler = free;
#else
static agl_malloc_handler_t malloc_handler = NULL;
static agl_realloc_handler_t realloc_handler = NULL;
static agl_free_handler_t free_handler = NULL;

#endif

void agl_malloc_set_handler(agl_malloc_handler_t handler) {
	malloc_handler = handler;
}

agl_malloc_handler_t agl_malloc_handler(void) {
	return malloc_handler;
}

void *agl_malloc(size_t size) {
	void *mem;

	if (!malloc_handler) {
		agl_print("Invalid \"malloc\" handler.\n");
		return NULL;
	}

	if (!(mem = malloc_handler(size))) {
		agl_print("Out of memory.\n");
		return NULL;
	}
	return mem;
}

void *agl_calloc(size_t size, char c) {
	void *mem;

	if (!(mem = agl_malloc(size)))
		return NULL;

	memset(mem, c, size);
	return mem;
}

void agl_realloc_set_handler(agl_realloc_handler_t handler) {
	realloc_handler = handler;
}

agl_realloc_handler_t agl_realloc_handler(void) {
	return realloc_handler;
}

int agl_realloc(void** old, size_t size) {
	void *mem;

	if (!realloc_handler) {
		agl_print("Invalid \"realloc\" handler.\n");
		return 0;
	}

	if (!(mem = (void *)realloc_handler(*old, size))) {
		agl_print("Out of memory.\n");
		return 0;
	}
	*old = mem;
	return 1;
}

void agl_free_set_handler(agl_free_handler_t handler) {
	free_handler = handler;
}

agl_free_handler_t agl_free_handler(void) {
	return free_handler;
}

void agl_free(void *ptr) {
	if (!free_handler) {
		agl_print("Invalid \"free\" handler.\n");
		return;
	}
	free_handler(ptr);
}
