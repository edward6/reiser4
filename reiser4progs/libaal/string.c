/*
    string.c -- memory-working and string-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <sys/types.h>

/* Memory-working functions */
void *aal_memset(void *dest, char c, size_t n) {
    char *dest_p = (char *)dest;

    for (; (int)dest_p - (int)dest < (int)n; dest_p++)
	*dest_p = c;

    return dest;
}

void *aal_memcpy(void *dest, const void *src, size_t n) {
    char *dest_p; 
    char *src_p;

    if (dest > src) {
	dest_p = (char *)dest; 
	src_p = (char *)src;

	for (; (int)src_p - (int)src < (int)n; src_p++, dest_p++)
	    *dest_p = *src_p;
    } else {
	dest_p = (char *)dest + n - 1; 
	src_p = (char *)src + n  - 1;

	for (; (int)src_p - (int)src >= 0; src_p--, dest_p--)
	    *dest_p = *src_p;
    }
    
    return dest;
}

int aal_memcmp(const void *s1, const void *s2, size_t n) {
    const char *p_s1 = (const char *)s1, *p_s2 = (const char *)s2;
    for (; (size_t)(p_s1 - (int)s1) < n; p_s1++, p_s2++) {
	if (*p_s1 < *p_s2) 
	    return -1;
	
	if (*p_s1 > *p_s2)
	    return 1;
    }
    return p_s1 != s1 ? 0 : -1;
}

/* String-working functions */
size_t aal_strlen(const char *s) {
    size_t len = 0;

    while (*s++) len++;
    
    return len;
}

int aal_strncmp(const char *s1, const char *s2, size_t n) {
    size_t len = aal_strlen(s1) < n ? aal_strlen(s1) : n;
    len = aal_strlen(s2) < len ? aal_strlen(s2) : len;

    return aal_memcmp((const void *)s1, (const void *)s2, len);
}

char *aal_strncpy(char *dest, const char *src, size_t n) {
    size_t len = aal_strlen(src) < n ? aal_strlen(src) : n;
	
    aal_memcpy((void *)dest, (const void *)src, len);
	
    if (len < n) 
	*(dest + aal_strlen(dest)) = '\0';
	
    return dest;
}

char *aal_strncat(char *dest, const char *src, size_t n) {
    size_t len = aal_strlen(src) < n ? aal_strlen(src) : n;
	
    aal_memcpy(dest + aal_strlen(dest), src, len);
	
    if (len < n) 
	*(dest + aal_strlen(dest)) = '\0';
	
    return dest;
}

#define CONV_DEC_RANGE 1000000000
#define CONV_HEX_RANGE 0x10000000
#define CONV_OCT_RANGE 01000000000

#undef DEF_CONVERTOR

#define DEF_CONVERTOR(name, type)				\
int name(type d, size_t n, char *a, int base, int flags) {	\
    char *p = a;						\
    type s;							\
    type range;							\
								\
    switch (base) {						\
	case 10: range = CONV_DEC_RANGE; break;			\
	case 16: range = CONV_HEX_RANGE; break;			\
	case 8: range = CONV_OCT_RANGE; break;			\
	default: return 0;					\
    }								\
    aal_memset(p, 0, n);					\
								\
    if (base == 16) {						\
	aal_strncat(p, "0x", 2);				\
	p += 2;							\
    }								\
								\
    if (base == 8)						\
	*p++ = '0';						\
								\
    if (d == 0) {						\
	*p++ = '0';						\
	return 1;						\
    }								\
								\
    for (s = range; s > 0; s /= base) {				\
	type v = d / s;						\
								\
	if ((size_t)(p - a) >= n)				\
	    break;						\
								\
	if (v > 0) {						\
	    if (v >= (type)base)				\
		v = (d / s) - ((v / base) * base);		\
	    switch (base) {					\
		case 10: case 8: *p++ = '0' + v; break;		\
		case 16: {					\
		    if (flags == 0)				\
			*p++ = '0' + (v > 9 ? 39 : 0) + v;	\
		    else					\
			*p++ = '0' + (v > 9 ? 7 : 0) + v;	\
		    break;					\
		}						\
	    }							\
	}							\
    }								\
    return p - a;						\
}								\

DEF_CONVERTOR(aal_utoa, unsigned int);
DEF_CONVERTOR(aal_lutoa, unsigned long int);
DEF_CONVERTOR(aal_llutoa, unsigned long long);
DEF_CONVERTOR(aal_stoa, int);
DEF_CONVERTOR(aal_lstoa, long int);
DEF_CONVERTOR(aal_llstoa, long long);

