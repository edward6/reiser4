/*
    debug.c -- assert through exception implementation.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>

#ifdef ENABLE_DEBUG

int __assert(char *hint, int cond, char *text, char *file, 
    int line, char *function) 
{
    if (cond) 
	return 1;

    return (aal_exception_throw(EXCEPTION_BUG, EXCEPTION_IGNORE | EXCEPTION_CANCEL,
	"%s: Assertion (%s) at %s:%d in function %s() failed.", hint, text, file, 
	line, function) == EXCEPTION_IGNORE);
}

#endif

