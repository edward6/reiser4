/*
	string.c -- memory-working and string-working functions.
	Copyright (C) 1996-2002 Hans Reiser.
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
	char *dest_p = (char *)dest; char *src_p = (char *)src;

	for (; (int)src_p - (int)src < (int)n; src_p++, dest_p++)
		*dest_p = *src_p;
	
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
char *aal_strncpy(char *dest, const char *src, size_t n) {
	size_t len = strlen(src) < n ? strlen(src) : n;
	
	aal_memcpy((void *)dest, (const void *)src, len);
	
	if (len < n) 
		*(dest + strlen(dest)) = '\0';
	
	return dest;
}

char *aal_strncat(char *dest, const char *src, size_t n) {
	size_t len = strlen(src) < n ? strlen(src) : n;
	
	aal_memcpy(dest + strlen(dest), src, len);
	
	if (len < n) 
		*(dest + strlen(dest)) = '\0';
	
	return dest;
}

int aal_strncmp(const char *s1, const char *s2, size_t n) {
	size_t len = strlen(s1) < n ? strlen(s1) : n;
	len = strlen(s2) < len ? strlen(s2) : len;

	return aal_memcmp((const void *)s1, (const void *)s2, len);
}

/* Longint to string convertation function */
int aal_ltos(long int d, size_t n, char *a, int base) {
	long int s;
	char *p = a;
	long int range;
	
	if (base != 10 && base != 16)
		return 0;
	
	range = base == 10 ? 1000000000 : 0x10000000;
	aal_memset(p, 0, n);

	if (d < 0) 
		*p++ = '-';
	
	if (base == 16) {
		aal_strncat(p, "0x", 2);
		p += 2;
	}
	
	if (d == 0) {
		*p++ = '0';
		return 1;
	}
	
	for (s = range; s > 0; s /= base) {
		long int v = d / s;
		
		if ((size_t)(p - a) >= n) 
			break;
		
		if (v > 0) {
			if (v >= base)
				v = (d / s) - ((v / base) * base);
			*p++ = ('0' + (base == 16 && v > 9 ? 39 : 0) + v);
		}
	}
	return p - a;
}

