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

static void meat_print_usage(void) {
    fprintf(stderr, "Usage: <open|create> DEV\n");
}

static void meat_print_plugin(reiserfs_plugin_t *plugin) {
    aal_printf("%x:%x (%s)\n", plugin->h.type, plugin->h.id, plugin->h.label);
}

static void meat_print_fs(reiserfs_fs_t *fs) {
    reiserfs_plugin_t *plugin;

    aal_printf("\nreiserfs %s, block size %u, blocks: %llu, used: %llu, free: %llu.\n\n", 
	reiserfs_fs_format(fs), reiserfs_fs_blocksize(fs), 
	reiserfs_format_get_blocks(fs), reiserfs_alloc_used(fs), 
	reiserfs_alloc_free(fs));

    aal_printf("Used plugins:\n-------------\n");

    aal_printf("(1) ");
    meat_print_plugin(fs->format->plugin);
    
    if (fs->journal) {
	aal_printf("(2) ");
	meat_print_plugin(fs->journal->plugin);
    }

    aal_printf("(3) ");
    meat_print_plugin(fs->alloc->plugin);
    
    aal_printf("(4) ");
    meat_print_plugin(fs->oid->plugin);
}

int main(int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;

#ifndef ENABLE_COMPACT    
    
    if (argc < 2) {
	meat_print_usage();
	return 0xfe;
    }
	
    if (libreiserfs_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiserfs.");
	return 0xff;
    }
    
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
    meat_print_fs(fs);

    reiserfs_fs_close(fs);
    libreiserfs_fini();
    aal_file_close(device);

    return 0;

error_free_device:
    aal_file_close(device);
error_free_libreiserfs:
    libreiserfs_fini();
error:
    
#endif
    
    return 0xff;
}

