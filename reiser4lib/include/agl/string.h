/*
	string.h -- memory-working and string-working functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <sys/types.h>

extern void *agl_memset(void *dest, char c, size_t n);
extern void *agl_memcpy(void *dest, const void *src, size_t n);
extern char *agl_strncpy(char *dest, const char *src, size_t n);

