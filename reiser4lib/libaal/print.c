/*
    print.c -- output functions and some useful utilities.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdarg.h>
#include <aal/aal.h>

#ifndef ENABLE_COMPACT
#  include <stdio.h>
#endif

#ifndef ENABLE_COMPACT
static aal_printf_handler_t printf_handler = (aal_printf_handler_t)printf;
#else
static aal_printf_handler_t printf_handler = NULL;
#endif

void aal_printf_set_handler(aal_printf_handler_t handler) {
    printf_handler = handler;
}

aal_printf_handler_t aal_printf_get_handler(void) {
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

#define MOD_EMPTY 0x0
#define MOD_LONG 0x1
#define MOD_LONG_LONG 0x2

#include <stdio.h>
int aal_vsnprintf(char *buff, size_t n, const char *format, va_list arg_list) {
    int i;
    long int li;
    long long int lli;
			
    unsigned int u;
    unsigned long int lu;
    unsigned long long llu;
    
    int modifier = MOD_EMPTY;
    const char *old = format;
    const char *fmt = format;
    
    aal_memset(buff, 0, n);
	
    while (*fmt) {
	if (fmt - format + 1 >= (int)n)
	    break;

	modifier = MOD_EMPTY;	
	switch (*fmt) {
	    case '%': {
		if (aal_strlen(fmt) < 2)
		    break;
			
		if (fmt - format > 0)
		    aal_memcpy(buff + aal_strlen(buff), old, fmt - old);
repeat:		
		fmt++;
		switch (*fmt) {
		    case 's': {
			char *s;
			
			if (modifier != MOD_EMPTY)
			    break;
			
			s = va_arg(arg_list, char *);
			aal_strncat(buff, s, n - aal_strlen(buff));
			fmt++;
			break;
		    }
		    case 'l': {
			modifier = (modifier == MOD_LONG ? MOD_LONG_LONG : MOD_LONG);
			old++;
			goto repeat;
		    }
		    case 'd':
		    case 'i':
		    case 'u':
		    case 'X':
		    case 'x': {
			char s[32];
			
			aal_memset(s, 0, sizeof(s));
			
			if (*fmt == 'd' || *fmt == 'i') {
			    if (modifier == MOD_EMPTY) {
				i = va_arg(arg_list, int);
				aal_stos(i, sizeof(s), s, 10);
			    } else if (modifier == MOD_LONG) {
				li = va_arg(arg_list, long int);
				aal_lstos(li, sizeof(s), s, 10);
			    } else {
				lli = va_arg(arg_list, long long int);
				aal_llstos(lli, sizeof(s), s, 10);
			    }
			    aal_strncat(buff, s, n - aal_strlen(buff));
			} else {
			    if (modifier == MOD_EMPTY) {
				u = va_arg(arg_list, unsigned int);
				aal_utos(u, sizeof(s), s, (*fmt == 'u' ? 10 : 16));
			    } else if (modifier == MOD_LONG) {
				lu = va_arg(arg_list, unsigned long int);
				aal_lutos(lu, sizeof(s), s, (*fmt == 'u' ? 10 : 16));
			    } else {
				llu = va_arg(arg_list, unsigned long long);
				aal_llutos(llu, sizeof(s), s, (*fmt == 'u' ? 10 : 16));
			    }
			    aal_strncat(buff, s, n - aal_strlen(buff));
			}
			fmt++;
		    }
		}
		old = fmt;
		break;
	    }
	    default: fmt++;
	}
    }

    if (fmt - format > 0)
	aal_memcpy(buff + aal_strlen(buff), old, fmt - old);

    return aal_strlen(buff);
}

int aal_snprintf(char *buff, size_t n, const char *format, ...) {
    int len;
    va_list arg_list;
	
    va_start(arg_list, format);
    len = aal_vsnprintf(buff, n, format, arg_list);
    va_end(arg_list);
    return len;
}

