/*
    mkreiserfs.c -- the program to create reiser4 filesystem.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <stdio.h>
#include <fcntl.h>

#include <reiserfs/reiserfs.h>

#define REISER40_PROFILE 0x1

static reiserfs_profile_t profile40 = {
    .label = "profile40",
    .desc = "Default profile for reiser4 filesystem",
    
    .node = 0x0,
    .item = {
	.internal = 0x3,
	.statdata = 0x0,
	.direntry = 0x2,
	.fileentry = 0x0
    },
    .file = 0x0,
    .dir = 0x0,
    .hash = 0x0,
    .tail = 0x0,
    .hook = 0x0,
    .perm = 0x0,
    .format = 0x0,
    .oid = 0x0,
    .alloc = 0x0,
    .journal = 0x0
};
    
static error_t mkfs_setup_profile(reiserfs_profile_t *profile, int number) {
    aal_assert("vpf-104", profile != NULL, return -1);

    switch (number) {
	case REISER40_PROFILE: {
	    *profile = profile40;
	    break;
	}
	default: {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Unknown profile has detected %x.", number);
	    return -1;
	}
    }
    return 0;
}

static void mkfs_print_usage(void) {
    fprintf(stderr, "Usage: mkreiserfs FILE\n");
}

int main(int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_profile_t profile;
    
    if (argc < 2) {
	mkfs_print_usage();
	return 0xfe;
    }
       
    if (libreiserfs_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiserfs.");
	goto error;
    }

    if (!(device = aal_file_open(argv[1], REISERFS_DEFAULT_BLOCKSIZE, O_RDWR))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s.", argv[1]);
	goto error_free_libreiserfs;
    }
    
    mkfs_setup_profile(&profile, REISER40_PROFILE);
    
    fprintf(stderr, "Creating filesystem with %s...", profile.label);
    fflush(stderr);
    
    if (!(fs = reiserfs_fs_create(device, &profile, 4096, NULL, 
	NULL, aal_device_len(device), device, NULL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create filesystem on %s.", argv[1]);
	goto error_free_device;
    }
    fprintf(stderr, "done\n");

    fprintf(stderr, "Synchronizing...");
    fflush(stderr);
    
    if (reiserfs_fs_sync(fs)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize created filesystem.");
	goto error_free_fs;
    }
	
    if (aal_device_sync(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize device %s.", argv[1]);
	goto error_free_fs;
    }

    fprintf(stderr, "done\n");
    
    reiserfs_fs_close(fs);
    libreiserfs_fini();
    aal_file_close(device);
    
    return 0;

error_free_fs:
    reiserfs_fs_close(fs);
error_free_device:
    aal_file_close(device);
error_free_libreiserfs:
    libreiserfs_fini();
error:
    return 0xff;
}
