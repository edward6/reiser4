/*
    plugin.h -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <aal/aal.h>

typedef void reiserfs_opaque_t;
typedef void reiserfs_params_opaque_t;
typedef uint64_t oid_t;

enum reiserfs_plugin_id {
    REISERFS_FILE_PLUGIN,
    REISERFS_DIR_PLUGIN,
    REISERFS_ITEM_PLUGIN,
    REISERFS_NODE_PLUGIN,
    REISERFS_HASH_PLUGIN,
    REISERFS_TAIL_PLUGIN,
    REISERFS_HOOK_PLUGIN,
    REISERFS_PERM_PLUGIN,
    REISERFS_SD_EXT_PLUGIN,
    REISERFS_FORMAT_PLUGIN,
    REISERFS_OID_PLUGIN,
    REISERFS_ALLOC_PLUGIN,
    REISERFS_JOURNAL_PLUGIN,
    REISERFS_KEY_PLUGIN,
};

enum reiserfs_item_type_id {
    STAT_ITEM,
    DIRENTRY_ITEM,
    INTERNAL_ITEM,
    FILE_ITEM
};

typedef enum reiserfs_item_type_id reiserfs_item_type_id_t;
typedef int reiserfs_plugin_id_t;

#define REISERFS_PLUGIN_MAX_LABEL	16
#define REISERFS_PLUGIN_MAX_DESC	256

/* Common plugin header */
struct reiserfs_plugin_header {
    void *handle;
    reiserfs_plugin_id_t id;
    reiserfs_plugin_id_t type;
    const char label[REISERFS_PLUGIN_MAX_LABEL];
    const char desc[REISERFS_PLUGIN_MAX_DESC];
};

typedef struct reiserfs_plugin_header reiserfs_plugin_header_t;

struct reiserfs_key_plugin {
    reiserfs_plugin_header_t h;

    /* Confirms key format */
    int (*confirm) (void *);

    /* Returns minimal key for this key-format */
    const void *(*minimal) (void);
    
    /* Returns maximal key for this key-format */
    const void *(*maximal) (void);

    /* Compares two keys */
    int (*compare) (const void *, const void *);

    /* Creates key by its components */
    error_t (*init) (void *, uint16_t, oid_t, oid_t, uint64_t);

    /* Gets/sets key type (minor in reiser4 notation) */	
    void (*set_type) (void *, uint16_t);
    uint16_t (*get_type) (void *);

    /* Gets/sets key locality */
    void (*set_locality) (void *, oid_t);
    oid_t (*get_locality) (void *);
    
    /* Gets/sets key objectid */
    void (*set_objectid) (void *, oid_t);
    oid_t (*get_objectid) (void *);

    /* Gets/sets key offset */
    void (*set_offset) (void *, uint64_t);
    uint64_t (*get_offset) (void *);
};

typedef struct reiserfs_key_plugin reiserfs_key_plugin_t;

struct reiserfs_file_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_file_plugin reiserfs_file_plugin_t;

struct reiserfs_dir_plugin {
    reiserfs_plugin_header_t h;

    reiserfs_opaque_t *(*create) (void);
    reiserfs_opaque_t *(*open) (void);
    void (*close) (reiserfs_opaque_t *);
};

typedef struct reiserfs_dir_plugin reiserfs_dir_plugin_t;

struct reiserfs_item_ops {
    reiserfs_item_type_id_t type;

    error_t (*create) (void *, void *);
    int (*lookup) (void *, void *, void *);

    error_t (*confirm) (void *);
    error_t (*check) (void *);
    void (*print) (void *, char *, uint16_t);
    
    /* 
	Unit-working routines. 	We need to create special opaque 
	type for item_info and unit_info. But for awhile it will 
	be void *.

	Vitaly: No, we should not. I wrote about it already in 
	filesystem.h. 
    */
    int (*unit_add) (void *, void *, void *);
    uint16_t (*unit_count) (void *);
    int (*unit_remove) (void *, int32_t, int32_t);
    
    void (*estimate) (void *, void *);
    uint32_t (*minsize) (void);
    
    int (*internal) (void);
};

typedef struct reiserfs_item_ops reiserfs_item_ops_t;

struct reiserfs_direntry_ops {
};

typedef struct reiserfs_direntry_ops reiserfs_direntry_ops_t;

struct reiserfs_stat_ops {
};

typedef struct reiserfs_stat_ops reiserfs_stat_ops_t;

struct reiserfs_internal_ops {
    void (*set_pointer) (void *, blk_t);
    blk_t (*get_pointer) (void *);
    int (*has_pointer) (void *, blk_t);
};

