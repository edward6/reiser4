/*
	meat.c -- a demo program that works with libreiserfs.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <stdio.h>
#include <fcntl.h>

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

int main(int argc, char *argv[]) {
	reiserfs_fs_t *fs;
	aal_device_t *device;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s DEV\n", argv[0]);
		return 0xfe;
	}
	
	if (!(device = aal_file_open(argv[1], REISERFS_DEFAULT_BLOCKSIZE, O_RDONLY))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "", 
			"Can't open device %s.", argv[1]);
		return 0xff;
	}
	
	if (!libreiserfs_init()) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "", 
			"Can't initialize libreiserfs.");
		goto error_free_device;
	}
	
	if (!(fs = reiserfs_fs_open(device, device, 0))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "", 
			"Can't open filesystem on %s.", argv[1]);
		goto error_free_device;
	}
	
	fprintf(stderr, "Reiserfs was successfully oneped on %s.\n", argv[1]);
	
	reiserfs_fs_close(fs, 0);
	libreiserfs_done();
	
	aal_file_close(device);
	
	return 0;
	
error_free_device:
	aal_file_close(device);
error:
	return 0xff;
}
