/*
    repair.h -- the central recovery include file.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman
*/

#ifndef PROGS_H
#define PROGS_H

//#include <reiser4/reiser4.h>
#include <getopt.h>
#include <stdio.h>

struct repair_data {
    reiser4_profile_t *profile;
    uint16_t mode;
    uint16_t options;

    FILE *logfile;
};

typedef struct repair_data repair_data_t;

/* Repair modes. */
#define REPAIR_CHECK	0x1
#define REPAIR_REBUILD	0x2
#define REPAIR_ROLLBACK	0x3

#define repair_mode(repair_data)	((repair_data)->mode)

/* Repair options. */
#define REPAIR_OPT_NO_JOURNAL	0x1
#define REPAIR_OPT_AUTO		0x2
#define REPAIR_OPT_FORCE	0x3
#define REPAIR_OPT_QUIET	0x4
#define REPAIR_OPT_VERBOSE	0x5

#define repair_set_option(bit, repair_data)	(aal_set_bit(bit, &repair_data->options))
#define repair_test_option(bit, repair_data)	(aal_test_bit(bit, &repair_data->options))
#define repair_clear_option(bit, repair_data)	(aal_clear_bit(bit, &repair_data->options))


/*  -----------------------------------------------------------
    | Common scheem for communication with users.             |
    |---------------------------------------------------------|
    |  stream (modifier) | default | with log | with 'no-log' |
    |--------------------|---------|--------------------------|
    | warn  (verbose)    | stderr  | log      |  -            |
    | info               | stderr  | stderr   |  -            |
    | error (verbose)    | stderr  | log      |  -            |
    | fatal              | stderr  | stderr   | stderr        |
    | bug                | stderr  | stderr   | stderr        |
    -----------------------------------------------------------
    info   - Information which is supposed to be viewed on-line.
    warn   - Possible problems.
    error  - Problems. 
    fatal  - Problems which are supposed to be viewed on-line. 

    Problems: we need to support auto mode (choose the default answer 
    on all questions), verbose mode (when extra info will be printed) 
    and quiet mode (minimum of the progress) for all plugins.
*/

#define progs_fatal(msg, list...) \
    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, msg, ##list)
#define progs_bug(msg, list...)	\
    aal_exception_throw(EXCEPTION_BUG, EXCEPTION_OK, msg, ##list)
#define progs_error(msg, list...) \
    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, msg, ##list)
#define progs_warn(msg, list...) \
    aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, msg, ##list)
#define progs_info(msg, list...) \
    aal_exception_throw(EXCEPTION_INFORMATION, EXCEPTION_OK, msg, ##list)

#endif