typedef struct reiserfs_internal_ops reiserfs_internal_ops_t;

struct reiserfs_item_plugin {
    reiserfs_plugin_header_t h;

    /* Methods common for all item types */
    reiserfs_item_ops_t common;

    /* Methods specific to particular type of item */
    union {
	reiserfs_direntry_ops_t dir;
	reiserfs_stat_ops_t stat;
	reiserfs_internal_ops_t internal;
    } specific;
};

typedef struct reiserfs_item_plugin reiserfs_item_plugin_t;

/*
    Node plugin operates on passed block. It doesn't any 
    initialization, so it hasn't close method and all its
    methods accepts first argument aal_block_t, not initialized
    previously hypothetic instance of node.
*/
struct reiserfs_node_plugin {
    reiserfs_plugin_header_t h;

    /* 
	Forms empty node incorresponding to given level in 
	specified block.
    */
    error_t (*create) (aal_block_t *, uint8_t);

    /* 
	Perform some needed operations to the futher fast work 
	with the node. Useful for compressed nodes, etc.
     */
    error_t (*open) (reiserfs_opaque_t *);
    error_t (*close) (reiserfs_opaque_t *);
    
    /*
	Confirms that given block contains valid node of
	requested format.
    */
    error_t (*confirm) (aal_block_t *);

    /* Make more smart node's check and return result */
    error_t (*check) (aal_block_t *, int);
    
    /* Makes lookup inside node by specified key */
    int (*lookup) (aal_block_t *, void *, void *);
    
    /* Gets/sets node's free space */
    uint16_t (*get_free_space) (aal_block_t *);
    void (*set_free_space) (aal_block_t *, uint32_t);
    
    /*
	This is optional method. That means that there could be 
	node formats which do not keep level.
    */
    uint8_t (*get_level) (aal_block_t *);
    void (*set_level) (aal_block_t *, uint8_t);

    /* Prints node into given buffer */
    void (*print) (aal_block_t *, char *, uint16_t);
    
    /* Inserts item/past units into specified node/item */
    error_t (*item_insert) (aal_block_t *, void *, void *, void *);
    error_t (*item_paste) (aal_block_t *, void *, void *, void *);
    
    /* Returns item's overhead */
    uint16_t (*item_overhead) (aal_block_t *);

    /* Returns item's max size */
    uint16_t (*item_maxsize) (aal_block_t *);

    /* Returns max item count */
    uint16_t (*item_maxnum) (aal_block_t *);

    /* Returns item count */
    uint16_t (*item_count) (aal_block_t *);
    
    /* Returns item's length by pos */
    uint16_t (*item_length) (aal_block_t *, int32_t);
    
    /* Gets item at passed pos */
    void *(*item_at) (aal_block_t *, int32_t);

    /* Gets/sets node's plugin ID */
    uint16_t (*item_get_plugin_id) (aal_block_t *, int32_t);
    void (*item_set_plugin_id) (aal_block_t *, int32_t, uint16_t);
    
    /* Compare two keys */
    int (*key_cmp) (const void *, const void *);
	
    /* Gets key by pos */
    void *(*key_at) (aal_block_t *, int32_t);
};

typedef struct reiserfs_node_plugin reiserfs_node_plugin_t;

struct reiserfs_hash_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hash_plugin reiserfs_hash_plugin_t;

struct reiserfs_tail_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_tail_plugin reiserfs_tail_plugin_t;

struct reiserfs_hook_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hook_plugin reiserfs_hook_plugin_t;

struct reiserfs_perm_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_perm_plugin reiserfs_perm_plugin_t;

/* Disk-format plugin */
struct reiserfs_format_plugin {
    reiserfs_plugin_header_t h;
    
    /* 
	Called during filesystem opening (mounting).
	It reads format-specific super block and initializes
	plugins suitable for this format.
    */
    reiserfs_opaque_t *(*open) (aal_device_t *, aal_device_t *);
    
    /* 
	Called during filesystem creating. It forms format-specific
	super block, initializes plugins and calls their create 
	method.
    */
    reiserfs_opaque_t *(*create) (aal_device_t *, count_t, 
	aal_device_t *, reiserfs_params_opaque_t *);
    
    /*
	Called during filesystem syncing. It calls method sync
	for every "child" plugin (block allocator, journal, etc).
    */
    error_t (*sync) (reiserfs_opaque_t *);

    /*
	Checks format-specific super block for validness. Also checks
	whether filesystem objects lie in valid places. For example,
	format-specific supetr block for format40 must lie in 17-th
	4096 byte block.
    */
    error_t (*check) (reiserfs_opaque_t *);

