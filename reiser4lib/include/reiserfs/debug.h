/*
    debug.h -- libreiserfs assert implementation through exception.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DEBUG_H
#define DEBUG_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef ENABLE_DEBUG

#ifdef __GNUC__

#define ASSERT(cond, action)		\
    do {				\
    	if (!libreiserfs_assert(cond,	\
	   #cond,			\
	    __FILE__,			\
	    __LINE__,			\
	    __PRETTY_FUNCTION__))	\
	{				\
	    action;			\
	}				\
    } while (0);

#else

#define ASSERT(cond, action)		\
    do {				\
	if (!libreiserfs_assert(cond,	\
	    #cond,			\
	    "unknown",			\
	    0,				\
	    "unknown"))			\
	{				\
	    action;			\
	}				\
    } while (0);

#endif

#else

#define ASSERT(cond, action) while (0) {}

#endif

extern int libreiserfs_assert(int cond, char *cond_text, char *file, int line, 
    char *function);

#endif

