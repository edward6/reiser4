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

enum format_modifier {
    mod_empty,
    mod_long,
    mod_longer
};

typedef enum format_modifier format_modifier_t;

int aal_vsnprintf(char *buff, size_t n, const char *format, va_list arg_list) {
    int i;
    long int li;
    long long int lli;
			
    unsigned int u;
    unsigned long int lu;
    unsigned long long llu;
    
    const char *old = format;
    const char *fmt = format;
    
    format_modifier_t modifier = mod_empty;
    
    aal_memset(buff, 0, n);
	
    while (*fmt) {
	if (fmt - format + 1 >= (int)n)
	    break;

	modifier = mod_empty;
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
			
			if (modifier != mod_empty)
			    break;
			
			s = va_arg(arg_list, char *);
			aal_strncat(buff, s, n - aal_strlen(buff));
			fmt++;
			break;
		    }
		    case 'c' : {
			char c = va_arg(arg_list, char);
			*buff = c; fmt++;
			break;
		    }
		    case '%' : {
			aal_strncat(buff, "% ", n - aal_strlen(buff));
			fmt++;
			break;
		    }
		    case 'l': {
			modifier = (modifier == mod_long ? mod_longer : mod_long);
			old++;
			goto repeat;
		    }
		    case 'd':
		    case 'o':
		    case 'i':
		    case 'u':
		    case 'X':
		    case 'x': {
			char s[32];
			
			aal_memset(s, 0, sizeof(s));
			
			if (*fmt == 'd' || *fmt == 'i') {
			    if (modifier == mod_empty) {
				i = va_arg(arg_list, int);
				aal_stoa(i, sizeof(s), s, 10, 0);
			    } else if (modifier == mod_long) {
				li = va_arg(arg_list, long int);
				aal_lstoa(li, sizeof(s), s, 10, 0);
			    } else {
				lli = va_arg(arg_list, long long int);
				aal_llstoa(lli, sizeof(s), s, 10, 0);
			    }
			    aal_strncat(buff, s, n - aal_strlen(buff));
			} else {
			    if (modifier == mod_empty) {
				u = va_arg(arg_list, unsigned int);
				switch (*fmt) {
				    case 'u': {
					aal_utoa(u, sizeof(s), s, 10, 0);
					break;
				    }
				    case 'x': {
					aal_utoa(u, sizeof(s), s, 16, 0);
					break;
				    }
				    case 'X': {
					aal_utoa(u, sizeof(s), s, 16, 1);
					break;
				    }
				    case 'o': {
					aal_utoa(u, sizeof(s), s, 8, 0);
					break;
				    }
				}
			    } else if (modifier == mod_long) {
				lu = va_arg(arg_list, unsigned long int);
				switch (*fmt) {
				    case 'u': {
					aal_lutoa(lu, sizeof(s), s, 10, 0);
					break;
				    }
				    case 'x': {
					aal_lutoa(lu, sizeof(s), s, 16, 0);
					break;
				    }
				    case 'X': {
					aal_lutoa(lu, sizeof(s), s, 16, 1);
					break;
				    }
				    case 'o': {
					aal_lutoa(lu, sizeof(s), s, 8, 0);
					break;
				    }
				}
			    } else {
				llu = va_arg(arg_list, unsigned long long);
				switch (*fmt) {
				    case 'u': {
					aal_llutoa(llu, sizeof(s), s, 10, 0);
					break;
				    }
				    case 'x': {
					aal_llutoa(llu, sizeof(s), s, 16, 0);
					break;
				    }
				    case 'X': {
					aal_llutoa(llu, sizeof(s), s, 16, 1);
					break;
				    }
				    case 'o': {
					aal_llutoa(llu, sizeof(s), s, 8, 0);
					break;
				    }
				}
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

