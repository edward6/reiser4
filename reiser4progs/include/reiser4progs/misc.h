/*
    misc.h -- reiser4progs common include.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef REISER4_MISC_H
#define REISER4_MISC_H

#include <aal/aal.h>
#include <reiser4/filesystem.h>

extern long int reiser4progs_misc_strtol(const char *str, int *error);

extern int reiser4progs_misc_choose_check(const char *chooses, int choose);
extern int reiser4progs_misc_choose_propose(const char *chooses, 
    const char *error, const char *format, ...) __check_format__(printf, 3, 4);

extern int reiser4progs_misc_dev_check(const char *dev);

extern int reiser4progs_misc_size_check(const char *str);
extern unsigned long long reiser4progs_misc_size_parse(const char *str, 
    int *error);

extern reiserfs_profile_t *reiser4progs_find_profile(const char *profile);
extern void reiser4progs_list_profile(void);

#endif

