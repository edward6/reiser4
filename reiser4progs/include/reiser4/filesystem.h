/*
    filesystem.h -- reiser4 filesystem structures and macros.
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

#define REISER4_DEFAULT_BLOCKSIZE	(4096)
#define REISER4_MASTER_OFFSET		(65536)
#define REISER4_MASTER_MAGIC		("R4Sb")

#define REISER4_LEGACY_FORMAT		(0x0)

/* Master super block structure and macros */
struct reiser4_master_super {

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

typedef struct reiser4_master_super reiser4_master_super_t;

#define get_mr_format_id(mr)		aal_get_le16(mr, mr_format_id)
#define set_mr_format_id(mr, val)	aal_set_le16(mr, mr_format_id, val)

#define get_mr_blocksize(mr)		aal_get_le16(mr, mr_blocksize)
#define set_mr_blocksize(mr, val)	aal_set_le16(mr, mr_blocksize, val)

struct reiser4_master {
    aal_block_t *block;
    aal_device_t *device;

    reiser4_master_super_t *super;
};

typedef struct reiser4_master reiser4_master_t;

typedef struct reiser4_fs reiser4_fs_t;

/* 
    Profile structure. It describes what plugins will be used for every part
    of the filesystem.
*/
struct reiser4_profile {
    char label[255];
    char desc[255];
    
    reiser4_id_t node;

    struct {
	reiser4_id_t dir;
    } dir;
    
    struct {
	reiser4_id_t reg;
	reiser4_id_t symlink;
	reiser4_id_t special;      
    } file;
    
    struct {	    
	reiser4_id_t statdata;
	reiser4_id_t internal;
	reiser4_id_t direntry;
	struct {
	    reiser4_id_t drop;
	    reiser4_id_t extent;
	} file_body;
	reiser4_id_t acl;
    } item;
    
    reiser4_id_t hash;
    reiser4_id_t drop_policy;
    reiser4_id_t perm;
    reiser4_id_t format;
    reiser4_id_t oid;
    reiser4_id_t alloc;
    reiser4_id_t journal;
    reiser4_id_t key;
    uint64_t sdext;
};

typedef struct reiser4_profile reiser4_profile_t;

typedef struct reiser4_tree reiser4_tree_t;
typedef struct reiser4_cache reiser4_cache_t;
typedef struct reiser4_node reiser4_node_t;

/* Coord inside reiser4 tree */
struct reiser4_coord {
    /* Pointer to the cached node */
    reiser4_cache_t *cache;

    /* Position inside the cached node */
    reiser4_pos_t pos;
};

typedef struct reiser4_coord reiser4_coord_t;

/* Reiser4 in-memory node structure */
struct reiser4_node {

    /* Block node lies in */
    aal_block_t *block;

    /* Node entity. This field is uinitializied by node plugin */
    reiser4_entity_t *entity;
    
    /* Node plugin is use */
    reiser4_plugin_t *plugin;
};

/* Reiserfs object structure (file, dir) */
struct reiser4_object {

    /* Referrence to the filesystem object opened on */
    reiser4_fs_t *fs;
    
    /* Object entity. It is initialized by object plugin */
    reiser4_entity_t *entity;

    /* Object plugin in use */
    reiser4_plugin_t *plugin;
    
    /* Object key of first item (most probably stat data item) */
    reiser4_key_t key;

    /* Current coord */
    reiser4_coord_t coord;
};

typedef struct reiser4_object reiser4_object_t;

/* Reiser4 disk-format in-memory structure */
struct reiser4_format {

    /* Device filesystem opended on */
    aal_device_t *device;
    
    /* 
	Disk-format entity. It is initialized by disk-format plugin durring
	initialization.
    */
    reiser4_entity_t *entity;

    /* Disk-format plugin in use */
    reiser4_plugin_t *plugin;
};

typedef struct reiser4_format reiser4_format_t;

/* Journal structure */
struct reiser4_journal {
    
