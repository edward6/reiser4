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
	REISERFS_LAYOUT_PLUGIN,
	REISERFS_OID_PLUGIN,
	REISERFS_ALLOC_PLUGIN
};

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
	int nlink;
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

typedef void reiserfs_layout_opaque_t;

struct reiserfs_layout_plugin {
	reiserfs_plugin_header_t h;
	reiserfs_layout_opaque_t *(*init) (aal_device_t *);
	void (*done) (reiserfs_layout_opaque_t *);
};

typedef struct reiserfs_layout_plugin reiserfs_layout_plugin_t;

struct reiserfs_oid_plugin {
	reiserfs_plugin_header_t h;
};

typedef struct reiserfs_oid_plugin reiserfs_oid_plugin_t;

struct reiserfs_alloc_plugin {
	reiserfs_plugin_header_t h;
};

typedef struct reiserfs_alloc_plugin reiserfs_alloc_plugin_t;

struct reiserfs_journal_plugin {
	reiserfs_plugin_header_t h;
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
	reiserfs_layout_plugin_t layout;
	reiserfs_oid_plugin_t oid;
	reiserfs_alloc_plugin_t alloc;
	reiserfs_journal_plugin_t journal;
};

typedef union reiserfs_plugin reiserfs_plugin_t;

extern reiserfs_plugin_t *reiserfs_plugin_load_by_name(const char *name, 
	const char *point);

extern reiserfs_plugin_t *reiserfs_plugin_load_by_cords(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id);

extern void reiserfs_plugin_unload(reiserfs_plugin_t *plugin);

#endif

