/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* plugin header. Data structures required by all plugin types. */

#if !defined( __PLUGIN_HEADER_H__ )
#define __PLUGIN_HEADER_H__

/* plugin data-types and constants */

#include "../tslist.h"

typedef enum {
	REISER4_FILE_PLUGIN_TYPE,
	REISER4_DIR_PLUGIN_TYPE,
	REISER4_ITEM_PLUGIN_TYPE,
	REISER4_NODE_PLUGIN_TYPE,
	REISER4_HASH_PLUGIN_TYPE,
	REISER4_TAIL_PLUGIN_TYPE,
	REISER4_PERM_PLUGIN_TYPE,
	REISER4_SD_EXT_PLUGIN_TYPE,
	REISER4_FORMAT_PLUGIN_TYPE,
	REISER4_OID_ALLOCATOR_PLUGIN_TYPE,
	REISER4_SPACE_ALLOCATOR_PLUGIN_TYPE,
	REISER4_JNODE_PLUGIN_TYPE,
	REISER4_CRYPTO_PLUGIN_TYPE,
	REISER4_COMPRESSION_PLUGIN_TYPE,
	REISER4_PSEUDO_PLUGIN_TYPE,
	REISER4_PLUGIN_TYPES
} reiser4_plugin_type;

struct reiser4_plugin_ops;
/* generic plugin operations, supported by each 
    plugin type. */
typedef struct reiser4_plugin_ops reiser4_plugin_ops;

struct reiser4_plugin_ref;
typedef struct reiser4_plugin_ref reiser4_plugin_ref;

TS_LIST_DECLARE(plugin);

/* common part of each plugin instance. */
typedef struct plugin_header {
	/* plugin type */
	reiser4_plugin_type type_id;
	/* id of this plugin */
	reiser4_plugin_id id;
	/* plugin operations */
	reiser4_plugin_ops *pops;
	/* short label of this plugin */
	const char *label;
	/* descriptive string. Put your copyright message here. */
	const char *desc;
	/* list linkage */
	plugin_list_link linkage;
} plugin_header;

/* __PLUGIN_HEADER_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
