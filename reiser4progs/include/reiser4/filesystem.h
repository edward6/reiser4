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

/* Master super block structure and macros */
struct reiser4_master_super {

    /* Reiser4 magic R4Sb */
    char mr_magic[4];

    /* Disk format plugin in use */
    d16_t mr_format_id;

    /* Block size in use */
    d16_t mr_blocksize;

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
    int native;

    aal_block_t *block;
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
    
    rpid_t node;

    struct {
	rpid_t dir;
    } dir;
    
    struct {
	rpid_t regular;
	rpid_t symlink;
	rpid_t special;      
    } file;
    
    struct {	    
	rpid_t statdata;
	rpid_t internal;
	rpid_t direntry;
	struct {
	    rpid_t tail;
	    rpid_t extent;
	} file_body;
	rpid_t acl;
    } item;
    
    rpid_t hash;
    rpid_t tail;
    rpid_t perm;
    rpid_t format;
    rpid_t oid;
    rpid_t alloc;
    rpid_t journal;
    rpid_t key;
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

/* Internal struct of libreiser4 for keeping item components */
struct reiser4_item {
    
    /* Item data length */
    uint32_t len;
	
    /* The pointer to the item body */
    reiser4_body_t *body;

    /* The pointer to the item plugin */
    reiser4_plugin_t *plugin;
    
    reiser4_node_t *node;
    reiser4_pos_t *pos;
};

typedef struct reiser4_item reiser4_item_t;

/* Reiser4 in-memory node structure */
struct reiser4_node {
    /* Block node lies in */
    aal_block_t *block;

    /* Node entity. This field is uinitializied by node plugin */
    reiser4_entity_t *entity;
};

/* Reiserfs object structure (file, dir) */
struct reiser4_file {

    /* Object entity. It is initialized by object plugin */
    reiser4_entity_t *entity;
    
    /* Current coord */
    reiser4_coord_t coord;

    /* Object key of first item (most probably stat data item) */
    reiser4_key_t key;
    
    /* Referrence to the filesystem object opened on */
    reiser4_fs_t *fs;
};

typedef struct reiser4_file reiser4_file_t;

/* Reiser4 disk-format in-memory structure */
struct reiser4_format {

    /* Device filesystem opended on */
    aal_device_t *device;
    
    /* 
	Disk-format entity. It is initialized by disk-format plugin durring
	initialization.
    */
    reiser4_entity_t *entity;
};

typedef struct reiser4_format reiser4_format_t;

/* Journal structure */
struct reiser4_journal {
    
    /* 
	Device journal opened on. In the case of standard journal this field 
	will be pointing to the same device as in disk-format struct. If the 
	journal is t relocated one then device will be contain pointer to opened
	device journal is opened on.
    */
    aal_device_t *device;

    /* Journal entity. Initializied by plugin */
    reiser4_entity_t *entity;
};

typedef struct reiser4_journal reiser4_journal_t;

/* Block allocator structure */
struct reiser4_alloc {
    reiser4_entity_t *entity;
};

typedef struct reiser4_alloc reiser4_alloc_t;

/* Oid allocator structure */
struct reiser4_oid {
    reiser4_entity_t *entity;
};

typedef struct reiser4_oid reiser4_oid_t;

/* Structure of one cached node in the internal libreiser4 tree */
struct reiser4_cache {
    uint8_t level;
	
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
    count_t cur;

    /* Maximal allowed size */
    count_t max;

    /* Is cache limit spying active? */
    int enabled;
};

typedef struct reiser4_cache_limit reiser4_cache_limit_t;

/* Tree structure */
struct reiser4_tree {

    /* Reference to filesystem instance tree opened on */
    reiser4_fs_t *fs;

    /* 
	Reference to root cached node. It is created by tree initialization 
	routines and always exists. All other cached nodes are loaded on demand 
	and flushed at memory presure event.
    */
    reiser4_cache_t *cache;

    /* 
	Limit for number of blocks allowed to be cached. If this value will be 
	exceeded, tree will perform flush operation until this value reach 
	allowed value minus some customizable and reasonable number of blocks.
    */
    reiser4_cache_limit_t limit;
    
    /* Tree root key */
    reiser4_key_t key;
};

/* Callback function type for opening node. */
typedef reiser4_node_t *(*reiser4_open_func_t) (aal_block_t *, void *);

/* Callback function type for preparing per-node traverse data. */
typedef errno_t (*reiser4_edge_func_t) (reiser4_node_t *, void *);

/* Callback function type for node handler. */
typedef errno_t (*reiser4_handler_func_t) (reiser4_node_t *, void *);

/* Callback function type for preparing per-item traverse data. */
typedef errno_t (*reiser4_setup_func_t) (reiser4_node_t *, reiser4_item_t *, 
    void *);

/* Filesystem compound structure */
struct reiser4_fs {
    
    /* Pointer to the master super block wrapp object */
    reiser4_master_t *master;

    /* Pointer to the disk-format instance */
    reiser4_format_t *format;

    /* Pointer to the journal in use */
    reiser4_journal_t *journal;

    /* Pointer to the block allocator in use */
    reiser4_alloc_t *alloc;

    /* Pointer to the oid allocator */
    reiser4_oid_t *oid;

    /* Tree cache */
    reiser4_tree_t *tree;

    /* Root object */
    reiser4_file_t *root;

    /* User-specified data (used by fsck) */
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

extern rpid_t reiser4_fs_format_pid(reiser4_fs_t *fs);
extern aal_device_t *reiser4_fs_host_device(reiser4_fs_t *fs);
extern aal_device_t *reiser4_fs_journal_device(reiser4_fs_t *fs);

#endif

