/*
    file.h -- standard file device that works via device interface.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FILE_DEVICE_H
#define FILE_DEVICE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE

#include <aal/device.h>

extern aal_device_t *aal_file_open(const char *file, uint32_t blocksize, int flags);
extern int aal_file_reopen(aal_device_t *device, int flags);
extern void aal_file_close(aal_device_t *device);

#endif

#endif

