/*
    ls.c -- a demo program which works like standard ls utility.
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
#include <reiser4/reiser4.h>

#include <misc.h>

static void ls_print_usage(void) {
    fprintf(stderr, "Usage: ls FILE DIR\n");
}

static void ls_setup_streams(void) {
    int i;
    for (i = 0; i < 5; i++)
	progs_exception_set_stream(i, stderr);
}

int main(int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_object_t *object;
    reiserfs_entry_hint_t entry;

#ifndef ENABLE_COMPACT    
    
    if (argc < 3) {
	ls_print_usage();
	return 0xfe;
    }
	
    ls_setup_streams();
    aal_exception_set_handler(progs_exception_handler);

    if (libreiser4_init(0)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	return 0xff;
    }
    
    if (!(device = aal_file_open(argv[1], REISERFS_DEFAULT_BLOCKSIZE, O_RDWR))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s.", argv[1]);
	goto error_free_libreiser4;
    }
    
    if (!(fs = reiserfs_fs_open(device, device, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open filesystem on %s.", aal_device_name(device));
	goto error_free_device;
    }
    
    if (!(object = reiserfs_dir_open(fs, argv[2]))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open dir \"%s\".", argv[2]);
	goto error_free_fs;
    }
    
    {
	reiserfs_plugin_t *dir_plugin;
	reiserfs_object_hint_t dir_hint;
	
	if (!(dir_plugin = libreiser4_factory_find_by_id(DIR_PLUGIN_TYPE, DIR_DIR40_ID)))
	    libreiser4_factory_failed(goto error_free_object, find, dir, DIR_DIR40_ID);
	
	dir_hint.statdata_pid = ITEM_STATDATA40_ID;
	dir_hint.sdext = SDEXT_UNIX_ID;

	dir_hint.direntry_pid = ITEM_CDE40_ID;
	dir_hint.hash_pid = HASH_R5_ID;
	
	{
	    int i;
	    char name[256];

	    for (i = 0; i < 19; i++) {
		aal_memset(name, 0, sizeof(name));
		aal_snprintf(name, 256, "testdir%d", i + 1);
		reiserfs_dir_create(fs, &dir_hint, dir_plugin, object, name);
	    }
	}
    }
    
    if (reiserfs_dir_rewind(object)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't rewind dir \"%s\".", argv[2]);
	goto error_free_object;
    }

    while (!reiserfs_dir_read(object, &entry)) {
	fprintf(stdout, "[%llx:%llx] %s\n", (entry.objid.locality >> 4), 
	    entry.objid.objectid, entry.name);
    }
    
    reiserfs_dir_close(object);
//    reiserfs_fs_sync(fs);

    reiserfs_fs_close(fs);
    libreiser4_done();
    aal_file_close(device);

    return 0;

error_free_object:
    reiserfs_dir_close(object);
error_free_fs:
    reiserfs_fs_close(fs);
error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_done();
error:
    
#endif
    
    return 0xff;
}

