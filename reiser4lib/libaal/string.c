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

char *aal_strcat(char *dest, const char *src) {
	aal_memcpy(dest + strlen(dest), src, strlen(src));
	return dest;
}

