/*
    progs.h -- the central reiser4progs include file.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman
*/

#ifndef PROGS_H
#define PROGS_H

/* Error codes for progs */
#define NO_ERROR	    0
#define USER_ERROR	    1 /* wrong parameters, not allowed values, syntax error, etc. */
#define OPERATION_ERROR	    2 /* bug in the code, assertions, etc. */
#define IO_ERROR	    8

struct reiser4_program_data {
    aal_device_t *host_device;
    aal_device_t *journal_device;

    reiserfs_fs_t *fs;
    reiserfs_profile_t *profile;
    
    void *data;
};

typedef struct reiser4_program_data reiser4_program_data_t;

#endif

