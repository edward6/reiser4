/*
    string.h -- memory-working and string-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <sys/types.h>

#ifndef STRING_H
#define STRING_H

#define aal_def_conv(name, type) \
    extern int name(type d, size_t n, char *a, int base, int flags)

extern void *aal_memset(void *dest, char c, size_t n);
extern void *aal_memcpy(void *dest, const void *src, size_t n);
extern int aal_memcmp(const void *s1, const void *s2, size_t n);

extern char *aal_strncpy(char *dest, const char *src, size_t n);
extern char *aal_strncat(char *dest, const char *src, size_t n);
extern int aal_strncmp(const char *s1, const char *s2, size_t n);
extern size_t aal_strlen(const char *s);

aal_def_conv(aal_utoa, unsigned int);
aal_def_conv(aal_lutoa, unsigned long int);
aal_def_conv(aal_llutoa, unsigned long long);
aal_def_conv(aal_stoa, int);
aal_def_conv(aal_lstoa, long int);
aal_def_conv(aal_llstoa, long long);
	
#endif

