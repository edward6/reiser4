/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* plugin-sets. see fs/reiser4/plugin/plugin_set.c for details */

#if !defined( __PLUGIN_SET_H__ )
#define __PLUGIN_SET_H__

#include "../tshash.h"
#include "plugin.h"

struct plugin_set;
typedef struct plugin_set plugin_set;

TS_HASH_DECLARE(ps, plugin_set);

struct plugin_set {
	atomic_t            ref;
	__u32               hashval;
	/* plugin of file */
	file_plugin        *file;
	/* plugin of dir */
	dir_plugin         *dir;
	/* perm plugin for this file */
	perm_plugin        *perm;
	/* tail policy plugin. Only meaningful for regular files */
	tail_plugin        *tail;
	/* hash plugin. Only meaningful for directories. */
	hash_plugin        *hash;
	/* plugin of stat-data */
	item_plugin        *sd;
	/* plugin of items a directory is built of */
	item_plugin        *dir_item;
	/* crypto plugin */
	crypto_plugin      *crypto;
	/* digest plugin */
	digest_plugin      *digest;	
	/* compression plugin */
	compression_plugin *compression;
	ps_hash_link        link;
};

extern plugin_set *plugin_set_get_empty(void);
extern plugin_set *plugin_set_clone(plugin_set *set);
extern void        plugin_set_put(plugin_set *set);

extern int plugin_set_file       (plugin_set **set, file_plugin *file);
extern int plugin_set_dir        (plugin_set **set, dir_plugin *file);
extern int plugin_set_perm       (plugin_set **set, perm_plugin *file);
extern int plugin_set_tail       (plugin_set **set, tail_plugin *file);
extern int plugin_set_hash       (plugin_set **set, hash_plugin *file);
extern int plugin_set_sd         (plugin_set **set, item_plugin *file);
extern int plugin_set_dir_item   (plugin_set **set, item_plugin *file);
extern int plugin_set_crypto     (plugin_set **set, crypto_plugin *file);
extern int plugin_set_digest     (plugin_set **set, digest_plugin *file);
extern int plugin_set_compression(plugin_set **set, compression_plugin *file);

extern int  plugin_set_init(void);
extern void plugin_set_done(void);

/* __PLUGIN_SET_H__ */
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
