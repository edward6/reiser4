/*
    plugin.h -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <aal/aal.h>

typedef void reiserfs_opaque_t;
typedef int reiserfs_id_t;

enum reiserfs_plugin_type {
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

typedef enum reiserfs_plugin_type reiserfs_plugin_type_t;

enum reiserfs_item_type_id {
    REISERFS_STAT_ITEM,
    REISERFS_DIRENTRY_ITEM,
    REISERFS_INTERNAL_ITEM,
    REISERFS_FILE_ITEM
};

typedef enum reiserfs_item_type_id reiserfs_item_type_id_t;

#define REISERFS_PLUGIN_MAX_LABEL	16
#define REISERFS_PLUGIN_MAX_DESC	256

/* Common plugin header */
struct reiserfs_plugin_header {
    void *handle;
    reiserfs_id_t id;
    reiserfs_plugin_type_t type;
    const char label[REISERFS_PLUGIN_MAX_LABEL];
    const char desc[REISERFS_PLUGIN_MAX_DESC];
};

typedef struct reiserfs_plugin_header reiserfs_plugin_header_t;

struct reiserfs_key_ops {
    reiserfs_plugin_header_t h;

    /* Confirms key format */
    int (*confirm) (const void *);

    /* Returns minimal key for this key-format */
    const void *(*minimal) (void);
    
    /* Returns maximal key for this key-format */
    const void *(*maximal) (void);

    /* Compares two keys */
    int (*compare) (const void *, const void *);

    /* 
	Cleans key. Actually it just memsets it by zeros,
	but more smart behavior may be implemented.
    */
    void (*clean) (void *);

    /* Gets/sets key type (minor in reiser4 notation) */	
    void (*set_type) (void *, uint32_t);
    uint32_t (*get_type) (const void *);

    /* Gets/sets key locality */
    void (*set_locality) (void *, uint64_t);
    uint64_t (*get_locality) (const void *);
    
    /* Gets/sets key objectid */
    void (*set_objectid) (void *, uint64_t);
    uint64_t (*get_objectid) (const void *);

    /* Gets/sets key offset */
    void (*set_offset) (void *, uint64_t);
    uint64_t (*get_offset) (const void *);

    /* Gets/sets directory key hash */
    void (*set_hash) (void *, uint64_t);
    uint64_t (*get_hash) (const void *);

    /* Gets/sets directory key generation counter */
    void (*set_counter) (void *, uint8_t);
    uint8_t (*get_counter) (const void *);
    
    /* Get size of the key */
    uint8_t (*size) (void);

    error_t (*build_file_key) (void *, uint32_t, uint64_t, uint64_t, uint64_t);
    error_t (*build_dir_key) (void *, void *, uint64_t, uint64_t, const char *);
    
    error_t (*build_file_short_key) (void *, uint32_t, uint64_t, uint64_t, uint8_t);
    error_t (*build_dir_short_key) (void *, const char *, void *, uint8_t);
    
    error_t (*build_key_by_file_short_key) (void *, void *, uint8_t);
    error_t (*build_key_by_dir_short_key) (void *, void *, uint8_t); 
};

typedef struct reiserfs_key_ops reiserfs_key_ops_t;

struct reiserfs_file_ops {
    reiserfs_plugin_header_t h;

    void *(*build) (void *, void *, uint16_t, uint16_t); 
    void (*destroy) (void *);
};

typedef struct reiserfs_file_ops reiserfs_file_ops_t;

struct reiserfs_dir_ops {
    reiserfs_plugin_header_t h;

    void *(*build) (void *, void *, uint16_t, uint16_t); 
    void (*destroy) (void *);
};

typedef struct reiserfs_dir_ops reiserfs_dir_ops_t;

struct reiserfs_item_common_ops {
    reiserfs_item_type_id_t type;

    error_t (*create) (void *, void *);
    int (*lookup) (void *, void *, void *);

    error_t (*confirm) (void *);
    error_t (*check) (void *);
    void (*print) (void *, char *, uint16_t);

    /* 
	Get the max key which could be stored in the item of 
	this type.
    */
    error_t (*max_key) (void *);
    
    int (*unit_add) (void *, void *, void *);
    uint16_t (*unit_count) (void *);
    int (*unit_remove) (void *, int32_t, int32_t);
    
    error_t (*estimate) (void *, void *);
    uint32_t (*minsize) (void);
    
    int (*internal) (void);
};

typedef struct reiserfs_item_common_ops reiserfs_item_common_ops_t;

struct reiserfs_direntry_ops {
};

typedef struct reiserfs_direntry_ops reiserfs_direntry_ops_t;

struct reiserfs_stat_ops {
    uint16_t (*get_mode) (void *);
    void (*set_mode) (void *, uint16_t);
};

typedef struct reiserfs_stat_ops reiserfs_stat_ops_t;

