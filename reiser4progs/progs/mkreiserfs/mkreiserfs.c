/*
    mkreiserfs.c -- the program to create reiser4 filesystem.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/
#include <stdio.h>
#include <fcntl.h>
#include <reiserfs/reiserfs.h>

#define REISER40_PROFILE 1

static void set_base_plugin_ids (reiserfs_default_plugin_t *def_plugs, int profile) {
    reiserfs_default_plugin_t reiser40 = 
	    {	0, /* node */ 
		{ /* item */
		    0 /* internal */,
		    0 /* stat */,
		    0 /* dir_item */,
		    0 /* file_item */
		},
		0 /* file */,
		0 /* dir */,
		0 /* hash */,
		0 /* tail */,
		0 /* hook */,
		0 /* perm */,
		0 /* format */,
		0 /* oid */,
		0 /* alloc */,
		0 /* journal */
	    };
    
    aal_assert("vpf-104", def_plugs != NULL, return);

    switch (profile) {
	case REISER40_PROFILE:
	    *def_plugs = reiser40;
	    break;
	default:
    }
}

static void print_usage (void) {
    fprintf (stderr, "Usage: mkreiserfs device");
}

static void print_plugin(reiserfs_plugin_t *plugin) {
    aal_printf("%x:%x (%s)\n", plugin->h.type, plugin->h.id, plugin->h.label);
    aal_printf("%s\n\n", plugin->h.desc);
}

static void print_fs(reiserfs_fs_t *fs) {
    reiserfs_plugin_t *plugin;

    aal_printf("reiserfs %s, block size %u, blocks: %llu, used: %llu, free: %llu.\n\n", 
	reiserfs_fs_format(fs), reiserfs_fs_blocksize(fs), 
	reiserfs_format_get_blocks(fs), reiserfs_alloc_used(fs), 
	reiserfs_alloc_free(fs));
    
    print_plugin(fs->format->plugin);
    
    if (fs->journal)
	print_plugin(fs->journal->plugin);

    print_plugin(fs->alloc->plugin);
}

int main (int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;
    int ret;
    reiserfs_default_plugin_t default_plugins;
    
    if (libreiserfs_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiserfs.");
	ret = -1;
	goto error;
    }

    if (!(device = aal_file_open(argv[1], REISERFS_DEFAULT_BLOCKSIZE, O_RDWR))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s.", argv[1]);
	ret = -2;
	goto error_free_libreiserfs;
    }
    
    set_base_plugin_ids(&default_plugins, REISER40_PROFILE);
    
    if (!(fs = reiserfs_fs_create_2(device, &default_plugins, 4096, "test-uuid", 
	"test-label", aal_device_len(device), device, NULL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create filesystem on %s.", argv[1]);
	goto error_free_device;
    }

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
    
    print_fs(fs);

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
    return -2;
}
