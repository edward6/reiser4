/*
    plugin.h -- reiserfs plugin factory implementation.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman.
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <aal/aal.h>

typedef void reiserfs_opaque_t;
typedef void reiserfs_params_opaque_t;

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
    REISERFS_JOURNAL_PLUGIN
};

enum reiserfs_item_type_id {
    STAT_DATA_ITEM,
    DIR_ENTRY_ITEM,
    INTERNAL_ITEM,
    FILE_ITEM
};

typedef enum reiserfs_item_type_id reiserfs_item_type_id_t;
typedef int reiserfs_plugin_id_t;

#define REISERFS_PLUGIN_MAX_LABEL	16
#define REISERFS_PLUGIN_MAX_DESC	256

struct reiserfs_plugin_header {
    void *handle;
    reiserfs_plugin_id_t id;
    reiserfs_plugin_id_t type;
    const char label[REISERFS_PLUGIN_MAX_LABEL];
    const char desc[REISERFS_PLUGIN_MAX_DESC];
};

typedef struct reiserfs_plugin_header reiserfs_plugin_header_t;

struct reiserfs_file_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_file_plugin reiserfs_file_plugin_t;

struct reiserfs_dir_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_dir_plugin reiserfs_dir_plugin_t;

struct reiserfs_common_item_plugin {
    reiserfs_item_type_id_t item_type;

    error_t (*create) (reiserfs_opaque_t *, reiserfs_opaque_t *);
    error_t (*open) (reiserfs_opaque_t *);
    error_t (*close) (reiserfs_opaque_t *);
    int (*lookup) (reiserfs_opaque_t *, reiserfs_opaque_t *);

    int (*add_unit) (reiserfs_opaque_t *, int32_t, 
	reiserfs_opaque_t *unit_info);
    
    error_t (*confirm) (reiserfs_opaque_t *);
    error_t (*check) (reiserfs_opaque_t *);
    void (*print) (reiserfs_opaque_t *, char *);
    uint16_t (*units_count) (reiserfs_opaque_t *);
    
    int (*remove_units) (reiserfs_opaque_t *, int32_t, int32_t);
    
    /*  
	Estimate gets coords (where to insert/past into) and item_info
	(what needs to be inserted). If coords is NULL, then this is 
	insertion, othewise it is pasting. The amount of needed space
	should be set into item_info->lenght. 
    */
    error_t (*estimate) (reiserfs_opaque_t *, reiserfs_opaque_t *);
    
    int (*is_internal) ();
};

typedef struct reiserfs_common_item_plugin reiserfs_common_item_plugin_t;

struct reiserfs_dir_entry_ops {
    int (*add_entry) (reiserfs_opaque_t *, int32_t, reiserfs_opaque_t *parent, 
	char *, reiserfs_opaque_t *entry);
    
    int (*max_name_len) (int blocksize);
};

typedef struct reiserfs_dir_entry_ops reiserfs_dir_entry_ops_t;

struct reiserfs_file_ops {
    int (*write) (reiserfs_opaque_t *file, void *buff);
    
    int (*read) (reiserfs_opaque_t *file, void *buff);
};

typedef struct reiserfs_file_ops reiserfs_file_ops_t;

struct reiserfs_stat_ops {
};

typedef struct reiserfs_stat_ops reiserfs_stat_ops_t;

struct reiserfs_internal_ops {
    blk_t (*down_link) (reiserfs_opaque_t *);
    
    /* Check that given internal item contains given pointer. */
    int (*has_pointer_to) (reiserfs_opaque_t *, blk_t);
};

typedef struct reiserfs_internal_ops reiserfs_internal_ops_t;

struct reiserfs_item_plugin {
    reiserfs_plugin_header_t h;

    /* Methods common for all item types */
    reiserfs_common_item_plugin_t common;

    /* Methods specific to particular type of item */
    union {
	reiserfs_dir_entry_ops_t dir;
	reiserfs_file_ops_t file;
	reiserfs_stat_ops_t stat;
	reiserfs_internal_ops_t internal;
    } specific;
};

typedef struct reiserfs_item_plugin reiserfs_item_plugin_t;

struct reiserfs_node_plugin {
    reiserfs_plugin_header_t h;

    error_t (*open) (reiserfs_opaque_t *);
    error_t (*create) (reiserfs_opaque_t *, uint8_t);
    error_t (*close) (reiserfs_opaque_t *);
    error_t (*confirm) (reiserfs_opaque_t *);
    error_t (*check) (reiserfs_opaque_t *, int);
    int (*lookup) (reiserfs_opaque_t *, reiserfs_opaque_t *);
    error_t (*insert) (reiserfs_opaque_t *, reiserfs_opaque_t *, 
	    reiserfs_opaque_t *);
    error_t (*update_parent) (reiserfs_opaque_t *parent, 
	reiserfs_opaque_t *node);
 
    uint16_t (*item_overhead) (reiserfs_opaque_t *);
    uint16_t (*item_max_size) (reiserfs_opaque_t *);
    uint16_t (*item_max_num) (reiserfs_opaque_t *);
    uint16_t (*item_count) (reiserfs_opaque_t *);
    uint16_t (*item_length) (reiserfs_opaque_t *, int32_t);
    reiserfs_opaque_t *(*key_at) (reiserfs_opaque_t *, int32_t);
    uint16_t (*item_plugin_id) (reiserfs_opaque_t *, int32_t);
    void *(*item) (reiserfs_opaque_t *, int32_t);
    
