/*
	file.c -- standard file device abstraction layer
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <aal/aal.h>

static int file_read(aal_device_t *device, void *buff, blk_t block, count_t count) {
	loff_t off, blocklen;
	
	if (!device || !buff)
		return 0;
	
	off = (loff_t)block * (loff_t)device->blocksize;
	
	if (lseek64((int)device->entity, off, SEEK_SET) == -1)
		return 0;

	blocklen = (loff_t)count * (loff_t)device->blocksize;
	
	if (read((int)device->entity, buff, blocklen) <= 0)
		return 0;
	
	return 1;
}

static int file_write(aal_device_t *device, void *buff, blk_t block, count_t count) {
	loff_t off, blocklen;
	
	if (!device || !buff)
		return 0;
	
	off = (loff_t)block * (loff_t)device->blocksize;
	
	if (lseek64((int)device->entity, off, SEEK_SET) == -1)
		return 0;

	blocklen = (loff_t)count * (loff_t)device->blocksize;
	
	if (write((int)device->entity, buff, blocklen) <= 0)
		return 0;
	
	return 1;
}

static int file_sync(aal_device_t *device) {

	if (!device) 
		return 0;
	
	return !fsync((int)device->entity);
}

static int file_flags(aal_device_t *device) {

	if (!device) 
		return 0;
		
	return device->flags;
}

static int file_equals(aal_device_t *device1, aal_device_t *device2) {

	if (!device1 || !device2)
	  return 0;
	  
	return !strcmp((char *)device1->data, (char *)device2->data);
}

static int file_stat(aal_device_t *device, struct stat *st) {
	
	if (!device || !st)
		return 0;
	
	if (stat((char *)device->data, st))
		return 0;

	return 1;
}

static count_t file_len(aal_device_t *device) {
	loff_t max_off = 0;
	
	if (!device) 
		return 0;
	
	if ((max_off = lseek64((int)device->entity, 0, SEEK_END)) == (loff_t)-1)
		return 0;
	
	return max_off / device->blocksize;
}

static struct aal_device_ops ops = {
	file_read, 
	file_write, 
	file_sync, 
	file_flags, 
	file_equals, 
	file_stat, 
	file_len
};

aal_device_t *aal_file_open(const char *file, size_t blocksize, int flags) {
	int fd;
	
	if (!file) 
		return NULL;
	
	if ((fd = open(file, flags | O_LARGEFILE)) == -1)
		return NULL;
	
	return aal_device_open(&ops, (void *)fd, blocksize, flags, (void *)file);
}

int aal_file_reopen(aal_device_t *device, int flags) {
	int fd;
	
	if (!device) 
		return 0;

	close((int)device->entity);
	
	if ((fd = open((char *)device->data, flags | O_LARGEFILE)) == -1)
		return 0;
	
	device->entity = (void *)fd;
	device->flags = flags;
	
	return 1;
}

void aal_file_close(aal_device_t *device) {

	if (!device) return;

	close((int)device->entity);
	aal_device_close(device);
}

#endif

