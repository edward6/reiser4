/*
    meat.c -- a demo program that works with libreiserfs.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <stdio.h>
#  include <fcntl.h>
#endif

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

static void usage(void) {
    fprintf(stderr, "Usage: meat DEV <open|create>\n");
}

int main(int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;

#ifndef ENABLE_COMPACT    
    
    if (argc < 3) {
	usage();
	return 0xfe;
    }
	
    if (aal_strncmp(argv[1], "open", 4) && aal_strncmp(argv[1], "create", 6)) {
	usage();
	return 0xfe;
    }
	
    if (libreiserfs_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiserfs.");
	return 0xff;
    }
    
    if (aal_strncmp(argv[1], "open", 4) == 0) {
	    
	if (!(device = aal_file_open(argv[2], REISERFS_DEFAULT_BLOCKSIZE, O_RDONLY))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't open device %s.", argv[2]);
	    goto error_free_libreiserfs;
	}
    
	if (!(fs = reiserfs_fs_open(device, device, 0))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't %s filesystem on %s.", argv[1], argv[2]);
	    goto error_free_device;
	}
	
	aal_printf("Found reiserfs %s, block size %d.\n", reiserfs_fs_format(fs), 
	    reiserfs_fs_blocksize(fs));
	
	reiserfs_fs_close(fs, 0);
    } else {
	if (!(device = aal_file_open(argv[2], REISERFS_DEFAULT_BLOCKSIZE, O_RDWR))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't open device %s.", argv[2]);
	    goto error_free_libreiserfs;
	}
    
	if (!(fs = reiserfs_fs_create(device, 0x1, 0x1, 4096, "0000000000000000", argv[2], 
	    aal_device_len(device), device, NULL))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't create filesystem on %s.", argv[2]);
	    goto error_free_device;
	}
	
	reiserfs_fs_close(fs, 1);
	
	aal_printf("Filesystem on %s successfully created.\n", argv[2]);
    }
    
    libreiserfs_fini();
    
    aal_file_close(device);
    return 0;

error_free_libreiserfs:
    libreiserfs_fini();
error_free_device:
    aal_file_close(device);
error:
    
#endif
    
    return 0xff;
}

