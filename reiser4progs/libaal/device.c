/*
    device.c -- device independent interface and block-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

/* 
    This macro is used for checking whether specified routine from the device 
    operations is implemented or not. If not, it throws exception and performs 
    specified action.

    It is using in the following maner:

    aal_device_check_routine(some_devive_instance, read_operation, 
	goto error_processing);
    
    This macro was introdused to decrease source code by removing a lot of common 
    pieces and replace them by just one line of macro.
*/
#define aal_device_check_routine(device, routine, action)		    \
    do {								    \
	if (!device->ops->routine) {					    \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,		    \
		"Device operation \"" #routine "\" isn't implemented.");    \
	    action;							    \
	}								    \
    } while (0)

/*
    Initializes device instance, checks and sets all device attributes (blocksize, 
    flags, etc) and returns initialized instance to caller. 
*/
aal_device_t *aal_device_open(
    struct aal_device_ops *ops, /* pointer to device operations */
    uint16_t blocksize,		/* block size device is working with */
    int flags,			/* flags device opened with (O_RDONLY, etc) */
    void *data			/* some caller specified data */
) {
    aal_device_t *device;

    aal_assert("umka-429", ops != NULL, return NULL);
    
    /* Rough check for blocksize validness */
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Block size %u isn't power of two.", blocksize);
	return NULL;
    }	
    
    /* Allocating memory for device instance and initializing all fields */
    if (!(device = (aal_device_t *)aal_calloc(sizeof(*device), 0)))
	return NULL;

    device->ops = ops;
    device->data = data;
    device->flags = flags;
    device->blocksize = blocksize;
	
    return device;
}

/* Closes device. Frees all assosiated memory */
void aal_device_close(
    aal_device_t *device	/* device to be closed */
) {
    aal_assert("umka-430", device != NULL, return);
    aal_free(device);
}

/* 
    Checks and sets new block size for specified device. Returns error code, see 
    aal.h for more detailed description of errno_t.
*/
errno_t aal_device_set_bs(
    aal_device_t *device,	/* device to be set with passed blocksize */
    uint16_t blocksize		/* new blocksize value */
) {

    aal_assert("umka-431", device != NULL, return -1);
	
    if (!aal_pow_of_two(blocksize)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Block size %u isn't power of two.", blocksize);
	return -1;
    }	
    device->blocksize = blocksize;
	
    return 0;
}

/* Returns current block size from specified device */
uint16_t aal_device_get_bs(
    aal_device_t *device	/* device instance blocksize will be received from */
) {

    aal_assert("umka-432", device != NULL, return 0);

    return device->blocksize;
}

/* 
    Performs read operation on specified device. Actualy it calls corresponding
    operation (read) from assosiated with device operations. Returns error code,
    see aal.h for more detailed description of errno_t.
*/
errno_t aal_device_read(
    aal_device_t *device,	/* device instance we will read from */
    void *buff,			/* buffer we will read into */
    blk_t block,		/* block number to be read from */
    count_t count)		/* count of blocks to be read */
{
    aal_assert("umka-433", device != NULL, return -1);

    aal_device_check_routine(device, read, return -1);
    return device->ops->read(device, buff, block, count);
}

/* 
    Performs write operation on specified device. Actualy it calls corresponding
    operation (write) from assosiated with device operations. Returns error code,
    see aal.h for more detailed description of errno_t.
*/
errno_t aal_device_write(
    aal_device_t *device,	/* device instance we will write into */
    void *buff,			/* buffer with data to be wrote */
    blk_t block,		/* block we will write to */
    count_t count		/* number of blocks to be wrote */
) {
    aal_assert("umka-434", device != NULL, return -1);
    aal_assert("umka-435", buff != NULL, return -1);
	
    aal_device_check_routine(device, write, return -1);
    return device->ops->write(device, buff, block, count);
}

/* 
    Performs sync operation on specified device. Actualy it calls corresponding
    operation (sync) from assosiated with device operations. Returns error code,
    see aal.h for more detailed description of errno_t.
*/
errno_t aal_device_sync(
    aal_device_t *device	/* device instance that will be synchronized */
) {
    aal_assert("umka-436", device != NULL, return -1);
    
    aal_device_check_routine(device, sync, return -1);
    return device->ops->sync(device);
}

/* Returns flags, device was opened with */
int aal_device_flags(
    aal_device_t *device	/* device instance flags will be obtained from */
) {
    aal_assert("umka-437", device != NULL, return -1);

    aal_device_check_routine(device, flags, return -1);
    return device->ops->flags(device);
}

/* 
    Compares two devices. Returns TRUE for equal devices and FALSE for different 
    ones. This function is needed in order to be aware is host device user has 
    specified corresponds journal one. And in some other cases.
*/
int aal_device_equals(
    aal_device_t *device1,	/* first device for comparing */
    aal_device_t *device2	/* second one */
) {
    aal_assert("umka-438", device1 != NULL, return 0);
    aal_assert("umka-439", device2 != NULL, return 0);
	
    aal_device_check_routine(device1, equals, return 0);
    return device1->ops->equals(device1, device2);
}

/* 
    Retuns device stat information by calling "stat" operation from specified
    device instance.
*/
uint32_t aal_device_stat(
    aal_device_t *device	/* device instance to be stated */
) {
    aal_assert("umka-440", device != NULL, return 0);
	
    aal_device_check_routine(device, stat, return 0);
    return device->ops->stat(device);
}

/* Returns device length in blocks */
count_t aal_device_len(
    aal_device_t *device	/* device, length in blocks will be obtained from */
) {
    aal_assert("umka-441", device != NULL, return 0);	

    aal_device_check_routine(device, len, return 0);
    return device->ops->len(device);
}

/* Returns device name. For standard file it is file name */
char *aal_device_name(
    aal_device_t *device	/* device, name will be obtained from */
) {
    aal_assert("umka-442", device != NULL, return NULL);
    
    return device->name;
}

/* Returns last error occured on device */
char *aal_device_error(
    aal_device_t *device	/* device error description will be obtailed from */
) {
    aal_assert("umka-752", device != NULL, return NULL);
    return device->error;
}

