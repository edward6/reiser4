/*
	device.c -- device abstraction layer.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <sys/types.h>
#include <sys/stat.h>

#include <aal/aal.h>

static int power_of_two(unsigned long n) {
	return (n & -n) == n;
}

device_t *device_open(struct device_ops *ops, const void *entity, 
	size_t blocksize, int flags, void *data) 
{
	device_t *device;
	
	if (!ops) 
		return NULL;
	
	if (!power_of_two(blocksize)) {
		aal_printf("Block size %d isn't power of two.\n", blocksize);
		return NULL;
	}	
	
	if (!(device = (device_t *)aal_malloc(sizeof(*device))))
		return NULL;

	device->ops = ops;
	device->data = data;
	device->flags = flags;
	device->entity = entity;
	device->blocksize = blocksize;
	
	return device;
}

void device_close(device_t *device) {
	
	if (!device) 
		return;
	
	device->ops = NULL;
	device->entity = NULL;
	device->data = NULL;
	aal_free(device);
}

int device_set_block_size(device_t *device, size_t blocksize) {

	if (!device) 
		return 0;
	
	if (!power_of_two(blocksize)) {
		aal_printf("Block size %d isn't power of two.\n", blocksize);
		return 0;
	}	
	
	device->blocksize = blocksize;
	
	return 1;
}

size_t device_block_size(device_t *device) {

	if (!device) 
		return 0;

	return device->blocksize;
}

int device_read(device_t *device, void *buff, blk_t block, count_t count) {

	if (!device) 
		return 0;

	if (device->ops->read)
		return device->ops->read(device, buff, block, count);
	
	return 0;
}

int device_write(device_t *device, void *buff, blk_t block, count_t count) {

	if (!device) 
		return 0;
	
	if (device->ops->write)
		return device->ops->write(device, buff, block, count);
		
	return 0;
}
	
int device_sync(device_t *device) {

	if (!device) 
		return 0;

	if (device->ops->sync)
		return device->ops->sync(device);
	
	return 0;	
}

int device_flags(device_t *device) {

	if (!device) 
		return 0;

	if (device->ops->flags)
		return device->ops->flags(device);
	
	return 0;
}

int device_equals(device_t *device1, device_t *device2) {
	
	if (!device1 || !device2) 
		return 0;

	if (device1->ops->equals)
		return device1->ops->equals(device1, device2);
	
	return 0;
}

int device_stat(device_t *device, struct stat* st) {

	if (!device) 
		return 0;
	
	if (device->ops->stat)
		return device->ops->stat(device, st);

	return 0;
}

count_t device_len(device_t *device) {
	
	if (!device)
		return 0;

	if (device->ops->len)
		return device->ops->len(device);

	return 0;
}