    /* 
	Device journal opened on. In the case of standard journal this field will
	be pointing to the same device as in disk-format struct. If the journal 
	is t relocated one then device will be contain pointer to opened device 
	journal is opened on.
    */
    aal_device_t *device;

    /* Journal entity. Initializied by plugin */
    reiser4_entity_t *entity;

    /* Plugin for working with journal by */
    reiser4_plugin_t *plugin;
};

typedef struct reiser4_journal reiser4_journal_t;

/* Block allocator structure */
struct reiser4_alloc {
    reiser4_entity_t *entity;
    reiser4_plugin_t *plugin;
};

typedef struct reiser4_alloc reiser4_alloc_t;

/* Oid allocator structure */
struct reiser4_oid {
    
    /* Oid allocator entity */
    reiser4_entity_t *entity;

    /* Oid allocator plugin in use */
    reiser4_plugin_t *plugin;
};

typedef struct reiser4_oid reiser4_oid_t;

/* Structure of one cached node in the internal libreiser4 tree */
struct reiser4_cache {
    
    /* Reference to tree instance cache lies in */
    reiser4_tree_t *tree;
       	
    /* Reference to the node assosiated with this cache node */
    reiser4_node_t *node;
    
    /* 
	Reference to the parent node. It is used for accessing parent durring 
	balancing.
    */
    reiser4_cache_t *parent;

    /* Reference to left neighbour */
    reiser4_cache_t *left;

    /* Reference to right neighbour */
    reiser4_cache_t *right;
    
    /* List of children nodes */
    aal_list_t *list;
};

struct reiser4_cache_limit {
    /* Current size of cache in blocks */
    int32_t cur;

    /* Maximal allowed size */
    uint32_t max;

    /* Is cache limit spying active? */
    int enabled;
};

typedef struct reiser4_cache_limit reiser4_cache_limit_t;

/* Tree structure */
struct reiser4_tree {

    /* Reference to filesystem instance tree opened on */
    reiser4_fs_t *fs;

    /* 
	Reference to root cacheed node. It is created by tree initialization routines 
	and always exists. All other cached nodes are loaded on demand and flushed at
	memory presure event.
    */
    reiser4_cache_t *cache;

    /* 
	Limit for number of blocks allowed to be cached. If this value will be exceeded, 
	tree will perform flush operation until this value reach allowed value minus some
	customizable and reasonable number of blocks.
    */
    reiser4_cache_limit_t limit;
};

/* Filesystem compound structure */
struct reiser4_fs {
    reiser4_master_t *master;
    reiser4_format_t *format;
    reiser4_journal_t *journal;
    reiser4_alloc_t *alloc;
    reiser4_oid_t *oid;
    reiser4_tree_t *tree;
    reiser4_object_t *dir;

    reiser4_key_t key;
    void *data;
};

/* Public functions */
extern reiser4_fs_t *reiser4_fs_open(aal_device_t *host_device, 
    aal_device_t *journal_device, int replay);

extern void reiser4_fs_close(reiser4_fs_t *fs);

#ifndef ENABLE_COMPACT

extern reiser4_fs_t *reiser4_fs_create(reiser4_profile_t *profile, 
    aal_device_t *host_device, size_t blocksize, const char *uuid, 
    const char *label, count_t len, aal_device_t *journal_device, 
    void *journal_params);

extern errno_t reiser4_fs_sync(reiser4_fs_t *fs);

#endif

extern const char *reiser4_fs_name(reiser4_fs_t *fs);
extern uint16_t reiser4_fs_blocksize(reiser4_fs_t *fs);
extern errno_t reiserfs_fs_build_root_key(reiserfs_fs_t *fs, reiserfs_id_t pid);

extern reiser4_id_t reiser4_fs_format_pid(reiser4_fs_t *fs);
extern aal_device_t *reiser4_fs_host_device(reiser4_fs_t *fs);
extern aal_device_t *reiser4_fs_journal_device(reiser4_fs_t *fs);

#endif