struct reiserfs_internal_ops {
    void (*set_pointer) (void *, blk_t);
    blk_t (*get_pointer) (void *);
    int (*has_pointer) (void *, blk_t);
};

typedef struct reiserfs_internal_ops reiserfs_internal_ops_t;

struct reiserfs_item_ops {
    reiserfs_plugin_header_t h;

    /* Methods common for all item types */
    reiserfs_item_common_ops_t common;

    /* Methods specific to particular type of item */
    union {
	reiserfs_direntry_ops_t dir;
	reiserfs_stat_ops_t stat;
	reiserfs_internal_ops_t internal;
    } specific;
};

typedef struct reiserfs_item_ops reiserfs_item_ops_t;

/*
    Node plugin operates on passed block. It doesn't any 
    initialization, so it hasn't close method and all its
    methods accepts first argument aal_block_t, not initialized
    previously hypothetic instance of node.
*/
struct reiserfs_node_ops {
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
    
    /* Gets key by pos */
    void *(*item_key_at) (aal_block_t *, int32_t);
};

typedef struct reiserfs_node_ops reiserfs_node_ops_t;

struct reiserfs_hash_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hash_ops reiserfs_hash_ops_t;

struct reiserfs_tail_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_tail_ops reiserfs_tail_ops_t;

struct reiserfs_hook_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hook_ops reiserfs_hook_ops_t;

struct reiserfs_perm_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_perm_ops reiserfs_perm_ops_t;

/* Disk-format plugin */
struct reiserfs_format_ops {
    reiserfs_plugin_header_t h;
    
    /* 
	Called during filesystem opening (mounting).
	It reads format-specific super block and initializes
	plugins suitable for this format.
    */
    reiserfs_opaque_t *(*open) (aal_device_t *);
    
    /* 
	Called during filesystem creating. It forms format-specific
	super block, initializes plugins and calls their create 
	method.
    */
    reiserfs_opaque_t *(*create) (aal_device_t *, count_t, uint16_t);
    
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
    reiserfs_id_t (*journal_plugin_id) (reiserfs_opaque_t *);
    reiserfs_id_t (*alloc_plugin_id) (reiserfs_opaque_t *);
    reiserfs_id_t (*oid_plugin_id) (reiserfs_opaque_t *);

    void (*oid)(reiserfs_opaque_t *, void **, void **);
};

typedef struct reiserfs_format_ops reiserfs_format_ops_t;

struct reiserfs_oid_ops {
    reiserfs_plugin_header_t h;

    reiserfs_opaque_t *(*open) (void *, void *);
    reiserfs_opaque_t *(*create) (void *, void *);

    error_t (*sync) (reiserfs_opaque_t *);

    void (*close) (reiserfs_opaque_t *);
    
    uint64_t (*alloc) (reiserfs_opaque_t *);
    void (*dealloc) (reiserfs_opaque_t *, uint64_t);
    
    uint64_t (*next) (reiserfs_opaque_t *);
    uint64_t (*used) (reiserfs_opaque_t *);

    uint64_t (*root_parent_locality) (void);
    uint64_t (*root_parent_objectid) (void);
    uint64_t (*root_objectid) (void);
};

typedef struct reiserfs_oid_ops reiserfs_oid_ops_t;

struct reiserfs_alloc_ops {
    reiserfs_plugin_header_t h;
    
    reiserfs_opaque_t *(*open) (aal_device_t *, count_t);
    reiserfs_opaque_t *(*create) (aal_device_t *, count_t);
    void (*close) (reiserfs_opaque_t *);
    error_t (*sync) (reiserfs_opaque_t *);

    void (*mark) (reiserfs_opaque_t *, blk_t);
    int (*test) (reiserfs_opaque_t *, blk_t);
    
    blk_t (*alloc) (reiserfs_opaque_t *);
    void (*dealloc) (reiserfs_opaque_t *, blk_t);

    count_t (*free) (reiserfs_opaque_t *);
    count_t (*used) (reiserfs_opaque_t *);
};

typedef struct reiserfs_alloc_ops reiserfs_alloc_ops_t;

struct reiserfs_journal_ops {
    reiserfs_plugin_header_t h;
    
    reiserfs_opaque_t *(*open) (aal_device_t *);
    
    reiserfs_opaque_t *(*create) (aal_device_t *, 
	reiserfs_opaque_t *);
    
    void (*area) (reiserfs_opaque_t *, blk_t *start, blk_t *end);
    
    void (*close) (reiserfs_opaque_t *);
    error_t (*sync) (reiserfs_opaque_t *);
    error_t (*replay) (reiserfs_opaque_t *);
};

typedef struct reiserfs_journal_ops reiserfs_journal_ops_t;

union reiserfs_plugin {
    reiserfs_plugin_header_t h;
	
