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

struct device_ops;

struct device {
	int flags;
	void *data;
	size_t blocksize;
	const void *entity;
	struct device_ops *ops;
};

typedef struct device device_t;

struct device_ops {
	int (*read)(device_t *, void *, blk_t, count_t);
	int (*write)(device_t *, void *, blk_t, count_t);
	int (*sync)(device_t *);
	int (*flags)(device_t *);
	int (*equals)(device_t *, device_t *);
	int (*stat)(device_t *, struct stat *);
	count_t (*len)(device_t *);
};

extern device_t *device_open(struct device_ops *ops, const void *entity, size_t blocksize, 
	int flags, void *data);

extern void device_close(device_t *device);

extern int device_set_block_size(device_t *device, size_t blocksize);
extern size_t device_block_size(device_t *device);

extern int device_read(device_t *device, void *buff, blk_t block, count_t count);
extern int device_write(device_t *device, void *buff, blk_t block, count_t count);
extern int device_sync(device_t *device);
extern int device_flags(device_t *device);
extern int device_equals(device_t *device1, device_t *device2);
extern int device_stat(device_t *device, struct stat *st);
extern count_t device_len(device_t *device);

#endif

