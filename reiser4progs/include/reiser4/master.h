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

extern reiser4_master_t *reiser4_master_create(aal_device_t *device, 
    reiser4_id_t format_pid, unsigned int blocksize, const char *uuid, 
    const char *label);

extern errno_t reiser4_master_sync(reiser4_master_t *master);

extern reiser4_plugin_t *reiser4_master_guess(aal_device_t *device);

#endif

extern errno_t reiser4_master_valid(reiser4_master_t *master);
extern reiser4_master_t *reiser4_master_open(aal_device_t *device);
extern int reiser4_master_confirm(aal_device_t *device);
extern void reiser4_master_close(reiser4_master_t *master);

extern char *reiser4_master_magic(reiser4_master_t *master);
extern reiser4_id_t reiser4_master_format(reiser4_master_t *master);
extern uint32_t reiser4_master_blocksize(reiser4_master_t *master);
extern char *reiser4_master_uuid(reiser4_master_t *master);
extern char *reiser4_master_label(reiser4_master_t *master);

#endif

