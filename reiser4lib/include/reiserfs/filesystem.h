/*
    filesystem.h -- reiserfs filesystem structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <aal/aal.h>
#include <reiserfs/plugin.h>
#include <reiserfs/node.h>

#define REISERFS_DEFAULT_BLOCKSIZE	4096
#define REISERFS_MASTER_OFFSET		65536
#define REISERFS_MASTER_MAGIC		"R4Sb"

struct reiserfs_master {
    char mr_magic[4];
    uint16_t mr_format_id;
    uint16_t mr_blocksize;
    char mr_uuid[16];
    char mr_label[16];
};

typedef struct reiserfs_master reiserfs_master_t;

#define get_mr_format_id(mr)		get_le16(mr, mr_format_id)
#define set_mr_format_id(mr, val)	set_le16(mr, mr_format_id, val)

#define get_mr_block_size(mr)		get_le16(mr, mr_blocksize)
#define set_mr_block_size(mr, val)	set_le16(mr, mr_blocksize, val)

struct reiserfs_super {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_super reiserfs_super_t;

struct reiserfs_journal {
    aal_device_t *device;
    
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_journal reiserfs_journal_t;

struct reiserfs_alloc {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_alloc reiserfs_alloc_t;

struct reiserfs_tree {
    reiserfs_node_t *root;
};

typedef struct reiserfs_tree reiserfs_tree_t;

struct reiserfs_fs {
    aal_device_t *device;
    
    reiserfs_master_t *master;
    reiserfs_super_t *super;
    reiserfs_journal_t *journal;
    reiserfs_alloc_t *alloc;
    reiserfs_tree_t *tree;
};

typedef struct reiserfs_fs reiserfs_fs_t;

/* Public functions */
extern reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay);

extern void reiserfs_fs_close(reiserfs_fs_t *fs, int sync);
extern error_t reiserfs_fs_sync(reiserfs_fs_t *fs);
	
extern reiserfs_fs_t *reiserfs_fs_create(aal_device_t *host_device, 
    reiserfs_plugin_id_t format_plugin_id, reiserfs_plugin_id_t node_plugin_id,
    size_t blocksize, const char *uuid, const char *label, count_t len, 
    aal_device_t *journal_device, reiserfs_params_opaque_t *journal_params);

extern const char *reiserfs_fs_format(reiserfs_fs_t *fs);
extern uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_fs_format_plugin_id(reiserfs_fs_t *fs);

#endif

