/*
	filesystem.h -- reiserfs filesystem structures and macros.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <aal/aal.h>
#include <reiserfs/plugin.h>

struct reiserfs_super {
	reiserfs_layout_opaque_t *entity;
	reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_super reiserfs_super_t;

struct reiserfs_journal {
	aal_device_t *device;
	reiserfs_journal_opaque_t *entity;
	reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_journal reiserfs_journal_t;

struct reiserfs_alloc {
	reiserfs_alloc_opaque_t *entity;
	reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_alloc reiserfs_alloc_t;

struct reiserfs_tree {
};

typedef struct reiserfs_tree reiserfs_tree_t;

struct reiserfs_fs {
	aal_device_t *device;
	reiserfs_super_t *super;
	reiserfs_journal_t *journal;
	reiserfs_alloc_t *alloc;
	reiserfs_tree_t *tree;
};

typedef struct reiserfs_fs reiserfs_fs_t;

extern reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
	aal_device_t *journal_device, int replay);

extern void reiserfs_fs_close(reiserfs_fs_t *fs);

#endif

