/*
 	reiserfs.c -- memory allocation functions, version control functions and 
	library initialization code.
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <stdlib.h>
#  include <string.h>
#endif

#include <reiserfs/reiserfs.h>

#if defined(ENABLE_NLS) && !defined(ENABLE_COMPACT)
#  include <libintl.h>
#  define _(String) dgettext (PACKAGE, String)
#else
#  define _(String) (String)
#endif

static libreiserfs_malloc_handler_t malloc_handler = NULL;
static libreiserfs_realloc_handler_t realloc_handler = NULL;
static libreiserfs_free_handler_t free_handler = NULL;

int libreiserfs_get_max_interface_version(void) {
	return LIBREISERFS_MAX_INTERFACE_VERSION;
}

int libreiserfs_get_min_interface_version(void) {
	return LIBREISERFS_MIN_INTERFACE_VERSION;
}

const char *libreiserfs_get_version(void) {
	return VERSION;
}

static void _init(void) __attribute__ ((constructor));

static void _init(void) {
	malloc_handler = malloc;
	realloc_handler = realloc;
	free_handler = free;
#ifdef ENABLE_NLS
	 bindtextdomain(PACKAGE, LOCALEDIR);
#endif
}

void libreiserfs_malloc_set_handler(libreiserfs_malloc_handler_t handler) {
	malloc_handler = handler;
}

libreiserfs_malloc_handler_t libreiserfs_malloc_handler(void) {
	return malloc_handler;
}

void *libreiserfs_malloc(size_t size) {
	void *mem;

	if (!malloc_handler) {
		libreiserfs_exception_throw(EXCEPTION_FATAL, EXCEPTION_CANCEL,
			_("Can't allocate %d bytes. Invalid \"malloc\" handler."), size);
		return NULL;
	}

	if (!(mem = malloc_handler(size))) {
		libreiserfs_exception_throw(EXCEPTION_FATAL, EXCEPTION_CANCEL,
			_("Out of memory."));
		return NULL;
	}

	return mem;
}

void *libreiserfs_calloc(size_t size, char c) {
	void *mem;

	if (!(mem = libreiserfs_malloc(size)))
		return NULL;

	memset(mem, c, size);

	return mem;
}

void libreiserfs_realloc_set_handler(libreiserfs_realloc_handler_t handler) {
	realloc_handler = handler;
}

libreiserfs_realloc_handler_t libreiserfs_realloc_handler(void) {
	return realloc_handler;
}

int libreiserfs_realloc(void** old, size_t size) {
	void *mem;

	if (!realloc_handler) {
		libreiserfs_exception_throw(EXCEPTION_FATAL, EXCEPTION_CANCEL,
			_("Can't reallocate given chunk for %d bytes. Invalid \"realloc\" handler."),
		    size);
	    return 0;
	}

	if (!(mem = (void *)realloc_handler(*old, size))) {
		libreiserfs_exception_throw(EXCEPTION_FATAL, EXCEPTION_CANCEL,
			_("Out of memory."));
		return 0;
	}
	
	*old = mem;
	return 1;
}

void libreiserfs_free_set_handler(libreiserfs_free_handler_t handler) {
	free_handler = handler;
}

libreiserfs_free_handler_t libreiserfs_free_handler(void) {
	return free_handler;
}

void libreiserfs_free(void* ptr) {
	if (!free_handler) {
		libreiserfs_exception_throw(EXCEPTION_FATAL, EXCEPTION_CANCEL,
			_("Can't free given chunk. Invalid \"free\" handler."));
		return;
	}

	free_handler(ptr);
}

