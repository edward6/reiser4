/*
    debug.c -- implements assertions through exception mechanism.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>

#ifdef ENABLE_DEBUG

/* 
    This function is used to provide asserts via exception. It is used by macro
    aal_assert().
*/
int __assert(
    char *hint,	    /* person owner of assert */
    int cond,	    /* condition of assertion */
    char *text,	    /* text of the assertion */
    char *file,	    /* source file assertion was failed in */
    int line,	    /* line of code assertion was failed in */
    char *function  /* function in code assertion was failed in */
) {
    /* Checking the condition */
    if (cond) 
	return 1;

    /* 
	Actual exception throwing. Messages will contain hint for owner, file, 
	line and function assertion was failed in.
    */ 
    return (aal_exception_throw(EXCEPTION_BUG, EXCEPTION_IGNORE | EXCEPTION_CANCEL,
	"%s: Assertion (%s) at %s:%d in function %s() failed.", hint, text, file, 
	line, function) == EXCEPTION_IGNORE);
}

#endif

