/*
    string.h -- memory-working and string-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <sys/types.h>

#ifndef STRING_H
#define STRING_H

#define DEFINE_CONVERTOR(name, type) \
    extern int name(type d, size_t n, char *a, int base, int flags)

extern void *aal_memset(void *dest, char c, size_t n);
extern void *aal_memcpy(void *dest, const void *src, size_t n);
extern int aal_memcmp(const void *s1, const void *s2, size_t n);

extern char *aal_strncpy(char *dest, const char *src, size_t n);
extern char *aal_strncat(char *dest, const char *src, size_t n);
extern int aal_strncmp(const char *s1, const char *s2, size_t n);
extern size_t aal_strlen(const char *s);

DEFINE_CONVERTOR(aal_utoa, unsigned int);
DEFINE_CONVERTOR(aal_lutoa, unsigned long int);
DEFINE_CONVERTOR(aal_llutoa, unsigned long long);
DEFINE_CONVERTOR(aal_stoa, int);
DEFINE_CONVERTOR(aal_lstoa, long int);
DEFINE_CONVERTOR(aal_llstoa, long long);
	
#endif

