/*
    mkfs.h -- mkfs structure declarations.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.    
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <progs/progs.h>
#include <progs/io.h>
#include <progs/misc.h>
#include <reiser4/reiser4.h>

struct mkfs_data {
    uint16_t mode;
    uint32_t options;
    uuid_t uuid;
    char label[17];
    uint16_t blocksize;
    uint32_t host_size;
};

typedef struct mkfs_data mkfs_data_t;

#define mkfs_data(prog_data)	    ((mkfs_data_t *)(prog_data)->data)

/* MkFS modes */
#define MKFS_PRINT_PROFILE  0
#define MKFS_PRINT_PLUGINS  1

#define mkfs_set_mode(bit, prog_data)	    (aal_set_bit(bit, &mkfs_data(prog_data)->mode))
#define mkfs_test_mode(bit, prog_data)	    (aal_test_bit(bit, &mkfs_data(prog_data)->mode))

/* MkFS options */
#define MKFS_QUIET	    0
#define MKFS_FORCE_ALL	    1
#define MKFS_FORCE_QUIET    2

#define mkfs_set_option(bit, prog_data)	    (aal_set_bit(bit, &mkfs_data(prog_data)->options))
#define mkfs_set_quiet(prog_data)	    (mkfs_set_option(MKFS_QUIET, prog_data))
#define mkfs_set_force_all(prog_data)	    (mkfs_set_option(MKFS_FORCE_ALL, prog_data))
#define mkfs_set_force_quiet(prog_data)	    (mkfs_set_option(MKFS_FORCE_QUIET, prog_data))

#define mkfs_test_option(bit, prog_data)    (aal_test_bit(bit, &mkfs_data(prog_data)->options))
#define mkfs_test_quiet(prog_data)	    (mkfs_test_option(MKFS_QUIET, prog_data))
#define mkfs_test_force_all(prog_data)	    (mkfs_test_option(MKFS_FORCE_ALL, prog_data))
#define mkfs_test_force_quiet(prog_data)    (mkfs_test_option(MKFS_FORCE_QUIET, prog_data))

#define mkfs_clear_option(bit, prog_data)   (aal_clear_bit(bit, &mkfs_data(prog_data)->options))

