/*
	ls.c -- a programm which demonstrates reiserfs dir access functions
	Copyright (C) 2001, 2002 Yury Umanets <torque@ukrpost.net>, see COPYING for 
	licensing and copyright details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <dal/file_dal.h>

#include <reiserfs/reiserfs.h>
#include <reiserfs/libprogs_tools.h>

int main(int argc, char *argv[]) {
	dal_t *dal;
	reiserfs_fs_t *fs;
	reiserfs_dir_t *dir;
	reiserfs_path_node_t *leaf;
	reiserfs_dir_entry_t entry;
	
	int32_t offset = 0;
	int error = 0;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s DEV DIR [ FROM ]\n", argv[0]);
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
	
	if (!(dir = reiserfs_dir_open(fs, argv[2]))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Couldn't open dir %s.", argv[2]);
		goto error_free_fs;
	}	
	
	if (argc > 3 && (offset = progs_strtol(argv[3], &error)) && error) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid offset %s.", argv[3]);
		goto error_free_fs;
	}
	
	if (!reiserfs_dir_seek(dir, (uint32_t)offset)) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Couldn't seek to %lu. Current position is %lu.", offset, 
			reiserfs_dir_offset(dir));
		goto error_free_dir;
	}
	
	while (reiserfs_dir_read(dir, &entry))
	    printf("%s\n", entry.de_name);
	
	reiserfs_dir_close(dir);
	
	reiserfs_fs_close(fs);
	file_dal_close(dal);
	
	return 0;

error_free_dir:
	reiserfs_dir_close(dir);
error_free_fs:
	reiserfs_fs_close(fs);
error_free_dal:
	file_dal_close(dal);
error:
	return 0xff;    
}

