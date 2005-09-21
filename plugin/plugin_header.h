/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* plugin header. Data structures required by all plugin types. */

#if !defined( __PLUGIN_HEADER_H__ )
#define __PLUGIN_HEADER_H__

/* plugin data-types and constants */

#include "../debug.h"
#include "../dformat.h"

typedef enum {
	REISER4_FILE_PLUGIN_TYPE,
	REISER4_DIR_PLUGIN_TYPE,
	REISER4_ITEM_PLUGIN_TYPE,
	REISER4_NODE_PLUGIN_TYPE,
	REISER4_HASH_PLUGIN_TYPE,
	REISER4_FIBRATION_PLUGIN_TYPE,
	REISER4_FORMATTING_PLUGIN_TYPE,
	REISER4_PERM_PLUGIN_TYPE,
	REISER4_SD_EXT_PLUGIN_TYPE,
	REISER4_FORMAT_PLUGIN_TYPE,
	REISER4_JNODE_PLUGIN_TYPE,
	REISER4_CRYPTO_PLUGIN_TYPE,
	REISER4_DIGEST_PLUGIN_TYPE,
	REISER4_COMPRESSION_PLUGIN_TYPE,
	REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
	REISER4_CLUSTER_PLUGIN_TYPE,
	REISER4_REGULAR_PLUGIN_TYPE,
	REISER4_PLUGIN_TYPES
} reiser4_plugin_type;

struct reiser4_plugin_ops;
/* generic plugin operations, supported by each
    plugin type. */
typedef struct reiser4_plugin_ops reiser4_plugin_ops;

/* the common part of all plugin instances. */
typedef struct plugin_header {
	/* plugin type */
	reiser4_plugin_type type_id;
	/* id of this plugin */
	reiser4_plugin_id id;
	/* plugin operations */
	reiser4_plugin_ops *pops;
/* NIKITA-FIXME-HANS: usage of and access to label and desc is not commented and defined. */
	/* short label of this plugin */
	const char *label;
	/* descriptive string.. */
	const char *desc;
	/* list linkage */
	struct list_head linkage;
} plugin_header;

/* PRIVATE INTERFACES */
/* NIKITA-FIXME-HANS: what is this for and why does it duplicate what is in plugin_header? */
/* plugin type representation. */
typedef struct reiser4_plugin_type_data {
	/* internal plugin type identifier. Should coincide with
	   index of this item in plugins[] array. */
	reiser4_plugin_type type_id;
	/* short symbolic label of this plugin type. Should be no longer
	   than MAX_PLUGIN_TYPE_LABEL_LEN characters including '\0'. */
	const char *label;
	/* plugin type description longer than .label */
	const char *desc;

/* NIKITA-FIXME-HANS: define built-in */
	/* number of built-in plugin instances of this type */
	int builtin_num;
	/* array of built-in plugins */
	void *builtin;
	struct list_head plugins_list;
	size_t size;
} reiser4_plugin_type_data;

extern reiser4_plugin_type_data plugins[REISER4_PLUGIN_TYPES];

int is_type_id_valid(reiser4_plugin_type type_id);
int is_plugin_id_valid(reiser4_plugin_type type_id, reiser4_plugin_id id);

static inline reiser4_plugin *plugin_at(reiser4_plugin_type_data * ptype, int i)
{
	char *builtin;

	builtin = ptype->builtin;
	return (reiser4_plugin *) (builtin + i * ptype->size);
}

/* return plugin by its @type_id and @id */
static inline reiser4_plugin *plugin_by_id(reiser4_plugin_type type_id
					   /* plugin type id */ ,
					   reiser4_plugin_id id /* plugin id */
					   )
{
	assert("nikita-1651", is_type_id_valid(type_id));
	assert("nikita-1652", is_plugin_id_valid(type_id, id));
	return plugin_at(&plugins[type_id], id);
}

extern reiser4_plugin *plugin_by_unsafe_id(reiser4_plugin_type type_id,
					   reiser4_plugin_id id);

/**
 * plugin_by_disk_id - get reiser4_plugin
 * @type_id: plugin type id
 * @did: plugin id in disk format
 *
 * Returns reiser4_plugin by plugin type id an dplugin_id.
 */
static inline reiser4_plugin *plugin_by_disk_id(reiser4_tree * tree UNUSED_ARG,
						reiser4_plugin_type type_id,
						__le16 *plugin_id)
{
	/*
	 * what we should do properly is to maintain within each file-system a
	 * dictionary that maps on-disk plugin ids to "universal" ids. This
	 * dictionary will be resolved on mount time, so that this function
	 * will perform just one additional array lookup.
	 */
	return plugin_by_unsafe_id(type_id, le16_to_cpu(*plugin_id));
}

/* __PLUGIN_HEADER_H__ */
#endif

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
