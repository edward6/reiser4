/*
	debug.c -- libreiserfs assert implementation.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#ifdef ENABLE_DEBUG

int libreiserfs_assert(int cond, char *cond_text, char *file, int line, char *function) {
	aal_exception_option_t opt;

	if (cond) 
		return 1;

	opt = aal_exception_throw(EXCEPTION_BUG, EXCEPTION_IGNORE | EXCEPTION_CANCEL,
		"", "Assertion (%s) at %s:%d in function %s() failed.", cond_text, file, 
		line, function);
	
	return opt == EXCEPTION_IGNORE;
}

#endif