    /*
	Probes whether filesystem on given device has this format.
	Returns "true" if so and "false" otherwise.
    */
    int (*confirm) (aal_device_t *device);

    /*
	Closes opened or created previously filesystem. Frees
	all assosiated memory.
    */
    void (*close) (reiserfs_opaque_t *);
    
    /*
	Returns format string for this format. For example
	"reiserfs 4.0".
    */
    const char *(*format) (reiserfs_opaque_t *);

    /* 
	Returns offset in blocks where format-specific super block 
	lies.
    */
    blk_t (*offset) (reiserfs_opaque_t *);
    
    /* Gets/sets root block */
    blk_t (*get_root) (reiserfs_opaque_t *);
    void (*set_root) (reiserfs_opaque_t *, blk_t);
    
    /* Gets/sets block count */
    count_t (*get_blocks) (reiserfs_opaque_t *);
    void (*set_blocks) (reiserfs_opaque_t *, count_t);
    
    /* Gets/sets height field */
    uint16_t (*get_height) (reiserfs_opaque_t *);
    void (*set_height) (reiserfs_opaque_t *, uint16_t);
    
    /* Gets/sets free blocks number for this format */
    count_t (*get_free) (reiserfs_opaque_t *);
    void (*set_free) (reiserfs_opaque_t *, count_t);
    
    /* Returns children objects plugins */
    reiserfs_plugin_id_t (*journal_plugin_id) (reiserfs_opaque_t *);
    reiserfs_plugin_id_t (*alloc_plugin_id) (reiserfs_opaque_t *);
    reiserfs_plugin_id_t (*oid_plugin_id) (reiserfs_opaque_t *);
    
    /* 
	Returns initialized children entities (journal, block allocator)
	oid alloactor.
    */
    reiserfs_opaque_t *(*journal) (reiserfs_opaque_t *);
    reiserfs_opaque_t *(*alloc) (reiserfs_opaque_t *);
    reiserfs_opaque_t *(*oid) (reiserfs_opaque_t *);
};

typedef struct reiserfs_format_plugin reiserfs_format_plugin_t;

struct reiserfs_oid_plugin {
    reiserfs_plugin_header_t h;

    reiserfs_opaque_t *(*open) (void *, uint32_t);
    void (*close) (reiserfs_opaque_t *);
    
    oid_t (*alloc) (reiserfs_opaque_t *);
    void (*dealloc) (reiserfs_opaque_t *, oid_t);
    
    oid_t (*next) (reiserfs_opaque_t *);
    oid_t (*used) (reiserfs_opaque_t *);

    oid_t (*root_parent_locality) (reiserfs_opaque_t *);
    oid_t (*root_parent_objectid) (reiserfs_opaque_t *);
    
    oid_t (*root_objectid) (reiserfs_opaque_t *);
};

typedef struct reiserfs_oid_plugin reiserfs_oid_plugin_t;

struct reiserfs_alloc_plugin {
    reiserfs_plugin_header_t h;
    
    reiserfs_opaque_t *(*open) (aal_device_t *, count_t);
    reiserfs_opaque_t *(*create) (aal_device_t *, count_t);
    void (*close) (reiserfs_opaque_t *);
    error_t (*sync) (reiserfs_opaque_t *);

    void (*mark) (reiserfs_opaque_t *, blk_t);
    
    blk_t (*alloc) (reiserfs_opaque_t *);
    void (*dealloc) (reiserfs_opaque_t *, blk_t);

    count_t (*free) (reiserfs_opaque_t *);
    count_t (*used) (reiserfs_opaque_t *);
};

typedef struct reiserfs_alloc_plugin reiserfs_alloc_plugin_t;

struct reiserfs_journal_plugin {
    reiserfs_plugin_header_t h;
    
    reiserfs_opaque_t *(*open) (aal_device_t *);
    reiserfs_opaque_t *(*create) (aal_device_t *, reiserfs_params_opaque_t *params);
    void (*close) (reiserfs_opaque_t *);
    error_t (*sync) (reiserfs_opaque_t *);
    error_t (*replay) (reiserfs_opaque_t *);
};

typedef struct reiserfs_journal_plugin reiserfs_journal_plugin_t;

union reiserfs_plugin {
    reiserfs_plugin_header_t h;
	
