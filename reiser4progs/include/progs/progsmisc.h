/*
    progsmisc.h -- miscellaneous useful tools for reiser4 progs.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef PROGS_MISC_H
#define PROGS_MISC_H

#include <mntent.h>

#include <reiser4/reiser4.h>

extern long long int progs_misc_strtol(const char *str);
/*
extern int progs_misc_choose_check(const char *chooses, int choose);
extern int progs_misc_choose_propose(const char *chooses,
    const char *error, const char *format, ...) __check_format__(printf, 3, 4);
extern int progs_misc_dev_check(const char *dev);

extern int progs_misc_size_check(const char *str);
*/

extern long long int progs_misc_size_parse(const char *str);

extern reiserfs_profile_t *progs_find_profile(const char *profile);
extern void reiserfs_list_profile(void);
extern reiserfs_profile_t *progs_get_default_profile();

extern void progs_print_profile_list(void);
extern void progs_print_plugins(); 
extern int progs_profile_override_plugin_id_by_name(reiserfs_profile_t *profile, 
    const char *plugin_type_name, const char *plugin_label);
extern void progs_print_profile(reiserfs_profile_t *profile); 

extern int progs_is_mounted(const char *filename);
extern int progs_is_mounted_ro(const char *filename);

inline FILE *progs_get_log();
inline void progs_set_log(FILE *log);

#define prog_progress(msg, list...) aal_exception_throw(stderr, 0, 0, 0, msg, ##list)
#define prog_fatal(msg, list...)    aal_throw_fatal(EO_OK, msg, ##list) 
#define prog_error(msg, list...)    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_ERROR, EO_OK, 0, msg, ##list) : \
    aal_throw_error(EO_OK, msg, ##list)
#define prog_bug(msg, list...)	    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_BUG, EO_OK, 0, msg, ##list) :   \
    aal_throw_bug(EO_OK, msg, ##list)
#define prog_warn(msg, list...)	    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_WARN, EO_OK, 0, msg, ##list) :  \
    aal_throw_warning(EO_OK, msg, ##list)
#define prog_info(msg, list...)	    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_INFO, EO_OK, 0, msg, ##list) :  \
    aal_throw_information(EO_OK, msg, ##list)
#define prog_ask(opt, def, msg, list...)				    \
    aal_exception_throw(stdout, 0, opt, def, msg, ##list)

#endif
