/*
    master.h -- master super block functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef MASTER_H
#define MASTER_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/filesystem.h>

#ifndef ENABLE_COMPACT

extern reiserfs_master_t *reiserfs_master_create(reiserfs_id_t format_pid,
    unsigned int blocksize, const char *uuid, const char *label);

extern errno_t reiserfs_master_sync(reiserfs_master_t *master,
    aal_device_t *device);

extern errno_t reiserfs_master_check(reiserfs_master_t *master);
extern reiserfs_plugin_t *reiserfs_master_guess(aal_device_t *device);

#endif

extern reiserfs_master_t *reiserfs_master_open(aal_device_t *device);
extern int reiserfs_master_confirm(aal_device_t *device);
extern void reiserfs_master_close(reiserfs_master_t *master);

#endif

