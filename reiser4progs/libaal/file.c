/*
    file.c -- standard file device abstraction layer. It is used files functions
    to read or write into device.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT

/* This is needed for enabling such functions as lseek64, etc. */
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <aal/aal.h>

#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>

static void save_error(aal_device_t *device) {
    char *error;
    
    if(!device)
	return;

    if ((error = strerror(errno)))
	aal_strncpy(device->error, error, aal_strlen(error));
}

/*
    Handler for "read" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static errno_t file_read(aal_device_t *device, void *buff, blk_t block, 
    count_t count) 
{
    loff_t off, len;
	
    if (!device || !buff)
    	return -1;
	
    off = (loff_t)(block * device->blocksize);
    if (lseek64(*((int *)device->entity), off, SEEK_SET) == -1) {
	save_error(device);
	return errno;
    }

    len = (loff_t)(count * device->blocksize);
    if (read(*((int *)device->entity), buff, len) <= 0) {
	save_error(device);
	return errno;
    }
    return 0;
}

/*
    Handler for "write" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static errno_t file_write(aal_device_t *device, void *buff, blk_t block, count_t count) {
    loff_t off, len;
	
    if (!device || !buff)
	return -1;
	
    off = (loff_t)(block * device->blocksize);
    if (lseek64(*((int *)device->entity), off, SEEK_SET) == -1) {
	save_error(device);
	return errno;
    }
	
    len = (loff_t)(count * device->blocksize);
    if (write((*(int *)device->entity), buff, len) <= 0) {
	save_error(device);
	return errno;
    }
	
    return 0;
}

/*
    Handler for "sync" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static errno_t file_sync(aal_device_t *device) {

    if (!device) 
	return -1;
	
    if (fsync(*((int *)device->entity))) {
	save_error(device);
	return errno;
    }

    return 0;
}

/*
    Handler for "flags" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static int file_flags(aal_device_t *device) {

    if (!device) 
	return -1;
		
    return device->flags;
}

/*
    Handler for "equals" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static int file_equals(aal_device_t *device1, aal_device_t *device2) {

    if (!device1 || !device2)
	return 0;
	  
    return !aal_strncmp((char *)device1->data, 
	(char *)device2->data, aal_strlen((char *)device1->data));
}

/*
    Handler for "stat" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static uint32_t file_stat(aal_device_t *device) {
    struct stat st;
	
    if (!device)
	return 0;
	
    if (stat((char *)device->data, &st)) {
	save_error(device);
	return 0;
    }
    return (uint32_t)st.st_dev;
}

/*
    Handler for "len" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static count_t file_len(aal_device_t *device) {
    loff_t max_off = 0;
	
    if (!device) 
	return 0;
	
    if ((max_off = lseek64(*((int *)device->entity), 0, SEEK_END)) == (loff_t)-1) {
	save_error(device);
	return 0;
    }
    return (count_t)(max_off / device->blocksize);
}

/*
    Initializing the file device operations. They are used when any operation of 
    enumerated bellow is performed on a file device.
*/
static struct aal_device_ops ops = {
    .read = file_read, 
    .write = file_write, 
    .sync = file_sync, 
    .flags = file_flags, 
    .equals = file_equals, 
    .stat = file_stat, 
    .len = file_len
};

/*
    Opens actual file, initializes aal_device_t instance and returns it to caller.
    This function as well fille device at all is whidely used in all reiser4progs
    (mkfs, fsck, etc) for opening a device and working with them.
*/
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

/*
    This function reopens opened previously file device in order to change 
    flags, device was opened with.
*/
errno_t aal_file_reopen(aal_device_t *device, int flags) {
    int fd;
	
    if (!device) 
	return -1;

    close(*((int *)device->entity));
	
    if ((fd = open((char *)device->data, flags | O_LARGEFILE)) == -1) {
	save_error(device);
	return errno;
    }
    
    *((int *)device->entity) = fd;
    device->flags = flags;

    return 0;
}

/* 
    Closes file device. Close opened file descriptor, frees all assosiated memory.
    It is usualy called at end for work any utility from reiser4progs set.
*/
void aal_file_close(aal_device_t *device) {

    if (!device) 
	return;

    close(*((int *)device->entity));
    aal_free(device->entity);
    aal_device_close(device);
}

#endif

