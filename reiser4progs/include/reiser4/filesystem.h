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

#define REISERFS_DEFAULT_BLOCKSIZE	(4096)
#define REISERFS_MASTER_OFFSET		(65536)
#define REISERFS_MASTER_MAGIC		("R4Sb")

#define REISERFS_LEGACY_FORMAT		(0x0)

/* Master super block structure and macros */
struct reiserfs_master {
    /* Reiser4 magic R4Sb */
    char mr_magic[4];

    /* Disk format plugin in use */
    uint16_t mr_format_id;

    /* Block size in use */
    uint16_t mr_blocksize;

    /* Universaly unique identifier */
    char mr_uuid[16];

    /* File system label in use */
    char mr_label[16];
};

typedef struct reiserfs_master reiserfs_master_t;

#define get_mr_format_id(mr)		aal_get_le16(mr, mr_format_id)
#define set_mr_format_id(mr, val)	aal_set_le16(mr, mr_format_id, val)

#define get_mr_block_size(mr)		aal_get_le16(mr, mr_blocksize)
#define set_mr_block_size(mr, val)	aal_set_le16(mr, mr_blocksize, val)

typedef struct reiserfs_fs reiserfs_fs_t;

/* 
    Profile structure. It describes what plugins will be used for every part
    of the filesystem.
*/
struct reiserfs_profile {
    char label[255];
    char desc[255];
    
    reiserfs_id_t node;

    struct {
	reiserfs_id_t dir;
    } dir;
    
    struct {
	reiserfs_id_t reg;
	reiserfs_id_t symlink;
	reiserfs_id_t special;      
    } file;
    
    struct {	    
	reiserfs_id_t statdata;
	reiserfs_id_t internal;
	reiserfs_id_t direntry;
	struct {
	    reiserfs_id_t drop;
	    reiserfs_id_t extent;
	} file_body;
	reiserfs_id_t acl;
    } item;
    
    reiserfs_id_t hash;
    reiserfs_id_t drop_policy;
    reiserfs_id_t perm;
    reiserfs_id_t format;
    reiserfs_id_t oid;
    reiserfs_id_t alloc;
    reiserfs_id_t journal;
    reiserfs_id_t key;
    uint64_t sdext;
};

typedef struct reiserfs_profile reiserfs_profile_t;

typedef struct reiserfs_tree reiserfs_tree_t;
typedef struct reiserfs_cache reiserfs_cache_t;
typedef struct reiserfs_node reiserfs_node_t;

/* Coord inside reiser4 tree */
struct reiserfs_coord {
    /* Pointer to the cached node */
    reiserfs_cache_t *cache;

    /* Position inside the cached node */
    reiserfs_pos_t pos;
};

typedef struct reiserfs_coord reiserfs_coord_t;

/* Reiser4 in-memory node structure */
struct reiserfs_node {

    /* Block node lies in */
    aal_block_t *block;

    /* Node entity. This field is uinitializied by node plugin */
    reiserfs_entity_t *entity;
    
    /* Node plugin is use */
    reiserfs_plugin_t *plugin;
};

/* Reiserfs object structure (file, dir) */
struct reiserfs_object {

    /* Referrence to the filesystem object opened on */
    reiserfs_fs_t *fs;
    
    /* Object entity. It is initialized by object plugin */
    reiserfs_entity_t *entity;

    /* Object plugin in use */
    reiserfs_plugin_t *plugin;
    
    /* Object key of first item (most probably stat data item) */
    reiserfs_key_t key;

    /* Current coord */
    reiserfs_coord_t coord;
};

typedef struct reiserfs_object reiserfs_object_t;

/* Reiser4 disk-format in-memory structure */
struct reiserfs_format {

    /* 
	Disk-format entity. It is initialized by disk-format plugin durring
	initialization.
    */
    reiserfs_entity_t *entity;

    /* Disk-format plugin in use */
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_format reiserfs_format_t;

/* Journal structure */
struct reiserfs_journal {
    reiserfs_entity_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_journal reiserfs_journal_t;

/* Block allocator structure */
struct reiserfs_alloc {
    reiserfs_entity_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_alloc reiserfs_alloc_t;

/* Oid allocator structure */
struct reiserfs_oid {

    /* Key of the root object */
    reiserfs_key_t key;
    
    /* Oid allocator entity */
    reiserfs_entity_t *entity;

    /* Oid allocator plugin in use */
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_oid reiserfs_oid_t;

/* Structure of one cached node in the internal libreiser4 tree */
struct reiserfs_cache {
    
    /* Reference to tree instance cache lies in */
    reiserfs_tree_t *tree;
       	
    /* Reference to the node assosiated with this cache node */
    reiserfs_node_t *node;
    
    /* 
	Reference to the parent node. It is used for accessing parent durring 
	balancing.
    */
    reiserfs_cache_t *parent;

    /* Reference to left neighbour */
    reiserfs_cache_t *left;

    /* Reference to right neighbour */
    reiserfs_cache_t *right;
    
    /* List of children nodes */
    aal_list_t *list;
};

struct reiserfs_cache_limit {
    /* Current size of cache in blocks */
    int32_t cur;

    /* Maximal allowed size */
    uint32_t max;

    /* Is cache limit spying active? */
    int enabled;
};

typedef struct reiserfs_cache_limit reiserfs_cache_limit_t;

/* Tree structure */
struct reiserfs_tree {

    /* Reference to filesystem instance tree opened on */
    reiserfs_fs_t *fs;

    /* 
	Reference to root cacheed node. It is created by tree initialization routines 
	and always exists. All other cached nodes are loaded on demand and flushed at
	memory presure event.
    */
    reiserfs_cache_t *cache;

    /* 
	Limit for number of blocks allowed to be cached. If this value will be exceeded, 
	tree will perform flush operation until this value reach allowed value minus some
	customizable and reasonable number of blocks.
    */
    reiserfs_cache_limit_t limit;
};

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

    void *data;
};

/* Public functions */
extern reiserfs_fs_t *reiserfs_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay);

extern void reiserfs_fs_close(reiserfs_fs_t *fs);

#ifndef ENABLE_COMPACT

extern reiserfs_fs_t *reiserfs_fs_create(reiserfs_profile_t *profile, 
    aal_device_t *host_device, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    void *journal_params);

extern errno_t reiserfs_fs_sync(reiserfs_fs_t *fs);

extern errno_t reiserfs_fs_check(reiserfs_fs_t *fs);

#endif

extern const char *reiserfs_fs_format(reiserfs_fs_t *fs);
extern uint16_t reiserfs_fs_blocksize(reiserfs_fs_t *fs);
extern reiserfs_id_t reiserfs_fs_format_pid(reiserfs_fs_t *fs);

#endif

