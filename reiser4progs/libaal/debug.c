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

/* This function is used to provide asserts via exception */
int __assert(char *hint, int cond, char *text, char *file, 
    int line, char *function) 
{
    if (cond) 
	return 1;

    return (aal_throw_ask(EO_IGNORE | EO_CANCEL, EO_CANCEL, "%s: Assertion (%s) at %s:%d "
	"in function %s() failed.", hint, text, file, line, function) == EO_IGNORE);
}

#endif

