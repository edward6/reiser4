/*
	cat.c -- a programm which demonstrates reiserfs file access functions
	Copyright (C) 2001, 2002 Yury Umanets <torque@ukrpost.net>, see COPYING for 
	licensing and copyright details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <dal/file_dal.h>

#include <reiserfs/reiserfs.h>
#include <reiserfs/libprogs_tools.h>

int main(int argc, char *argv[]) {
	dal_t *dal;
	char *buffer;
	int error = 0;
	int64_t pos = 0;
	uint64_t buffer_size, readed_size = 0;
	
	reiserfs_fs_t *fs;
	reiserfs_file_t *file;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s DEV FILE [ pos ] \n", argv[0]);
		return 0xff;
	}

	if (!(dal = file_dal_open(argv[1], DEFAULT_BLOCK_SIZE, O_RDONLY))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Couldn't open device %s.", argv[1]);
		return 0xfe;    
	}
	
	if (!(fs = reiserfs_fs_open_fast(dal, dal))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Couldn't open filesystem on %s.", argv[1]);
		goto error_free_dal;
	}
	
	if (!(file = reiserfs_file_open(fs, argv[2], O_RDONLY))) {
	    libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
		    "Couldn't open file %s.", argv[2]); 
		goto error_free_fs;
	}
	
	if (argc == 4) {
		if ((pos = progs_strtol(argv[3], &error)) && error) {
			libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
				"Invalid position %s.", argv[3]); 
			goto error_free_file;
		}
	}
	
	if (!reiserfs_file_seek(file, pos)) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
		    "Couldn't seek file at %llu position.", pos); 
		goto error_free_file;
	}
	
	buffer_size = reiserfs_file_size(file) - reiserfs_file_offset(file);
	
	if (!(buffer = libreiserfs_malloc(buffer_size + 1)))
		goto error_free_file;
	
	memset(buffer, 0, buffer_size + 1);
	readed_size = reiserfs_file_read(file, buffer, buffer_size);
	
	if (readed_size != buffer_size) {
	    libreiserfs_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, 
		    "Couldn't read %llu bytes from file %s. Readed %llu bytes.", 
			reiserfs_file_size(file), argv[2], readed_size); 
	}

	fwrite(buffer, readed_size, 1, stdout);
	fflush(stdout);
	
	libreiserfs_free(buffer);
	reiserfs_file_close(file);
	
	reiserfs_fs_close(fs);
	file_dal_close(dal);
	
	return 0;

error_free_buffer:
	libreiserfs_free(buffer);
error_free_file:
	reiserfs_file_close(file);    
error_free_fs:
	reiserfs_fs_close(fs);
error_free_dal:
	file_dal_close(dal);
error:
	return 0xff;    
}

