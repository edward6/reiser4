/*
    info.c -- a demo program that demonstrates opening reiser4 and getting some information.
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

static void info_print_usage(void) {
    aal_printf("Usage: info FILE\n");
}

static void info_print_plugin(reiserfs_plugin_t *plugin) {
    aal_printf("%x:%x:%s\n(%s)\n\n", plugin->h.type, plugin->h.id, plugin->h.label, plugin->h.desc);
}

static void info_print_fs(reiserfs_fs_t *fs) {
    reiserfs_plugin_t *plugin;

    aal_printf("\nreiserfs %s, block size %u, blocks: %llu, used: %llu, free: %llu.\n\n", 
	reiserfs_fs_format(fs), reiserfs_fs_blocksize(fs), 
	reiserfs_format_get_blocks(fs->format), reiserfs_alloc_used(fs->alloc), 
	reiserfs_alloc_free(fs->alloc));

    aal_printf("Used plugins:\n-------------\n");

    aal_printf("(1) ");
    info_print_plugin(fs->format->plugin);
    
    if (fs->journal) {
	aal_printf("(2) ");
	info_print_plugin(fs->journal->plugin);
    }

    aal_printf("(3) ");
    info_print_plugin(fs->alloc->plugin);
    
    aal_printf("(4) ");
    info_print_plugin(fs->oid->plugin);
    
    aal_printf("(5) ");
    info_print_plugin(fs->key.plugin);
    
    aal_printf("(6) ");
    info_print_plugin(fs->tree->cache->node->node_plugin);
    
    aal_printf("(7) ");
    info_print_plugin(fs->dir->plugin);
}

int main(int argc, char *argv[]) {
    reiserfs_fs_t *fs;
    aal_device_t *device;

#ifndef ENABLE_COMPACT    
    
    if (argc < 2) {
	info_print_usage();
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
//    info_print_fs(fs);
    
    {
	reiserfs_entry_hint_t entry;
	reiserfs_object_t *obj;
	
	if (!(obj = reiserfs_object_open(fs, fs->dir->plugin, "/reiser4progs"))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't open directory.");
	    goto error_free_device;
	}
	
	if (reiserfs_object_rewind(obj)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't rewind directory.");
	    goto error_free_device;
	}

	while (!reiserfs_object_read(obj, &entry))
	    aal_printf("%llx:%llx %s\n", (entry.locality >> 4), entry.objectid, entry.name);

	reiserfs_object_close(obj);
    }
    
    reiserfs_fs_close(fs);
    libreiser4_done();
    aal_file_close(device);

    return 0;

error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_done();
error:
    
#endif
    
    return 0xff;
}

