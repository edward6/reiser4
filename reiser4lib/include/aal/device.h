/*
	device.h -- device interface.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef DEVICE_H
#define DEVICE_H

#include <sys/stat.h>
#include <sys/types.h>

typedef unsigned long blk_t;
typedef unsigned long count_t;

struct aal_device_ops;

struct aal_device {
    int flags;
    void *data;
    size_t blocksize;
    const void *entity;
    struct aal_device_ops *ops;
};

typedef struct aal_device aal_device_t;

struct aal_device_ops {
    int (*read)(aal_device_t *, void *, blk_t, count_t);
    int (*write)(aal_device_t *, void *, blk_t, count_t);
    int (*sync)(aal_device_t *);
    int (*flags)(aal_device_t *);
    int (*equals)(aal_device_t *, aal_device_t *);
    int (*stat)(aal_device_t *, struct stat *);
    count_t (*len)(aal_device_t *);
};

extern aal_device_t *aal_device_open(struct aal_device_ops *ops, const void *entity, 
    size_t blocksize, int flags, void *data);

extern void aal_device_close(aal_device_t *device);

extern int aal_device_set_blocksize(aal_device_t *device, size_t blocksize);
extern size_t aal_device_blocksize(aal_device_t *device);

extern int aal_device_read(aal_device_t *device, void *buff, blk_t block, count_t count);
extern int aal_device_write(aal_device_t *device, void *buff, blk_t block, count_t count);
extern int aal_device_sync(aal_device_t *device);
extern int aal_device_flags(aal_device_t *device);
extern int aal_device_equals(aal_device_t *device1, aal_device_t *device2);
extern int aal_device_stat(aal_device_t *device, struct stat *st);
extern count_t aal_device_len(aal_device_t *device);

#endif

