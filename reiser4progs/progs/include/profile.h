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

extern void progs_profile_print_list(void);
extern reiserfs_profile_t *progs_profile_find(const char *profile);

extern reiserfs_profile_t *progs_profile_default();

extern void progs_profile_print_list(void);

extern int progs_profile_override_plugin_id_by_name(reiserfs_profile_t *profile, 
    const char *plugin_type_name, const char *plugin_label);

extern void progs_profile_print(reiserfs_profile_t *profile);

#endif
