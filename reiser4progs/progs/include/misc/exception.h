/*
    exception.h -- common for all progs exception functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef PROGS_EXCEPTION_H
#define PROGS_EXCEPTION_H

#include <aal/exception.h>

extern aal_exception_option_t progs_exception_handler(aal_exception_t *exception);
extern void progs_exception_set_stream(aal_exception_type_t type, void *stream);
extern void *progs_exception_get_stream(aal_exception_type_t type);

#endif

