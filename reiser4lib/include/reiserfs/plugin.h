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

typedef void reiserfs_file_opaque_t;

struct reiserfs_file_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_file_plugin reiserfs_file_plugin_t;

typedef void reiserfs_dir_opaque_t;

struct reiserfs_dir_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_dir_plugin reiserfs_dir_plugin_t;

typedef void reiserfs_item_opaque_t;

struct reiserfs_item_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_item_plugin reiserfs_item_plugin_t;

typedef void reiserfs_node_opaque_t;

struct reiserfs_node_plugin {
    reiserfs_plugin_header_t h;

    reiserfs_node_opaque_t *(*open)  (aal_device_t *, aal_block_t *);
    reiserfs_node_opaque_t *(*create)(aal_device_t *, aal_block_t *, uint8_t);
    error_t (*confirm_format) (reiserfs_node_opaque_t *);
    error_t (*check) (reiserfs_node_opaque_t *, int);
    uint32_t (*max_item_size) (reiserfs_node_opaque_t *);
    uint32_t (*max_item_num) (reiserfs_node_opaque_t *);
    uint32_t (*count) (reiserfs_node_opaque_t *);
    uint32_t (*get_free_space) (reiserfs_node_opaque_t *);
    void (*set_free_space) (reiserfs_node_opaque_t *, uint32_t);
    void (*print) (reiserfs_node_opaque_t *);
};

typedef struct reiserfs_node_plugin reiserfs_node_plugin_t;

typedef void reiserfs_hash_opaque_t;

struct reiserfs_hash_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hash_plugin reiserfs_hash_plugin_t;

typedef void reiserfs_tail_opaque_t;

struct reiserfs_tail_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_tail_plugin reiserfs_tail_plugin_t;

typedef void reiserfs_hook_opaque_t;

struct reiserfs_hook_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hook_plugin reiserfs_hook_plugin_t;

typedef void reiserfs_perm_opaque_t;

struct reiserfs_perm_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_perm_plugin reiserfs_perm_plugin_t;

typedef void reiserfs_format_opaque_t;

struct reiserfs_format_plugin {
    reiserfs_plugin_header_t h;
	
    reiserfs_format_opaque_t *(*open) (aal_device_t *);
    reiserfs_format_opaque_t *(*create) (aal_device_t *, count_t);
    void (*close) (reiserfs_format_opaque_t *, int);
    error_t (*sync) (reiserfs_format_opaque_t *);
    error_t (*check) (reiserfs_format_opaque_t *);
    int (*probe) (aal_device_t *device);
    const char *(*format) (reiserfs_format_opaque_t *);

    blk_t (*root_block) (reiserfs_format_opaque_t *);
	
    reiserfs_plugin_id_t (*journal_plugin_id) (reiserfs_format_opaque_t *);
    reiserfs_plugin_id_t (*alloc_plugin_id) (reiserfs_format_opaque_t *);
    reiserfs_plugin_id_t (*node_plugin_id) (reiserfs_format_opaque_t *);
};

typedef struct reiserfs_format_plugin reiserfs_format_plugin_t;

typedef void reiserfs_oid_opaque_t;

struct reiserfs_oid_plugin {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_oid_plugin reiserfs_oid_plugin_t;

typedef void reiserfs_alloc_opaque_t;

struct reiserfs_alloc_plugin {
    reiserfs_plugin_header_t h;
    reiserfs_alloc_opaque_t *(*open) (aal_device_t *);
    reiserfs_alloc_opaque_t *(*create) (aal_device_t *);
    void (*close) (reiserfs_alloc_opaque_t *, int);
    error_t (*sync) (reiserfs_alloc_opaque_t *);
};

typedef struct reiserfs_alloc_plugin reiserfs_alloc_plugin_t;

typedef void reiserfs_journal_opaque_t;

struct reiserfs_journal_plugin {
    reiserfs_plugin_header_t h;
    reiserfs_journal_opaque_t *(*open) (aal_device_t *);
    reiserfs_journal_opaque_t *(*create) (aal_device_t *, reiserfs_params_opaque_t *params);
    void (*close) (reiserfs_journal_opaque_t *, int);
    error_t (*sync) (reiserfs_journal_opaque_t *);
    error_t (*replay) (reiserfs_journal_opaque_t *);
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

typedef reiserfs_plugin_t *(*reiserfs_plugin_entry_t) (void);

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
#   define reiserfs_plugin_check_routine(plugin, routine, action)i \
	while(0) {}
#endif

#ifndef ENABLE_COMPACT
#   define reiserfs_plugin_register(plugin) \
    static reiserfs_plugin_t *reiserfs_plugin_main(void) { \
        return &plugin; \
    } \
      \
    reiserfs_plugin_entry_t __plugin_entry = reiserfs_plugin_main
#else
#   define reiserfs_plugin_register(plugin) \
    static reiserfs_plugin_t *reiserfs_plugin_main(void) { \
        return &plugin; \
    } \
      \
    static reiserfs_plugin_entry_t __plugin_entry \
	__attribute__((__section__(".plugins"))) = reiserfs_plugin_main
#endif

#define REISERFS_GUESS_PLUGIN_ID -1

extern error_t reiserfs_plugins_init(void);
extern void reiserfs_plugins_fini(void);

#ifndef ENABLE_COMPACT
extern reiserfs_plugin_t *reiserfs_plugins_load_by_name(const char *name);
#endif

extern reiserfs_plugin_t *reiserfs_plugins_load_by_entry(reiserfs_plugin_entry_t entry);
extern void reiserfs_plugins_unload(reiserfs_plugin_t *plugin);

extern reiserfs_plugin_t *reiserfs_plugins_find_by_coords(reiserfs_plugin_type_t type, 
    reiserfs_plugin_id_t id);

extern reiserfs_plugin_t *reiserfs_plugins_find_by_label(const char *label);

#endif

