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

#define CONV_INT_DEC_RANGE 1000000000
#define CONV_INT_HEX_RANGE 0x10000000

#define CONV_LONG_DEC_RANGE 1000000000
#define CONV_LONG_HEX_RANGE 0x10000000

int aal_utos(unsigned int d, size_t n, char *a, int base) {
    char *p = a;
    unsigned int s;
    unsigned int range;
	
    if (base != 10 && base != 16)
	return 0;

    range = base == 10 ? CONV_INT_DEC_RANGE : CONV_INT_HEX_RANGE;
    aal_memset(p, 0, n);
    
    if (base == 16) {
	aal_strncat(p, "0x", 2);
	p += 2;
    }
    
    if (d == 0) {
	*p++ = '0';
	return 1;
    }
	
    for (s = range; s > 0; s /= base) {
	unsigned int v = d / s;
		
	if ((size_t)(p - a) >= n) 
	    break;
		
	if (v > 0) {
	    if (v >= (unsigned int)base)
		v = (d / s) - ((v / base) * base);
	    *p++ = ('0' + (base == 16 && v > 9 ? 39 : 0) + v);
	}
    }
    return p - a;
}

int aal_lutos(unsigned long int d, size_t n, char *a, int base) {
    char *p = a;
    unsigned long int s;
    unsigned long int range;
    
    if (base != 10 && base != 16)
	return 0;

    range = base == 10 ? CONV_LONG_DEC_RANGE : CONV_LONG_HEX_RANGE;
    aal_memset(p, 0, n);
    
    if (base == 16) {
	aal_strncat(p, "0x", 2);
	p += 2;
    }
    
    if (d == 0) {
	*p++ = '0';
	return 1;
    }
	
    for (s = range; s > 0; s /= base) {
	unsigned long int v = d / s;
	
	if ((size_t)(p - a) >= n) 
	    break;
	
	if (v > 0) {
	    if (v >= (unsigned long int)base)
		v = (d / s) - ((v / base) * base);
	    *p++ = ('0' + (base == 16 && v > 9 ? 39 : 0) + v);
	}
    }
    return p - a;
}

int aal_llutos(unsigned long long d, size_t n, char *a, int base) {
    char *p = a;
    unsigned long long s;
    unsigned long long range;

    if (base != 10 && base != 16)
	return 0;

    if (base == 16)
	return aal_utos((unsigned long long)d, n, a, 16);

    range = base == 10 ? CONV_LONG_DEC_RANGE : CONV_LONG_HEX_RANGE;
    aal_memset(p, 0, n);
    
    if (d == 0) {
	*p++ = '0';
	return 1;
    }
    
    for (s = range; s > 0; s /= base) {
	unsigned long long v = (d / s);
	
	if ((size_t)(p - a) >= n) 
	    break;
		
	if (v > 0) {
	    if (v >= (unsigned long long)base)
		v = (d / s) - ((v / base) * base);
	    *p++ = ('0' + v);
	}
    }
    return p - a;
}

int aal_stos(int d, size_t n, char *a, int base) {
    int s;
    int range;
    char *p = a;

    if (base != 10 && base != 16)
	return 0;

    if (base == 16)
	return aal_utos((unsigned int)d, n, a, 16);
    
    range = base == 10 ? CONV_INT_DEC_RANGE : CONV_INT_HEX_RANGE;
    aal_memset(p, 0, n);
    
    if (d == 0) {
	*p++ = '0';
	return 1;
    }
    
    if (d < 0) 
	*p++ = '-';

    for (s = range; s > 0; s /= base) {
	int v = d < 0 ? -(d / s) : (d / s);
		
	if ((size_t)(p - a) >= n) 
	    break;
		
	if (v > 0) {
	    if (v >= base)
		v = (d / s) - ((v / base) * base);
	    *p++ = ('0' + v);
	}
    }
    return p - a;
}

int aal_lstos(long int d, size_t n, char *a, int base) {
    char *p = a;
    long int s;
    long int range;

    if (base != 10 && base != 16)
	return 0;

    if (base == 16)
	return aal_utos((unsigned long int)d, n, a, 16);
    
    range = base == 10 ? CONV_LONG_DEC_RANGE : CONV_LONG_HEX_RANGE;
    aal_memset(p, 0, n);
    
    if (d == 0) {
	*p++ = '0';
	return 1;
    }
    
    if (d < 0) 
	*p++ = '-';

    for (s = range; s > 0; s /= base) {
	long int v = d < 0 ? -(d / s) : (d / s);
		
	if ((size_t)(p - a) >= n) 
	    break;
		
	if (v > 0) {
	    if (v >= base)
		v = (d / s) - ((v / base) * base);
	    *p++ = ('0' + v);
	}
    }
    return p - a;
}

int aal_llstos(long long d, size_t n, char *a, int base) {
    char *p = a;
    long long s;
    long long range;

    if (base != 10 && base != 16)
	return 0;

    if (base == 16)
	return aal_utos((unsigned long long)d, n, a, 16);
    
    range = base == 10 ? CONV_LONG_DEC_RANGE : CONV_LONG_HEX_RANGE;
    aal_memset(p, 0, n);
    
    if (d == 0) {
	*p++ = '0';
	return 1;
    }
    
    if (d < 0) 
	*p++ = '-';

    for (s = range; s > 0; s /= base) {
	long long v = d < 0 ? -(d / s) : (d / s);
		
	if ((size_t)(p - a) >= n) 
	    break;
		
	if (v > 0) {
	    if (v >= (long long)base)
		v = (d / s) - ((v / base) * base);
	    *p++ = ('0' + v);
	}
    }
    return p - a;
}

