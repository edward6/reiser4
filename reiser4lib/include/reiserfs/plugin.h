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
    REISERFS_JOURNAL_PLUGIN
};

typedef enum reiserfs_plugin_type reiserfs_plugin_type_t;
typedef int reiserfs_plugin_id_t;

#define REISERFS_PLUGIN_MAX_LABEL	16
#define REISERFS_PLUGIN_MAX_DESC	256

struct reiserfs_plugin_header {
    void *handle;
    reiserfs_plugin_id_t id;
    reiserfs_plugin_type_t type;
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

struct reiserfs_item_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_item_plugin reiserfs_item_plugin_t;

struct reiserfs_node_plugin {
    reiserfs_plugin_header_t h;

    reiserfs_opaque_t *(*open)  (aal_device_t *, aal_block_t *);
    reiserfs_opaque_t *(*create)(aal_device_t *, aal_block_t *, uint8_t);
    error_t (*confirm_format) (reiserfs_opaque_t *);
    error_t (*check) (reiserfs_opaque_t *, int);
    uint32_t (*max_item_size) (reiserfs_opaque_t *);
    uint32_t (*max_item_num) (reiserfs_opaque_t *);
    uint32_t (*count) (reiserfs_opaque_t *);
    uint32_t (*get_free_space) (reiserfs_opaque_t *);
    void (*set_free_space) (reiserfs_opaque_t *, uint32_t);
    void (*print) (reiserfs_opaque_t *);
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
	
    reiserfs_opaque_t *(*open) (reiserfs_opaque_t *, aal_device_t *);
    reiserfs_opaque_t *(*create) (reiserfs_opaque_t *, aal_device_t *, count_t, uint16_t);
    void (*close) (reiserfs_opaque_t *, int);
    error_t (*sync) (reiserfs_opaque_t *);
    error_t (*check) (reiserfs_opaque_t *);
    int (*probe) (aal_device_t *device);
    const char *(*format) (reiserfs_opaque_t *);

    blk_t (*root_block) (reiserfs_opaque_t *);
	
    reiserfs_plugin_id_t (*journal_plugin_id) (reiserfs_opaque_t *);
    reiserfs_plugin_id_t (*alloc_plugin_id) (reiserfs_opaque_t *);
    reiserfs_plugin_id_t (*node_plugin_id) (reiserfs_opaque_t *);
};

typedef struct reiserfs_format_plugin reiserfs_format_plugin_t;

struct reiserfs_oid_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_oid_plugin reiserfs_oid_plugin_t;

struct reiserfs_segment {
    blk_t start;
    count_t count;
};

typedef struct reiserfs_segment reiserfs_segment_t;

struct reiserfs_alloc_plugin {
    reiserfs_plugin_header_t h;
    reiserfs_opaque_t *(*open) (aal_device_t *);
    reiserfs_opaque_t *(*create) (aal_device_t *);
    void (*close) (reiserfs_opaque_t *, int);
    error_t (*sync) (reiserfs_opaque_t *);

    error_t (*allocate) (reiserfs_opaque_t *, reiserfs_segment_t *, reiserfs_segment_t *);
    error_t (*deallocate) (reiserfs_opaque_t *, reiserfs_segment_t *, reiserfs_segment_t *);
};

typedef struct reiserfs_alloc_plugin reiserfs_alloc_plugin_t;

struct reiserfs_journal_plugin {
    reiserfs_plugin_header_t h;
    reiserfs_opaque_t *(*open) (aal_device_t *);
    reiserfs_opaque_t *(*create) (aal_device_t *, reiserfs_params_opaque_t *params);
    void (*close) (reiserfs_opaque_t *, int);
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
    reiserfs_plugin_t *(*find_by_coords)(reiserfs_plugin_type_t, reiserfs_plugin_id_t);
    reiserfs_plugin_t *(*find_by_label)(const char *);
};

typedef struct reiserfs_plugins_factory reiserfs_plugins_factory_t;

typedef reiserfs_plugin_t *(*reiserfs_plugin_entry_t) (reiserfs_plugins_factory_t *);

#ifndef ENABLE_COMPACT
#   define reiserfs_plugin_check_routine(plugin, routine, action) \
    do { \
	if (!plugin.##routine##) { \
	    aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_OK, \
		"Routine \"" #routine "\" isn't implemented in plugin %s.", \
		plugin.h.label); \
	    action; \
	} \
    } while(0)
#else
#   define reiserfs_plugin_check_routine(plugin, routine, action) \
	while(0) {}
#endif

#if defined(ENABLE_COMPACT) || defined(ENABLE_MONOLITIC)
#   define reiserfs_plugin_register(entry) \
	static reiserfs_plugin_entry_t __plugin_entry \
	    __attribute__((__section__(".plugins"))) = entry
#else
#   define reiserfs_plugin_register(entry) \
	reiserfs_plugin_entry_t __plugin_entry = entry
#endif
	
#define REISERFS_GUESS_PLUGIN_ID -1

extern error_t reiserfs_plugins_init(void);
extern void reiserfs_plugins_fini(void);

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITIC)
extern reiserfs_plugin_t *reiserfs_plugins_load_by_name(const char *name);
#endif

extern reiserfs_plugin_t *reiserfs_plugins_load_by_entry(reiserfs_plugin_entry_t entry);
extern void reiserfs_plugins_unload(reiserfs_plugin_t *plugin);

extern reiserfs_plugin_t *reiserfs_plugins_find_by_coords(reiserfs_plugin_type_t type, 
    reiserfs_plugin_id_t id);

extern reiserfs_plugin_t *reiserfs_plugins_find_by_label(const char *label);

#endif

