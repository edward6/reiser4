/*
    filesystem.h -- reiserfs filesystem structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

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

#define get_mr_format_id(mr)		get_le16(mr, mr_format_id)
#define set_mr_format_id(mr, val)	set_le16(mr, mr_format_id, val)

#define get_mr_block_size(mr)		get_le16(mr, mr_blocksize)
#define set_mr_block_size(mr, val)	set_le16(mr, mr_blocksize, val)

/* 
    Profile structure. It describes what plugins will be used
    for every corresponding filesystem part.
*/
struct reiserfs_profile {
    char label[255];
    char desc[255];
    
    reiserfs_plugin_id_t node;
    
    struct {
	reiserfs_plugin_id_t internal;
	reiserfs_plugin_id_t statdata;
	reiserfs_plugin_id_t direntry;
	reiserfs_plugin_id_t fileentry;
    } item;
    
    reiserfs_plugin_id_t file;
    reiserfs_plugin_id_t dir;
    reiserfs_plugin_id_t hash;
    reiserfs_plugin_id_t tail;
    reiserfs_plugin_id_t hook;
    reiserfs_plugin_id_t perm;
    reiserfs_plugin_id_t format;
    reiserfs_plugin_id_t oid;
    reiserfs_plugin_id_t alloc;
    reiserfs_plugin_id_t journal;
    reiserfs_plugin_id_t key;
};

typedef struct reiserfs_profile reiserfs_profile_t;

typedef struct reiserfs_node reiserfs_node_t;

struct reiserfs_node {
    aal_block_t *block;
    aal_device_t *device;
    
    reiserfs_plugin_t *plugin;
    
    reiserfs_node_t *parent;
    aal_list_t *children;
};

struct reiserfs_node_header {
    uint16_t plugin_id; 
};

typedef struct reiserfs_node_header reiserfs_node_header_t;

/* 
    Tree representation object. It consists of root node
    chich contains childrens and so on.
*/
struct reiserfs_tree {
    reiserfs_node_t *root_node;

    /*
	Temporary directory plugin pointer. It is used
	for destroying root directory. It will be removed
	when dir API will be complete.
    */
    reiserfs_plugin_t *dir_plugin;

    /* 
	FIXME-UMKA: Here will be reiserfs_dir_t when ready.
	But for awhile it is just opaque wchich points to 
	entiry initialized by dir plugin.
    */
    reiserfs_opaque_t *root_dir;
};

typedef struct reiserfs_tree reiserfs_tree_t;

struct reiserfs_dir {
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_dir reiserfs_dir_t;

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
};

typedef struct reiserfs_oid reiserfs_oid_t;

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
};

typedef struct reiserfs_fs reiserfs_fs_t;

/* Public functions */
extern reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay);

extern void reiserfs_fs_close(reiserfs_fs_t *fs);
extern error_t reiserfs_fs_sync(reiserfs_fs_t *fs);
	
extern reiserfs_fs_t *reiserfs_fs_create(aal_device_t *host_device, 
    reiserfs_profile_t *profile, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    reiserfs_params_opaque_t *journal_params);

extern const char *reiserfs_fs_format(reiserfs_fs_t *fs);
extern uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_fs_format_plugin_id(reiserfs_fs_t *fs);

#endif