    reiserfs_file_ops_t file;
    reiserfs_dir_ops_t dir;
    reiserfs_item_ops_t item;
    reiserfs_node_ops_t node;
    reiserfs_hash_ops_t hash;
    reiserfs_tail_ops_t tail;
    reiserfs_hook_ops_t hook;
    reiserfs_perm_ops_t perm;
    reiserfs_format_ops_t format;
    reiserfs_oid_ops_t oid;
    reiserfs_alloc_ops_t alloc;
    reiserfs_journal_ops_t journal;
    reiserfs_key_ops_t key;
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
	to insert into (NULL or unit == -1 for insertion, 
	otherwise it is pasting) and data description from 1.
    
    (3) Insert node methods prepare needed space and call 
	Create/Paste item methods if data description is specified.
    
    (4) Create/Paste item methods if data description has not 
	beed specified on 3. 
*/

struct reiserfs_internal_hint {    
    blk_t pointer;
};

typedef struct reiserfs_internal_hint reiserfs_internal_hint_t;

/*  
    These fields should be changed to what proper description 
    of needed extentions. 
*/
struct reiserfs_stat_hint {
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
};

typedef struct reiserfs_stat_hint reiserfs_stat_hint_t;

struct reiserfs_entry_hint {
    uint64_t locality;
    uint64_t objectid;
    char *name;
};

typedef struct reiserfs_entry_hint reiserfs_entry_hint_t;

struct reiserfs_direntry_hint {
    uint16_t count;
    reiserfs_entry_hint_t **entry;
    
    reiserfs_plugin_t *key_plugin;
    reiserfs_plugin_t *hash_plugin;    
};

typedef struct reiserfs_direntry_hint reiserfs_direntry_hint_t;

/* 
    Create item or paste into item on the base of this structure. 
    "data" is a pointer to data to be copied. 
*/ 
struct reiserfs_item_hint {
    reiserfs_item_type_id_t type;
    /*
	This is pointer to already formated item body. It 
	is useful for item copying, replacing, etc. This
	will be used by fsck probably.
    */
    void *data;

    /*
	This is pointer to hint which describes item.
	It is widely used for creating an item.
    */
    void *hint;

    struct {
	reiserfs_plugin_t *plugin;
	uint8_t body[24];
    } key;
    
    uint16_t length;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_item_hint reiserfs_item_hint_t;

struct reiserfs_object_hint {
    uint16_t count;
    reiserfs_item_hint_t **item;
};

typedef struct reiserfs_object_hint reiserfs_object_hint_t;

struct reiserfs_pos {
    int16_t item;
    int16_t unit;
};

typedef struct reiserfs_pos reiserfs_pos_t;

struct reiserfs_plugin_factory {
    reiserfs_plugin_t *(*find)(reiserfs_plugin_type_t, reiserfs_id_t);
};

typedef struct reiserfs_plugin_factory reiserfs_plugin_factory_t;

typedef reiserfs_plugin_t *(*reiserfs_plugin_entry_t) (reiserfs_plugin_factory_t *);
typedef error_t (*reiserfs_plugin_func_t) (reiserfs_plugin_t *, void *);

/* Plugin functions and macros */
#ifndef ENABLE_COMPACT

#define libreiser4_plugin_call(action, ops, method, args...)	    \
    ({								    \
	if (!ops.method) {					    \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,	    \
		"Method \"" #method "\" isn't implemented in %s.",  \
		#ops);						    \
	    action;						    \
	}							    \
	ops.method(args);					    \
    })

#else

#define libreiser4_plugin_call(action, ops, method, args...)	    \
    ({ops.method(args);})					    \
    
#endif

#define REISERFS_GUESS_PLUGIN_ID 0xff

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)

extern reiserfs_plugin_t *libreiser4_plugin_load_by_name(const char *name);

#endif

extern reiserfs_plugin_t *libreiser4_plugin_load_by_entry(reiserfs_plugin_entry_t entry);

extern void libreiser4_plugin_unload(reiserfs_plugin_t *plugin);

/* Factory functions */
extern error_t libreiser4_factory_init(void);
extern void libreiser4_factory_done(void);

#if defined(ENABLE_COMPACT) || defined(ENABLE_MONOLITHIC)
    
#define libreiser4_factory_register(entry)			    \
    static reiserfs_plugin_entry_t __plugin_entry		    \
	__attribute__((__section__(".plugins"))) = entry
#else

#define libreiser4_factory_register(entry)			    \
    reiserfs_plugin_entry_t __plugin_entry = entry

#endif

#define libreiser4_factory_failed(action, oper, type, id)	    \
    do {							    \
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,	    \
	    "Can't " #oper " " #type " plugin by its id %x.",	    \
	    id);						    \
	action;							    \
    } while (0)

extern reiserfs_plugin_t *libreiser4_factory_find(reiserfs_plugin_type_t type,
    reiserfs_id_t id);

extern error_t libreiser4_factory_foreach(reiserfs_plugin_func_t plugin_func, 
    void *data);

#endif

