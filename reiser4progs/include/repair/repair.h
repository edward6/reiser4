/*
    repair/repair.h -- the common structures and methods for recovery.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman
*/

#ifndef REPAIR_H
#define REPAIR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

struct repair_data {
    reiser4_profile_t *profile;
    uint16_t mode;
    uint16_t options;

    FILE *logfile;
    aal_device_t *host_device;
    aal_device_t *journal_device;
};

typedef struct repair_data repair_data_t;

#define repair_data(fs)			((repair_data_t *)fs->data)

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
#define REPAIR_OPT_READ_ONLY	0x6

#define repair_set_option(bit, repair_data)	(aal_set_bit(bit, &repair_data->options))
#define repair_test_option(bit, repair_data)	(aal_test_bit(bit, &repair_data->options))
#define repair_clear_option(bit, repair_data)	(aal_clear_bit(bit, &repair_data->options))

#define repair_no_journal(repair_data)	(repair_test_option(REPAIR_OPT_NO_JOURNAL, repair_data))
#define repair_auto(repair_data)	(repair_test_option(REPAIR_OPT_AUTO, repair_data))
#define repair_force(repair_data)	(repair_test_option(REPAIR_OPT_FORCE, repair_data))
#define repair_quiet(repair_data)	(repair_test_option(REPAIR_OPT_QUIET, repair_data))
#define repair_verbose(repair_data)	(repair_test_option(REPAIR_OPT_VERBOSE, repair_data))
#define repair_read_only(repair_data)	(repair_test_option(REPAIR_OPT_READ_ONLY, repair_data))

struct repair_check {
    reiser4_format_t *format;
    reiser4_alloc_t *a_control;
    reiser4_key_t ld_key, rd_key;
    uint16_t options;
    uint8_t level;
    uint64_t flags;
};

typedef struct repair_check repair_check_t;

#define REPAIR_NOT_FIXED  0x1

#define repair_set_flag(data, flag)	(aal_set_bit(flag, &data->flags))
#define repair_test_flag(data, flag)	(aal_set_bit(flag, &data->flags))

#endif