    reiserfs_file_plugin_t file;
    reiserfs_dir_plugin_t dir;
    reiserfs_item_plugin_t item;
    reiserfs_node_plugin_t node;
    reiserfs_hash_plugin_t hash;
    reiserfs_tail_plugin_t tail;
    reiserfs_hook_plugin_t hook;
    reiserfs_perm_plugin_t perm;
    reiserfs_format_plugin_t format;
    reiserfs_oid_plugin_t oid;
    reiserfs_alloc_plugin_t alloc;
    reiserfs_journal_plugin_t journal;
    reiserfs_key_plugin_t key;
};

typedef union reiserfs_plugin reiserfs_plugin_t;

/* 
    To create a new item or to insert into the item we need to 
    perform the following operations:
    
    (1) Create the description of the data being inserted.
    (2) Ask item plugin how much space is needed for the 
    data, described in 1.
    
    (3) Free needed space for data being inserted.
    (4) Ask item plugin to create an item (to paste into 
    the item) on the base of description from 1.

    For such purposes we have:
    
    (1) Fixed description structures for all item types (stat, 
    diritem, internal, etc).
    
    (2) Estimate common item method which gets coord of where 
    to insert into (NULL or unit_pos == -1 for insertion, 
    otherwise it is pasting) and data description from 1.
    
    (3) Insert node methods prepare needed space and call 
    Create/Paste item methods if data description is specified.
    
    (4) Create/Paste item methods if data description has not 
    beed specified on 3. 
*/

/* 
    Create item or paste into item on the base of this structure. 
    "data" is a pointer to data to be copied. 
*/ 
struct reiserfs_item_info {    
    void *data;
    void *info;

    uint16_t length;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_item_info reiserfs_item_info_t;

struct reiserfs_internal_info {    
    blk_t blk;
};

typedef struct reiserfs_internal_info reiserfs_internal_info_t;

/*  
    These fields should be changed to what proper description 
    of needed extentions. 
*/
struct reiserfs_stat_info {
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

struct reiserfs_direntry_info {
    uint16_t count;
    reiserfs_entry_info_t *entry;
};

typedef struct reiserfs_direntry_info reiserfs_direntry_info_t;

struct reiserfs_dir_info {
};

typedef struct reiserfs_dir_info reiserfs_dir_info_t;

struct reiserfs_coord {
    aal_block_t *node;
    int16_t item_pos;
    int16_t unit_pos;
};

typedef struct reiserfs_coord reiserfs_coord_t;

struct reiserfs_plugin_factory {
    reiserfs_plugin_t *(*find_by_coords)(reiserfs_plugin_id_t, reiserfs_plugin_id_t);
    reiserfs_plugin_t *(*find_by_label)(const char *);
};

typedef struct reiserfs_plugin_factory reiserfs_plugin_factory_t;

typedef reiserfs_plugin_t *(*reiserfs_plugin_entry_t) (reiserfs_plugin_factory_t *);
typedef error_t (*reiserfs_plugin_func_t) (reiserfs_plugin_t *, void *);

#ifndef ENABLE_COMPACT

#define libreiserfs_plugins_call(action, ops, method, args...)	    \
    ({								    \
	if (!ops.##method##) {					    \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,	    \
		"Method \"" #method "\" isn't implemented in %s.",  \
		#ops);						    \
	    action;						    \
	}							    \
	ops.##method##(##args);					    \
    })

#else

#define libreiserfs_plugins_call(action, ops, method, args...)	    \
    ({ops.##method##(##args);})					    \
    
#endif

#if defined(ENABLE_COMPACT) || defined(ENABLE_MONOLITHIC)
    
#define libreiserfs_plugins_register(entry)			    \
    static reiserfs_plugin_entry_t __plugin_entry		    \
	__attribute__((__section__(".plugins"))) = entry
#else

#define libreiserfs_plugins_register(entry)			    \
    reiserfs_plugin_entry_t __plugin_entry = entry
    
#endif

#define REISERFS_GUESS_PLUGIN_ID 0xff

extern error_t libreiser4_plugins_init(void);
extern void libreiser4_plugins_fini(void);

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
extern reiserfs_plugin_t *libreiser4_plugins_load_by_name(const char *name);
#endif

extern reiserfs_plugin_t *libreiser4_plugins_load_by_entry(reiserfs_plugin_entry_t entry);
extern void libreiser4_plugins_unload(reiserfs_plugin_t *plugin);

extern reiserfs_plugin_t *libreiser4_plugins_find_by_coords(reiserfs_plugin_id_t type,
    reiserfs_plugin_id_t id);

extern reiserfs_plugin_t *libreiser4_plugins_find_by_label(const char *label);
extern error_t libreiser4_plugins_foreach(reiserfs_plugin_func_t plugin_func, void *data);

#endif

