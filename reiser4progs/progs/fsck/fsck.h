/*
    fsck.h -- fsck structure declarations.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.    
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <progs/version.h>
#include <progs/progs.h>
#include <progs/misc.h>
#include <progs/io.h>
#include <progs/profile.h>
#include <progs/filesystem.h>


struct fsck_data {
    uint16_t mode;
    uint32_t options;
};

typedef struct fsck_data fsck_data_t;

#define fsck_data(prog_data)	    ((fsck_data_t *)(prog_data)->data)

/* FSCK modes. */
#define FSCK_CHECK	0x1
#define FSCK_REBUILD	0x2
#define FSCK_ROLLBACK	0x3

#define fsck_mode(prog_data)		    (fsck_data(prog_data)->mode)

/* FSCK options. */
#define FSCK_OPT_NO_JOURNAL	0x1
#define FSCK_OPT_AUTO		0x2
#define FSCK_OPT_FORCE		0x3
#define FSCK_OPT_QUIET		0x4

#define fsck_set_option(bit, prog_data)	    (aal_set_bit(bit, &fsck_data(prog_data)->options))
#define fsck_test_option(bit, prog_data)    (aal_test_bit(bit, &fsck_data(prog_data)->options))
#define fsck_clear_option(bit, prog_data)   (aal_clear_bit(bit, &fsck_data(prog_data)->options))

