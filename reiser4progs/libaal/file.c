/*
    file.c -- standard file device abstraction layer.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <unistd.h>

#include <aal/aal.h>

static error_t file_read(aal_device_t *device, void *buff, blk_t block, count_t count) {
    loff_t off, len;
	
    if (!device || !buff)
    	return -1;
	
    off = (loff_t)(block * device->blocksize);
    if (lseek64(*((int *)device->entity), off, SEEK_SET) == -1)
	return -1;

    len = (loff_t)(count * device->blocksize);
    if (read(*((int *)device->entity), buff, len) <= 0)
	return -1;
	
    return 0;
}

static error_t file_write(aal_device_t *device, void *buff, blk_t block, count_t count) {
    loff_t off, len;
	
    if (!device || !buff)
	return -1;
	
    off = (loff_t)(block * device->blocksize);
    if (lseek64(*((int *)device->entity), off, SEEK_SET) == -1)
	return -1;
    
    len = (loff_t)(count * device->blocksize);
    if (write((*(int *)device->entity), buff, len) <= 0)
	return -1;
	
    return 0;
}

static error_t file_sync(aal_device_t *device) {

    if (!device) 
	return -1;
	
    return fsync(*((int *)device->entity));
}

static int file_flags(aal_device_t *device) {

    if (!device) 
	return -1;
		
    return device->flags;
}

static int file_equals(aal_device_t *device1, aal_device_t *device2) {

    if (!device1 || !device2)
	return 0;
	  
    return !aal_strncmp((char *)device1->data, 
	(char *)device2->data, aal_strlen((char *)device1->data));
}

static uint32_t file_stat(aal_device_t *device) {
    struct stat st;
	
    if (!device)
	return 0;
	
    if (stat((char *)device->data, &st))
	return 0;

    return (uint32_t)st.st_dev;
}

static count_t file_len(aal_device_t *device) {
    loff_t max_off = 0;
	
    if (!device) 
	return 0;
	
    if ((max_off = lseek64(*((int *)device->entity), 0, SEEK_END)) == (loff_t)-1)
	return 0;
	
    return (count_t)(max_off / device->blocksize);
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

aal_device_t *aal_file_open(const char *file, uint16_t blocksize, int flags) {
    int fd;
    aal_device_t *device;
	
    if (!file) 
	return NULL;
	
    if ((fd = open(file, flags | O_LARGEFILE)) == -1)
	return NULL;
	
    device = aal_device_open(&ops, blocksize, flags, (void *)file);
    aal_strncpy(device->name, file, aal_strlen(file));

    if (!(device->entity = aal_calloc(sizeof(int), 0)))
	goto error_free_device;

    *((int *)device->entity) = fd;
    
    return device;
    
error_free_device:
    aal_device_close(device);
error:
    return NULL;    
}

error_t aal_file_reopen(aal_device_t *device, int flags) {
    int fd;
	
    if (!device) 
	return -1;

    close(*((int *)device->entity));
	
    if ((fd = open((char *)device->data, flags | O_LARGEFILE)) == -1)
	return -1;
	
    *((int *)device->entity) = fd;
    device->flags = flags;
	
    return 0;
}

void aal_file_close(aal_device_t *device) {

    if (!device) 
	return;

    close(*((int *)device->entity));
    aal_free(device->entity);
    aal_device_close(device);
}

#endif

