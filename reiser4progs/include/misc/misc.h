/*
    misc.h -- miscellaneous useful code.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef MISC_H
#define MISC_H

#include <stdint.h>
#include <aal/aal.h>
#include <reiser4/filesystem.h>

typedef void *(*reiserfs_elem_func_t) (void *, uint32_t, void *);
typedef int (*reiserfs_comp_func_t) (const void *, const void *, void *);

extern int reiserfs_misc_bin_search(void *array, uint32_t count, 
    void *needle, reiserfs_elem_func_t elem_func, 
    reiserfs_comp_func_t comp_func, void *, uint64_t *pos);

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

