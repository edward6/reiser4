/*
    progs.h -- the central reiser4progs include file.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman
*/

#ifndef PROGS_H
#define PROGS_H

#include <reiser4/reiser4.h>
#include <getopt.h>

/* Error codes for progs */
#define NO_ERROR	    0 /*  */
#define OPERATION_ERROR	    8 /* bug in the code, assertions, etc. */
#define USER_ERROR	   16 /* wrong parameters, not allowed values, syntax error, etc. */

struct reiser4_program_data {
    aal_device_t *host_device;
    aal_device_t *journal_device;

    reiserfs_fs_t *fs;
    reiserfs_profile_t *profile;
    
    void *data;
};

typedef struct reiser4_program_data reiser4_program_data_t;

/*  -----------------------------------------------------------
    | Common scheem for communication with users.             |
    |---------------------------------------------------------|
    |  stream (modifier) | default | with log | with 'no-log' |
    |--------------------|---------|--------------------------|
    | info               | stdout  | stdout   |  -            |
    | warn  (verbose)    | stdout  | log      |  -            |
    | error (verbose)    | stderr  | log      |  -            |
    | fatal              | stderr  | stderr   | stderr        |
    | bug                | stderr  | stderr   | stderr        |
    | ask                | stdout  | stdout   | stdout        |
    | progress (quiet)   | stderr  | stderr   | stderr        |
    -----------------------------------------------------------
    info   - Information which is supposed to be viewed on-line.
    warn   - Possible problems.
    error  - Problems. 
    fatal  - Problems which are supposed to be viewed on-line. 
    bug    - Unexpected cases. 
    ask    - Questions (or their parts). 

    Problems: we need to support auto mode (choose the default answer 
    on all questions), verbose mode (when extra info will be printed) 
    and quiet mode (minimum of the progress) for all plugins.
*/

void progs_set_progress(FILE *stream);
FILE *progs_get_progress();
void progs_set_verbose();
void progs_init();

#define progs_progress(msg, list...)	    aal_throw_msg(progs_get_progress(), msg, ##list);\
					    fflush(progs_get_progress());
#define progs_fatal(msg, list...)	    aal_throw_fatal(EO_OK, msg, ##list)
#define progs_bug(msg, list...)		    aal_throw_bug(EO_OK, msg, ##list)
#define progs_error(msg, list...)	    aal_throw_error(EO_OK, msg, ##list)
#define progs_warn(msg, list...)	    aal_throw_warning(EO_OK, msg, ##list)
#define progs_info(msg, list...)	    aal_throw_information(EO_OK, msg, ##list)
#define progs_ask(opt, def, msg, list...)   aal_throw_ask(opt, def, msg, ##list)

/*
#define prog_bug(msg, list...)	    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_BUG, EO_OK, 0, msg, ##list) :   \
    aal_throw_bug(EO_OK, msg, ##list)
#define prog_error(msg, list...)    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_ERROR, EO_OK, 0, msg, ##list) : \
    aal_throw_error(EO_OK, msg, ##list)
#define prog_warn(msg, list...)	    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_WARN, EO_OK, 0, msg, ##list) :  \
    aal_throw_warning(EO_OK, msg, ##list)
#define prog_info(msg, list...)	    progs_get_log() ?			    \
    aal_exception_throw(progs_get_log(), ET_INFO, EO_OK, 0, msg, ##list) :  \
    aal_throw_information(EO_OK, msg, ##list)
#define prog_ask(opt, def, msg, list...)				    \
    aal_exception_throw(stdout, 0, opt, def, msg, ##list)
*/
#endif

