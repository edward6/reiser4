/*
    filesystem.h -- reiserfs filesystem structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <aal/aal.h>
#include <reiserfs/plugin.h>

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

/* Default plugins structure */
struct reiserfs_default_plugin {
    reiserfs_plugin_id_t node;
    
    struct item_plugins {
	reiserfs_plugin_id_t internal;
	reiserfs_plugin_id_t stat;
	reiserfs_plugin_id_t dir_item;
	reiserfs_plugin_id_t file_item;
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
};

typedef struct reiserfs_default_plugin reiserfs_default_plugin_t;

typedef struct reiserfs_node reiserfs_node_t;

struct reiserfs_node {
    aal_device_t *device;
    aal_block_t *block;
    reiserfs_plugin_t *plugin;
    reiserfs_node_t *parent;
    reiserfs_opaque_t *entity;
};

struct reiserfs_coord {
    reiserfs_node_t *node;	/* node in the tree */
    int16_t item_pos;		/* pos of an item in the node */
    int16_t unit_pos;		/* pos of an unit in the item */
};

typedef struct reiserfs_coord reiserfs_coord_t;

struct reiserfs_path {
    aal_list_t *entity;		/* list for holding path elements */
    void *data;			/* user specified per-path data */
};

struct reiserfs_node_common_header {
    uint16_t plugin_id; 
};

typedef struct reiserfs_node_common_header reiserfs_node_common_header_t;

/*
    This structure differs from others and I think we should move others to 
    the same form. 
    1. It is useless complicity to have opaque structures which encapsulate 
       information available on the api level like blocks, devices, etc.
    2. Opaque structures are useful when we work with e.g. compressed nodes
       which data shuold be uncompressed first. 
    3. For e.g. not-compressed nodes node plugin open method does just nothing
       but creates useless structure, which contails the same data as in 
       the reiserfs_node_t structure. 
    4. Plugins should work with the same structures as api does. E.g. node40 
       plugin should work with reiserfs_node_t method. If plugin needs it can 
       create some entity for itself.
    5. As many plugins does not need methods like open/create etc, we get rid 
       of their implementation. Good.
*/
struct reiserfs_item {
    reiserfs_coord_t *coord;
 
    reiserfs_plugin_t *plugin;
    reiserfs_opaque_t *entity;
};

typedef struct reiserfs_item reiserfs_item_t;

/* Tree structure */
struct reiserfs_tree {
    reiserfs_node_t *root;
};

typedef struct reiserfs_tree reiserfs_tree_t;

/* 
    To create a new item or to insert into the item we need to perform the following 
    operations:
    1. Create the description of the data being inserted.
    2. Ask item plugin how much space is needed for the data, described in 1.   
    3. Free needed space for data being inserted.
    4. Ask item plugin to create an item (to paste into the item) on the base of 
       description from 1.

    For such purposes we have: 
    1. Fixed description structures for all item types (stat, diritem, internal, etc).
    2. Estimate common item method which gets coord of where to insert into 
       (NULL or unit_pos == -1 for insertion, otherwise it is pasting) and data 
       description from 1.
    3. Insert node methods prepare needed space and call Create/Paste item methods if 
       data description is specified.
    4. Create/Paste item methods if data description has not beed specified on 3. 
*/

/* 
    Create item or paste into item on the base of this structure. 
    "data" is a pointer to data to be copied. 
    If "data" == NULL, item/units should be created on the base of "info". 
    Otherwise, "info" == NULL. 
*/ 
struct reiserfs_item_info {    
    void *data;
    void *info;
    uint16_t length;
    reiserfs_plugin_t *plugin;
};
typedef struct reiserfs_item_info reiserfs_item_info_t;

struct reiserfs_internal_info {    
    blk_t *blk;
};

typedef struct reiserfs_internal_info reiserfs_internal_info_t;

struct reiserfs_stat_info {
    /*  
	These fields should be changed to what proper description of 
	needed extentions. 
    */
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
};

typedef struct reiserfs_stat_info reiserfs_stat_info_t;

struct reiserfs_entry_info {
    uint64_t parent_id;
    uint64_t object_id;
    char *name;
};

typedef struct reiserfs_entry_info reiserfs_entry_info_t;

struct reiserfs_dir_info {
    uint16_t count;
    reiserfs_entry_info_t *entry;
};

typedef struct reiserfs_dir_info reiserfs_dir_info_t;

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
    reiserfs_plugin_id_t format_plugin_id, reiserfs_plugin_id_t node_plugin_id, 
    size_t blocksize, const char *uuid, const char *label, count_t len, 
    aal_device_t *journal_device, reiserfs_params_opaque_t *journal_params);

extern const char *reiserfs_fs_format(reiserfs_fs_t *fs);
extern uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_fs_format_plugin_id(reiserfs_fs_t *fs);

#endif