    uint16_t (*get_free_space) (reiserfs_opaque_t *);
    void (*set_free_space) (reiserfs_opaque_t *, uint32_t);

    /*  
	this is optional method. That means that there could be 
	node formats which do not keep level 
    */
    uint8_t (*get_level) (reiserfs_opaque_t *);
    void (*set_level) (reiserfs_opaque_t *, uint8_t);

    void (*print) (reiserfs_opaque_t *, char *);
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

struct reiserfs_format_plugin {
    reiserfs_plugin_header_t h;
	
    reiserfs_opaque_t *(*open) (aal_device_t *, aal_device_t *);
    
    reiserfs_opaque_t *(*create) (aal_device_t *, count_t, 
	aal_device_t *, reiserfs_params_opaque_t *);
    
    error_t (*sync) (reiserfs_opaque_t *);
    error_t (*check) (reiserfs_opaque_t *);
    int (*probe) (aal_device_t *device);
    void (*close) (reiserfs_opaque_t *);
    
    const char *(*format) (reiserfs_opaque_t *);
    blk_t (*offset) (reiserfs_opaque_t *);
    
    blk_t (*get_root) (reiserfs_opaque_t *);
    void (*set_root) (reiserfs_opaque_t *, blk_t);
    
    count_t (*get_blocks) (reiserfs_opaque_t *);
    void (*set_blocks) (reiserfs_opaque_t *, count_t);
    
    count_t (*get_free) (reiserfs_opaque_t *);
    void (*set_free) (reiserfs_opaque_t *, count_t);
    
    reiserfs_plugin_id_t (*journal_plugin_id) (reiserfs_opaque_t *);
    reiserfs_plugin_id_t (*alloc_plugin_id) (reiserfs_opaque_t *);
    reiserfs_plugin_id_t (*oid_plugin_id) (reiserfs_opaque_t *);
    
    reiserfs_opaque_t *(*journal) (reiserfs_opaque_t *);
    reiserfs_opaque_t *(*alloc) (reiserfs_opaque_t *);
    reiserfs_opaque_t *(*oid) (reiserfs_opaque_t *);
};

typedef struct reiserfs_format_plugin reiserfs_format_plugin_t;

struct reiserfs_oid_plugin {
    reiserfs_plugin_header_t h;

    reiserfs_opaque_t *(*init) (uint64_t, uint64_t);
    void (*close) (reiserfs_opaque_t *);
    
    uint64_t (*alloc) (reiserfs_opaque_t *);
    void (*dealloc) (reiserfs_opaque_t *, uint64_t);
    
    uint64_t (*next) (reiserfs_opaque_t *);
    uint64_t (*used) (reiserfs_opaque_t *);
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
};

typedef union reiserfs_plugin reiserfs_plugin_t;

struct reiserfs_plugins_factory {
    reiserfs_plugin_t *(*find_by_coords)(reiserfs_plugin_id_t, reiserfs_plugin_id_t);
    reiserfs_plugin_t *(*find_by_label)(const char *);
};

typedef struct reiserfs_plugins_factory reiserfs_plugins_factory_t;

typedef reiserfs_plugin_t *(*reiserfs_plugin_entry_t) (reiserfs_plugins_factory_t *);
typedef error_t (*reiserfs_plugin_func_t) (reiserfs_plugin_t *, void *);

#ifndef ENABLE_COMPACT
#   define reiserfs_check_method(ops, method, action) \
    do { \
	if (!ops.##method##) { \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, \
		"Method \"" #method "\" isn't implemented in %s.", \
		#ops); \
	    action; \
	} \
    } while(0)
#else
#   define reiserfs_check_method(plugin, routine, action) \
	while(0) {}
#endif

#if defined(ENABLE_COMPACT) || defined(ENABLE_MONOLITHIC)
#   define reiserfs_plugin_register(entry) \
	static reiserfs_plugin_entry_t __plugin_entry \
	    __attribute__((__section__(".plugins"))) = entry
#else
#   define reiserfs_plugin_register(entry) \
	reiserfs_plugin_entry_t __plugin_entry = entry
#endif

#define REISERFS_GUESS_PLUGIN_ID 0xff

extern error_t reiserfs_plugins_init(void);
extern void reiserfs_plugins_fini(void);

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
extern reiserfs_plugin_t *reiserfs_plugins_load_by_name(const char *name);
#endif

extern reiserfs_plugin_t *reiserfs_plugins_load_by_entry(reiserfs_plugin_entry_t entry);
extern void reiserfs_plugins_unload(reiserfs_plugin_t *plugin);

extern reiserfs_plugin_t *reiserfs_plugins_find_by_coords(reiserfs_plugin_id_t type,
    reiserfs_plugin_id_t id);

extern reiserfs_plugin_t *reiserfs_plugins_find_by_label(const char *label);
extern error_t reiserfs_plugins_foreach(reiserfs_plugin_func_t plugin_func, void *data);

#endif

