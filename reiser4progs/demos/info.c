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

#include <misc/misc.h>

static void info_print_usage(void) {
    fprintf(stderr, "Usage: info FILE\n");
}

static void info_print_plugin(reiser4_plugin_t *plugin) {
    fprintf(stderr, "0x%x:0x%x:%s\n(%s)\n\n", plugin->h.type, plugin->h.id, plugin->h.label, plugin->h.desc);
}

static void info_print_fs(reiser4_fs_t *fs) {
    reiser4_plugin_t *plugin;

    fprintf(stderr, "\nreiserfs %s, block size %u, blocks: %llu, used: %llu, free: %llu.\n\n", 
	reiser4_fs_name(fs), reiser4_fs_blocksize(fs), 
	reiser4_format_get_len(fs->format), reiser4_alloc_used(fs->alloc), 
	reiser4_alloc_free(fs->alloc));

    fprintf(stderr, "Used plugins:\n-------------\n");

    fprintf(stderr, "(1) ");
    info_print_plugin(fs->format->entity->plugin);
    
    if (fs->journal) {
	fprintf(stderr, "(2) ");
	info_print_plugin(fs->journal->entity->plugin);
    }

    fprintf(stderr, "(3) ");
    info_print_plugin(fs->alloc->entity->plugin);
    
    fprintf(stderr, "(4) ");
    info_print_plugin(fs->oid->entity->plugin);
    
    fprintf(stderr, "(5) ");
    info_print_plugin(fs->tree->key.plugin);
    
    fprintf(stderr, "(6) ");
    info_print_plugin(fs->tree->cache->node->entity->plugin);
    
    fprintf(stderr, "(7) ");
    info_print_plugin(fs->dir->entity->plugin);
}

int main(int argc, char *argv[]) {
    reiser4_fs_t *fs;
    aal_device_t *device;

#ifndef ENABLE_COMPACT    
    
    if (argc < 2) {
	info_print_usage();
	return 0xfe;
    }
    
    aal_exception_set_handler(progs_exception_handler);
    
    if (libreiser4_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	return 0xff;
    }
    
    {
	int i;
	for (i = 0; i < 5; i++)
	    progs_exception_set_stream(i, stderr);
    }
    
    if (!(device = aal_file_open(argv[1], REISER4_DEFAULT_BLOCKSIZE, O_RDONLY))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s.", argv[1]);
	goto error_free_libreiser4;
    }
    
    if (!(fs = reiser4_fs_open(device, device, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open filesystem on %s.", aal_device_name(device));
	goto error_free_device;
    }
    info_print_fs(fs);
    
    reiser4_fs_close(fs);
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

