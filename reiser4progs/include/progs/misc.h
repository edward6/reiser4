/*
    misc.h -- miscellaneous useful tools for reiser4 progs.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef PROGS_MISC_H
#define PROGS_MISC_H

#ifndef HAVE_CONFIG_H
#   include <config.h>
#endif

#include <errno.h>
#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <progs/progs.h>

extern long long int progs_misc_strtol(const char *str);

extern long long int progs_misc_size_parse(const char *str);

extern int progs_is_mounted(const char *filename);
extern int progs_is_mounted_ro(const char *filename);

#endif
