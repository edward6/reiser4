/*
	print.c -- output functions and some useful utilities.
	Coiyright (C) 1996-2002 Hans Reiser
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdarg.h>
#include <aal/aal.h>

#ifndef ENABLE_ALONE
#  include <stdio.h>
#endif

#ifndef ENABLE_ALONE
static aal_printf_handler_t printf_handler = (aal_printf_handler_t)printf;
#else
static aal_printf_handler_t printf_handler = NULL;
#endif

void aal_printf_set_handler(aal_printf_handler_t handler) {
	printf_handler = handler;
}

aal_printf_handler_t aal_printf_handler(void) {
	return printf_handler;
}

void aal_printf(const char *format, ...) {
	va_list arg_list;
	char buff[4096];
	
	if (!printf_handler)
		return;
	
	aal_memset(buff, 0, sizeof(buff));
	
	va_start(arg_list, format);
	aal_vsnprintf(buff, sizeof(buff), format, arg_list);
	va_end(arg_list);
	
	printf_handler(buff);
}

int aal_vsnprintf(char *buff, size_t n, const char *format, va_list arg_list) {
	const char *old = format;
	const char *fmt = format;
	
	aal_memset(buff, 0, n);
	
	while (*fmt) {
		if (fmt - format + 1 >= (int)n)
			break;
		
		switch (*fmt) {
			case '%': {
				char c = *(fmt + 1);
				
				if (fmt - format > 0)
					aal_memcpy(buff + strlen(buff), old, fmt - old);
				
				switch (c) {
					case 's': {
						char *s = va_arg(arg_list, char *);
						aal_strncat(buff, s, n - strlen(buff));
						break;
					}
					case 'd':
					case 'x': {
						int d;
						char s[11];

						aal_memset(s, 0, sizeof(s));
						
						d = va_arg(arg_list, int);
						aal_ltos(d, sizeof(s), s, c == d ? 10 : 16);
						aal_strncat(buff, s, n - strlen(buff));
					}
				}
				fmt += 2;
				old = fmt;
				break;
			}	
			default: fmt++;
		}
	}
	if (fmt - format > 0)
		aal_memcpy(buff + strlen(buff), old, fmt - old);
	return strlen(buff);
}

