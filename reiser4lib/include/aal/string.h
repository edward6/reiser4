/*
	string.h -- memory-working and string-working functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <sys/types.h>

#ifndef STRING_H
#define STRING_H

extern void *aal_memset(void *dest, char c, size_t n);
extern void *aal_memcpy(void *dest, const void *src, size_t n);

extern char *aal_strncpy(char *dest, const char *src, size_t n);
extern char *aal_strncat(char *dest, const char *src, size_t n);

extern int aal_ltos(long int d, size_t n, char *a, int base);
	
#endif

