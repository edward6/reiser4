/*
    string.h -- memory-working and string-working functions.
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

extern int aal_utos(unsigned int d, size_t n, char *a, int base);
extern int aal_lutos(unsigned long int d, size_t n, char *a, int base);
extern int aal_llutos(unsigned long long d, size_t n, char *a, int base);
extern int aal_stos(int d, size_t n, char *a, int base);
extern int aal_lstos(long int d, size_t n, char *a, int base);
extern int aal_llstos(long long d, size_t n, char *a, int base);

#endif

