/*
	device.c -- device abstraction layer.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <sys/types.h>
#include <sys/stat.h>

#include <aal/aal.h>

aal_device_t *aal_device_open(struct aal_device_ops *ops, const void *entity, 
	size_t blocksize, int flags, void *data) 
{
	aal_device_t *device;
	
	if (!ops) 
		return NULL;
	
	if (!aal_pow_of_two(blocksize)) {
		aal_printf("Block size %d isn't power of two.\n", blocksize);
		return NULL;
	}	
	
	if (!(device = (aal_device_t *)aal_malloc(sizeof(*device))))
		return NULL;

	device->ops = ops;
	device->data = data;
	device->flags = flags;
	device->entity = entity;
	device->blocksize = blocksize;
	
	return device;
}

void aal_device_close(aal_device_t *device) {
	
	if (!device) 
		return;
	
	device->ops = NULL;
	device->entity = NULL;
	device->data = NULL;
	aal_free(device);
}

int aal_device_set_blocksize(aal_device_t *device, size_t blocksize) {

	if (!device) 
		return 0;
	
	if (!aal_pow_of_two(blocksize)) {
		aal_printf("Block size %d isn't power of two.\n", blocksize);
		return 0;
	}	
	
	device->blocksize = blocksize;
	
	return 1;
}

size_t aal_device_blocksize(aal_device_t *device) {

	if (!device) 
		return 0;

	return device->blocksize;
}

int aal_device_read(aal_device_t *device, void *buff, blk_t block, count_t count) {

	if (!device) 
		return 0;

	if (device->ops->read)
		return device->ops->read(device, buff, block, count);
	
	return 0;
}

int aal_device_write(aal_device_t *device, void *buff, blk_t block, count_t count) {

	if (!device) 
		return 0;
	
	if (device->ops->write)
		return device->ops->write(device, buff, block, count);
		
	return 0;
}
	
int aal_device_sync(aal_device_t *device) {

	if (!device) 
		return 0;

	if (device->ops->sync)
		return device->ops->sync(device);
	
	return 0;	
}

int aal_device_flags(aal_device_t *device) {

	if (!device) 
		return 0;

	if (device->ops->flags)
		return device->ops->flags(device);
	
	return 0;
}

int aal_device_equals(aal_device_t *device1, aal_device_t *device2) {
	
	if (!device1 || !device2) 
		return 0;

	if (device1->ops->equals)
		return device1->ops->equals(device1, device2);
	
	return 0;
}

int aal_device_stat(aal_device_t *device, struct stat* st) {

	if (!device)
		return 0;
	
	if (device->ops->stat)
		return device->ops->stat(device, st);

	return 0;
}

count_t aal_device_len(aal_device_t *device) {
	
	if (!device)
		return 0;

	if (device->ops->len)
		return device->ops->len(device);

	return 0;
}

