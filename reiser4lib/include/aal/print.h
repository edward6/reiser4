/*
    print.h -- output functions and formating strings functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef PRINT_H
#define PRINT_H

#include <stdarg.h>

typedef void (*aal_printf_handler_t)(const char *);

extern void aal_printf_set_handler(aal_printf_handler_t handler);
extern aal_printf_handler_t aal_printf_get_handler(void);

extern void aal_printf(const char *format, ...);
extern int aal_vsnprintf(char *buff, size_t n, const char *format, va_list arg_list);
extern int aal_snprintf(char *buff, size_t n, const char *format, ...);

#endif

