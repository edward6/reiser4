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

extern int aal_vsnprintf(char *buff, size_t n, const char *format, 
    va_list arg_list);

extern int aal_snprintf(char *buff, size_t n, const char *format, 
    ...) __check_format__(printf, 3, 4);

#endif

