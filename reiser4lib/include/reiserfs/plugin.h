/*
	plugin.h -- reiserfs plugin factory implementation.
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <aal/aal.h>

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

#define REISERFS_UNSUPPORTED_PLUGIN 255

typedef enum reiserfs_plugin_type reiserfs_plugin_type_t;
typedef int reiserfs_plugin_id_t;

#define REISERFS_PLUGIN_MAX_LABEL	255
#define REISERFS_PLUGIN_MAX_DESC	4096

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
	
	reiserfs_format_opaque_t *(*init) (aal_device_t *);
	void (*done) (reiserfs_format_opaque_t *);
	reiserfs_plugin_id_t (*journal_plugin_id) (reiserfs_format_opaque_t *);
	reiserfs_plugin_id_t (*alloc_plugin_id) (reiserfs_format_opaque_t *);
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
	reiserfs_alloc_opaque_t *(*init) (aal_device_t *);
	void (*done) (reiserfs_alloc_opaque_t *);
};

typedef struct reiserfs_alloc_plugin reiserfs_alloc_plugin_t;

typedef void reiserfs_journal_opaque_t;

struct reiserfs_journal_plugin {
	reiserfs_plugin_header_t h;
	reiserfs_journal_opaque_t *(*init) (aal_device_t *);
	void (*done) (reiserfs_journal_opaque_t *);
	int (*replay) (reiserfs_journal_opaque_t *);
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

extern reiserfs_plugin_t *reiserfs_plugin_load(const char *name, 
	const char *point);

reiserfs_plugin_t *reiserfs_plugin_find(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id);

extern void reiserfs_plugin_unload(reiserfs_plugin_t *plugin);

#endif

