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

#include <aal/aal.h>

#include <fcntl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

/* Function for saving last error message into device assosiated buffer */
static void save_error(
    aal_device_t *device	    /* device, error will be saved into */
) {
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
static errno_t file_read(
    aal_device_t *device,	    /* file device for reading */
    void *buff,			    /* buffer data will be placed in */
    blk_t block,		    /* block number to be read from */
    count_t count		    /* number of blocks to be read */
) {
    loff_t off, len;
	
    if (!device || !buff)
    	return -1;
    
    /* 
	Positioning inside file. As configure script defines __USE_FILE_OFFSET64 
	macro inside config.h file, lseek function will be mapped into lseek64 
	one.
    */
    off = (loff_t)(block * device->blocksize);
    if (lseek(*((int *)device->entity), off, SEEK_SET) == -1) {
	save_error(device);
	return errno;
    }

    /* Reading data form file */
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
static errno_t file_write(
    aal_device_t *device,	    /* file device, data will be wrote onto */
    void *buff,			    /* buffer, data stored in */
    blk_t block,		    /* start position for writing */
    count_t count		    /* number of blocks to be write */
) {
    loff_t off, len;
	
    if (!device || !buff)
	return -1;
	
    /* Positioning inside file */
    off = (loff_t)(block * device->blocksize);
    if (lseek(*((int *)device->entity), off, SEEK_SET) == -1) {
	save_error(device);
	return errno;
    }
    
    /* Writing into file */
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
static errno_t file_sync(
    aal_device_t *device	    /* file device to be synchronized */
) {

    if (!device) 
	return -1;
	
    /* As this is file device, we are using fsync function for synchronizing file */
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
static int file_flags(
    aal_device_t *device	    /* file device, flags will be obtained from */
) {

    if (!device) 
	return -1;
		
    return device->flags;
}

/*
    Handler for "equals" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static int file_equals(
    aal_device_t *device1,	    /* the first device for comparing */
    aal_device_t *device2	    /* the second one */
) {

    if (!device1 || !device2)
	return 0;
    
    /* Devices are comparing by comparing their file names */
    return !aal_strncmp((char *)device1->data, 
	(char *)device2->data, aal_strlen((char *)device1->data));
}

/*
    Handler for "stat" operation for use with file device. See bellow for 
    understanding where it is used. 
*/
static uint32_t file_stat(
    aal_device_t *device	    /* file device to be stated */
) {
    struct stat st;
	
    if (!device)
	return 0;
    
    /* Stating file device by using standard "stat" function */
    if (stat((char *)device->data, &st)) {
	save_error(device);
	return 0;
    }
    return (uint32_t)st.st_dev;
}

#if defined(__linux__) && defined(_IOR) && !defined(BLKGETSIZE64)
#   define BLKGETSIZE64 _IOR(0x12, 114, sizeof(uint64_t))
#endif

/*
    Handler for "len" operation for use with file device. See bellow for 
    understanding where it is used.
*/
static count_t file_len(
    aal_device_t *device	    /* file device, lenght will be obtained from */
) {
    uint64_t size;
    loff_t max_off = 0;

    if (!device) 
	return 0;
    
#ifdef BLKGETSIZE64
    
    if (ioctl(*((int *)device->entity), BLKGETSIZE64, &size) >= 0)
        return (count_t)(size / device->blocksize);
    
#endif

#ifdef BLKGETSIZE    
    
    if (ioctl(*((int *)device->entity), BLKGETSIZE, &size) >= 0)
        return (count_t)(size / (device->blocksize / 512));
    
#endif
    
    if ((max_off = lseek(*((int *)device->entity), 0, SEEK_END)) == (loff_t)-1) {
	save_error(device);
	return 0;
    }
    
    return (count_t)(max_off / device->blocksize);
}

/*
    Initializing the file device operations. They are used when any operation of 
    enumerated bellow is performed on a file device. Here is the heart of file 
    the device. It is pretty simple. The same as in linux implemented abstraction 
    from interrupt controller.
*/
static struct aal_device_ops ops = {
    .read = file_read,		    /* handler for "read" operation */	    
    .write = file_write,	    /* handler for "write" operation */
    .sync = file_sync,		    /* handler for "sync" operation */
    .flags = file_flags,	    /* handler for "flags" obtaining */
    .equals = file_equals,	    /* handler for comparing two devices */
    .stat = file_stat,		    /* hanlder for stating device */
    .len = file_len		    /* handler for length obtaining */
};

/*
    Opens actual file, initializes aal_device_t instance and returns it to caller.
    This function as well fille device at all is whidely used in all reiser4progs
    (mkfs, fsck, etc) for opening a device and working with them.
*/
aal_device_t *aal_file_open(
    const char *file,		    /* name of file to be used as file device */
    uint16_t blocksize,		    /* used blocksize */
    int flags			    /* flags file will be opened with */
) {
    int fd;
    aal_device_t *device;
	
    if (!file) 
	return NULL;
    
    /* Opening specified file with specified flags */
#if defined(O_LARGEFILE)
    if ((fd = open(file, flags | O_LARGEFILE)) == -1)
#else
    if ((fd = open(file, flags)) == -1)
#endif
	return NULL;
    
    /* Initializing wrapper aal_device for it */
    device = aal_device_open(&ops, blocksize, flags, (void *)file);
    aal_strncpy(device->name, file, aal_strlen(file));

    /* Initializing device entity (file descripror in the case of file device) */
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
errno_t aal_file_reopen(
    aal_device_t *device,	    /* file device to be reopened */
    int flags			    /* flags device will be reopened with */
) {
    int fd;
	
    if (!device) 
	return -1;

    /* Close previously opened entity (file descriptor) */
    close(*((int *)device->entity));
    
    /* Reopening file */
#if defined(O_LARGEFILE)
    if ((fd = open((char *)device->data, flags | O_LARGEFILE)) == -1) {
#else
    if ((fd = open((char *)device->data, flags)) == -1) {
#endif
	save_error(device);
	return errno;
    }
    
    /* Reinitializing entity */
    *((int *)device->entity) = fd;
    device->flags = flags;

    return 0;
}

/* 
    Closes file device. Close opened file descriptor, frees all assosiated memory.
    It is usualy called at end for work any utility from reiser4progs set.
*/
void aal_file_close(
    aal_device_t *device	    /* file device to be closed */
) {

    if (!device) 
	return;

    /* Closing entity (file descriptor) */
    close(*((int *)device->entity));

    /* Closing device and freeing all assosiated memory */
    aal_free(device->entity);
    aal_device_close(device);
}

#endif

