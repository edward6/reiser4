/*
    string.c -- memory-working and string-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>

#ifdef ENABLE_COMPACT

/* 
    Memory and string working functions. They are full analog of standard ones.
    See corresponding man page for details.
*/
void *aal_memset(void *dest, char c, uint32_t n) {
    char *dest_p = (char *)dest;

    for (; (int)dest_p - (int)dest < (int)n; dest_p++)
	*dest_p = c;

    return dest;
}

void *aal_memcpy(void *dest, const void *src, uint32_t n) {
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

int aal_memcmp(const void *s1, const void *s2, uint32_t n) {
    const char *p_s1 = (const char *)s1, *p_s2 = (const char *)s2;
    for (; (uint32_t)(p_s1 - (int)s1) < n; p_s1++, p_s2++) {
	if (*p_s1 < *p_s2) 
	    return -1;
	
	if (*p_s1 > *p_s2)
	    return 1;
    }
    return p_s1 != s1 ? 0 : -1;
}

uint32_t aal_strlen(const char *s) {
    uint32_t len = 0;

    while (*s++) len++;
    
    return len;
}

int aal_strncmp(const char *s1, const char *s2, uint32_t n) {
    uint32_t len = aal_strlen(s1) < n ? aal_strlen(s1) : n;
    len = aal_strlen(s2) < len ? aal_strlen(s2) : len;

    return aal_memcmp((const void *)s1, (const void *)s2, len);
}

char *aal_strncpy(char *dest, const char *src, uint32_t n) {
    uint32_t len = aal_strlen(src) < n ? aal_strlen(src) : n;
	
    aal_memcpy((void *)dest, (const void *)src, len);
	
    if (len < n) 
	*(dest + aal_strlen(dest)) = '\0';
	
    return dest;
}

char *aal_strcat(char *dest, const char *src) {
    return aal_strncat(dest, src, aal_strlen(src));
}

char *aal_strncat(char *dest, const char *src, uint32_t n) {
    uint32_t len = aal_strlen(src) < n ? aal_strlen(src) : n;
	
    aal_memcpy(dest + aal_strlen(dest), src, len);
	
    if (len < n) 
	*(dest + aal_strlen(dest)) = '\0';
	
    return dest;
}

char *aal_strpbrk(const char *s, const char *accept) {
    char *p_s = (char *)s;
    char *p_a = (char *)accept;
    
    while (*p_s) {
	while (*p_a) {
	    if (*p_a == *p_s)
		return p_s;
	    p_a++;
	}
	p_s++;
    }
    return NULL;
}

char *aal_strchr(const char *s, int c) {
    char *p_s = (char *)s;
    while (*p_s) {
	if (*p_s == c)
	    return p_s;
	p_s++;
    }
    return NULL;
}

char *aal_strrchr(const char *s, int c) {
    char *p_s = ((char *)s + aal_strlen(s) - 1);
    while (p_s != s) {
	if (*p_s == c)
	    return p_s;
	p_s--;
    }
    return NULL;
}

char *aal_strsep(char **stringp, const char *delim) {
    char *begin, *end;

    begin = *stringp;
    
    if (begin == NULL)
        return NULL;
    
    if (delim[0] == '\0' || delim[1] == '\0') {
	char ch = delim[0];
	
        if (ch == '\0')
	    end = NULL;
	else {
	    if (*begin == ch)
		end = begin;
            else if (*begin == '\0')
		end = NULL;
            else
                end = aal_strchr(begin + 1, ch);
        }
    } else
	end = aal_strpbrk(begin, delim);
    
    if (end) {
	*end++ = '\0';
	*stringp = end;
    } else
	*stringp = NULL;
    
    return begin;
}

char *aal_strndup(const char *s, size_t n) {
    char *str = (char *)aal_calloc(n + 1, 0);
    aal_strncpy(str, s, n);

    return str;
}

#endif

void aal_strnup(char *dst, const char *src) {
    char *s;
    int i = 0;

    s = src;
    while (*s) {
	dst[i++] = toupper(*s);
	s++;
    }
    
    dst[i] = '\0';
}

/* Converts string denoted as size into digits */

#define CONV_DEC_RANGE 1000000000
#define CONV_HEX_RANGE 0x10000000
#define CONV_OCT_RANGE 01000000000

/* 
    Macro for providing function for converting the passed digital into string.
    It supports converting of decimal, hexadecimal and octal digits. It is used 
    by aal_vsnprintf function.
*/
#define aal_define_convertor(name, type)			\
int name(type d, uint32_t n, char *a, int base, int flags) {	\
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
	if ((uint32_t)(p - a) >= n)				\
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

/* Declaring converting functions for all supported types (%u, %lu, %llu) */
aal_define_convertor(aal_utoa, unsigned int);
aal_define_convertor(aal_lutoa, unsigned long int);
aal_define_convertor(aal_llutoa, unsigned long long);
aal_define_convertor(aal_stoa, int);
aal_define_convertor(aal_lstoa, long int);
aal_define_convertor(aal_llstoa, long long);

