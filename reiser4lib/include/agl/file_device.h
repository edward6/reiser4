/*
	file_device.h -- standard file device.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef FILE_DEVICE_H
#define FILE_DEVICE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE

#include <agl/device.h>

extern device_t *file_device_open(const char *file, size_t blocksize, int flags);
extern int file_device_reopen(device_t *device, int flags);
extern void file_device_close(device_t *device);

#endif

#endif

