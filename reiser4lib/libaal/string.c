/*
	string.c -- memory-working and string-working functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <sys/types.h>

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

char *aal_strncpy(char *dest, const char *src, size_t n) {
	int len = strlen(src) < n ? strlen(src) : n;
	return (char *)aal_memcpy((void *)dest, (const void *)src, len);
}

char *aal_strncat(char *dest, const char *src, size_t n) {
	aal_memcpy(dest + strlen(dest), src, (strlen(src) < n ? strlen(src) : n));
	return dest;
}

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
	
	for (s = range; s > 0; s /= base) {
		long int v = d / s;
		
		if (p - a >= n) 
			break;
		
		if (v > 0) {
			if (v >= base)
				v = (d / s) - ((v / base) * base);
			*p++ = ('0' + (base == 16 && v > 9 ? 39 : 0) + v);
		}
	}
	return p - a;
}

