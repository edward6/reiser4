/*
    aal.h -- the central aal header. aal - (application abstraction library).
    It contains functions which will help libreiser4 to work in any mode,
    out of the box. For now libaal supports two modes: standard (usespace, libc) 
    and so called allone mode - the mode, all bootloaders work in (real mode of
    processor, no libc).
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef AAL_H
#define AAL_H

/*
    As libaal may be used without any standard headers, we need to declare
    NULL macro here in order to avoid compilation errors.
*/
#undef NULL
#if defined(__cplusplus)
#  define NULL 0
#else
#  define NULL ((void *)0)
#endif

/* 
    Macro for checking the format string in situations like this:

    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Operation %d failed.", 
	"open");

    As aal_exception_throw is declared with this macro, compiller in the comile 
    time will make warning about incorrect format string.
*/
#undef __check_format__
#ifdef __GNUC__
#  define __check_format__(style, format, start) \
       __attribute__((__format__(style, format, start)))
#else
#  define __check_format__(style, format, start)
#endif

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

#include <stdarg.h>

/* 
    This type is used for return of result of execution some function.
    
    Success - 0 (not errors),
    Failure - negative error code
*/
typedef int errno_t;

/*
    Type for callback compare function. It is used in list functions and in 
    other places.
*/
typedef int (*comp_func_t) (const void *, const void *, void *);

/* 
    Type for callback function that is called for each element of list. Usage is 
    the same as previous one.
*/
typedef int (*foreach_func_t) (const void *, const void *);

#include "device.h"
#include "file.h"
#include "exception.h"
#include "list.h"
#include "malloc.h"
#include "print.h"
#include "string.h"
#include "math.h"
#include "endian.h"
#include "debug.h"

#endif

