/*
    meat.c -- a demo program that works with libreiser4.
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

static void meat_print_usage(void) {
    aal_printf("Usage: meat FILE\n");
}

static void meat_print_plugin(reiserfs_plugin_t *plugin) {
    aal_printf("%x:%x:%s\n(%s)\n", plugin->h.type, plugin->h.id, plugin->h.label, plugin->h.desc);
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
    
    aal_printf("(5) ");
    meat_print_plugin(fs->tree->root_node->plugin);
    
    if (fs->tree->dir_plugin) {
	aal_printf("(6) ");
	meat_print_plugin(fs->tree->dir_plugin);
    }
}

int main(int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;

#ifndef ENABLE_COMPACT    
    
    if (argc < 2) {
	meat_print_usage();
	return 0xfe;
    }
	
    if (libreiser4_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	return 0xff;
    }
    
    if (!(device = aal_file_open(argv[1], REISERFS_DEFAULT_BLOCKSIZE, O_RDONLY))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s.", argv[1]);
	goto error_free_libreiser4;
    }
    
    if (!(fs = reiserfs_fs_open(device, device, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open filesystem on %s.", aal_device_name(device));
	goto error_free_device;
    }
    meat_print_fs(fs);

    reiserfs_fs_close(fs);
    libreiser4_fini();
    aal_file_close(device);

    return 0;

error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_fini();
error:
    
#endif
    
    return 0xff;
}

