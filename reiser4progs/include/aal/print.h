/*
    print.h -- printing and formating strings functions. They are used for 
    independent from mode (alone or standard) string printing into some error
    console (stderr for standard mode and "int 0x10" for alone mode).
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef PRINT_H
#define PRINT_H

#include <aal/aal.h>

#ifndef ENABLE_COMPACT
#  include <stdio.h>
#endif

typedef void (*aal_printf_handler_t)(const char *);
typedef void (*aal_fprintf_handler_t)(void *stream, const char *);

extern void aal_printf_set_handler(aal_printf_handler_t handler);
extern aal_printf_handler_t aal_printf_get_handler(void);
extern void aal_fprintf_set_handler(aal_fprintf_handler_t handler);
extern aal_fprintf_handler_t aal_fprintf_get_handler(void);

extern void aal_printf(const char *format, ...) __check_format__(printf, 1, 2);
extern void aal_fprintf(void *stream, const char *format, ...) __check_format__(printf, 2, 3);
extern int aal_vsnprintf(char *buff, size_t n, const char *format, va_list arg_list);
extern int aal_snprintf(char *buff, size_t n, const char *format, ...) __check_format__(printf, 3, 4);

#endif

