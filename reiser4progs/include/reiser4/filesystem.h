/*
    filesystem.h -- reiserfs filesystem structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include <reiser4/key.h>

#define REISERFS_DEFAULT_BLOCKSIZE	4096
#define REISERFS_MASTER_OFFSET		65536
#define REISERFS_MASTER_MAGIC		"R4Sb"

/* Master super block structure and macros */
struct reiserfs_master {
    char mr_magic[4];
    uint16_t mr_format_id;
    uint16_t mr_blocksize;
    char mr_uuid[16];
    char mr_label[16];
};

typedef struct reiserfs_master reiserfs_master_t;

#define get_mr_format_id(mr)		aal_get_le16(mr, mr_format_id)
#define set_mr_format_id(mr, val)	aal_set_le16(mr, mr_format_id, val)

#define get_mr_block_size(mr)		aal_get_le16(mr, mr_blocksize)
#define set_mr_block_size(mr, val)	aal_set_le16(mr, mr_blocksize, val)

typedef struct reiserfs_fs reiserfs_fs_t;

/* 
    Profile structure. It describes what plugins will be used
    for every corresponding filesystem part.
*/
struct reiserfs_profile {
    char label[255];
    char desc[255];
    
    reiserfs_id_t node;
    
    struct {
	reiserfs_id_t internal;
	reiserfs_id_t statdata;
	reiserfs_id_t direntry;
	reiserfs_id_t tail;
	reiserfs_id_t extent;
    } item;
    
    reiserfs_id_t file;
    reiserfs_id_t dir;
    reiserfs_id_t hash;
    reiserfs_id_t tail;
    reiserfs_id_t hook;
    reiserfs_id_t perm;
    reiserfs_id_t format;
    reiserfs_id_t oid;
    reiserfs_id_t alloc;
    reiserfs_id_t journal;
    reiserfs_id_t key;
};

typedef struct reiserfs_profile reiserfs_profile_t;

typedef struct reiserfs_cache reiserfs_cache_t;
typedef struct reiserfs_node reiserfs_node_t;

struct reiserfs_coord {
    reiserfs_cache_t *cache;
    reiserfs_pos_t pos;
};

typedef struct reiserfs_coord reiserfs_coord_t;

struct reiserfs_node {
    aal_block_t *block;
    
    reiserfs_plugin_t *key_plugin;
    reiserfs_plugin_t *node_plugin;
};

struct reiserfs_node_header {
    uint16_t pid; 
};

typedef struct reiserfs_node_header reiserfs_node_header_t;

struct reiserfs_object {
    reiserfs_fs_t *fs;
    
    reiserfs_key_t key;
    reiserfs_coord_t coord;

    reiserfs_plugin_t *plugin;
    reiserfs_object_hint_t *hint;
};

typedef struct reiserfs_object reiserfs_object_t;

/* Format structure */
struct reiserfs_format {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_format reiserfs_format_t;

/* Journal structure */
struct reiserfs_journal {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_journal reiserfs_journal_t;

/* Block allocator structure */
struct reiserfs_alloc {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_alloc reiserfs_alloc_t;

/* Oid allocator structure */
struct reiserfs_oid {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
    reiserfs_key_t root_key;
};

typedef struct reiserfs_oid reiserfs_oid_t;

struct reiserfs_cache {
    reiserfs_node_t *node;
    
    reiserfs_cache_t *parent;
    reiserfs_cache_t *left;
    reiserfs_cache_t *right;
    
    aal_list_t *list;
};

struct reiserfs_tree {
    reiserfs_fs_t *fs;
    reiserfs_cache_t *cache;
};

typedef struct reiserfs_tree reiserfs_tree_t;

/* Filesystem compound structure */
struct reiserfs_fs {
    aal_device_t *host_device;
    aal_device_t *journal_device;
    
    reiserfs_master_t *master;
    reiserfs_format_t *format;
    reiserfs_journal_t *journal;
    reiserfs_alloc_t *alloc;
    reiserfs_oid_t *oid;
    reiserfs_tree_t *tree;
    reiserfs_object_t *dir;

    reiserfs_key_t key;
};

/* Public functions */
extern reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay);

extern void reiserfs_fs_close(reiserfs_fs_t *fs);

#ifndef ENABLE_COMPACT

extern reiserfs_fs_t *reiserfs_fs_create(reiserfs_profile_t *profile, 
    aal_device_t *host_device, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    reiserfs_opaque_t *journal_params);

extern errno_t reiserfs_fs_sync(reiserfs_fs_t *fs);

extern errno_t reiserfs_fs_check(reiserfs_fs_t *fs);

#endif

extern const char *reiserfs_fs_format(reiserfs_fs_t *fs);
extern uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs);
extern reiserfs_id_t reiserfs_fs_format_pid(reiserfs_fs_t *fs);

#endif

