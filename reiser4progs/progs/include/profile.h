/*
    profile.h -- headers of methods for working with profiles in reiser4 programs.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef PROGS_PROFILE_H
#define PROGS_PROFILE_H

#ifndef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/filesystem.h>

extern void progs_profile_list(void);
extern void progs_profile_print(reiser4_profile_t *profile);

extern reiser4_profile_t *progs_profile_default();
extern reiser4_profile_t *progs_profile_find(const char *profile);

extern errno_t progs_profile_override(reiser4_profile_t *profile, 
    const char *type, const char *name);

#endif
