/*
	filesystem.h -- reiserfs filesystem structures and macros.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <aal/aal.h>
#include <reiserfs/plugin.h>

#define REISERFS_MASTER_MAGIC 		"R4Sb"

#define REISERFS_MASTER_OFFSET 		65536
#define REISERFS_DEFAULT_BLOCKSIZE 	4096

struct reiserfs_master_super {
	char mr_magic[4];
	uint16_t mr_format_id;
	uint16_t mr_blocksize;
	char mr_uuid[16];
};

#define get_mr_format_id(mr)		get_le16(mr, mr_format_id)
#define get_mr_blocksize(mr)		get_le16(mr, mr_blocksize)

struct reiserfs_super {
	struct reiserfs_master_super master;
		
	reiserfs_format_opaque_t *entity;
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
	reiserfs_plugin_t *plugin;
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

