/*
    string.h -- memory-working and string-working functions. They are needed in
    order to be independent from specific application. As libreiser4 is used 
    string functions, we should provide them for it, because in alone mode they
    doesn't exist due to libc is not in use.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <sys/types.h>

#ifndef STRING_H
#define STRING_H

extern void *aal_memset(void *dest, char c, size_t n);
extern void *aal_memcpy(void *dest, const void *src, size_t n);
extern int aal_memcmp(const void *s1, const void *s2, size_t n);

extern char *aal_strncpy(char *dest, const char *src, size_t n);
extern char *aal_strncat(char *dest, const char *src, size_t n);
extern int aal_strncmp(const char *s1, const char *s2, size_t n);
extern size_t aal_strlen(const char *s);

extern char *aal_strpbrk(const char *s, const char *accept);
extern char *aal_strchr(const char *s, int c);
extern char *aal_strsep(char **stringp, const char *delim);

extern int aal_utoa(unsigned int d, size_t n, char *a, int base, int flags);
extern int aal_lutoa(unsigned long int d, size_t n, char *a, int base, int flags);
extern int aal_llutoa(unsigned long long d, size_t n, char *a, int base, int flags);

extern int aal_stoa(int d, size_t n, char *a, int base, int flags);
extern int aal_lstoa(long int d, size_t n, char *a, int base, int flags);
extern int aal_llstoa(long long d, size_t n, char *a, int base, int flags);

#endif

