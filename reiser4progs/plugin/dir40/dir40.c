/*
    dir40.c -- reiser4 default directory object plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <sys/stat.h>
#endif

#include <reiser4/reiser4.h>
#include "dir40.h"

static reiserfs_plugin_factory_t *factory = NULL;

#ifndef ENABLE_COMPACT

/*
    FIXME-UMKA: Is it possible will be exist objects without 
    stat data? If so, we need to throw out stat_plugin_id
    from accepted parameters.
*/
static reiserfs_object_hint_t *dir40_build(reiserfs_key_t *parent, 
    reiserfs_key_t *object, uint16_t stat_plugin_id, uint16_t direntry_plugin_id) 
{
    reiserfs_object_hint_t *hint;
    reiserfs_item_hint_t *item_hint;

    reiserfs_entry_hint_t **entry;
    reiserfs_stat_hint_t *stat_hint;
    reiserfs_direntry_hint_t *direntry_hint;

    reiserfs_plugin_t *key_plugin;
    
    oid_t parent_objectid;
    oid_t parent_locality;
    oid_t objectid;

    aal_assert("umka-743", parent != NULL, return NULL);
    aal_assert("umka-744", object != NULL, return NULL);

    key_plugin = object->plugin;
    
    parent_objectid = libreiser4_plugin_call(return NULL, key_plugin->key, 
	get_objectid, parent->body);
    
    parent_locality = libreiser4_plugin_call(return NULL, key_plugin->key, 
	get_locality, parent->body);
    
    objectid = libreiser4_plugin_call(return NULL, key_plugin->key, 
	get_objectid, object->body);
    
    if (!(hint = aal_calloc(sizeof(*hint), 0)))
	return NULL;

    hint->count = 2;
    
    if (!(hint->item = aal_calloc(2 * sizeof(reiserfs_item_hint_t *), 0)))
	goto error_free_hint;
    
    /* Initializing stat data hint */
    if (!(hint->item[0] = aal_calloc(sizeof(reiserfs_item_hint_t), 0)))
	goto error_free_item;
    
    hint->item[0]->type = REISERFS_STAT_ITEM; 
    
    if (!(hint->item[0]->plugin = 
	    factory->find(REISERFS_ITEM_PLUGIN, stat_plugin_id)))
    {
	libreiser4_factory_failed(goto error_free_item0, 
	    find, item, stat_plugin_id);
    }

    if (!(hint->item[0]->hint = aal_calloc(sizeof(reiserfs_stat_hint_t), 0)))
	goto error_free_item0;
   
    hint->item[0]->key.plugin = key_plugin;
    
    aal_memcpy(&hint->item[0]->key.body, 
	object->body, sizeof(object->body));
    
    stat_hint = hint->item[0]->hint;

    stat_hint->mode = S_IFDIR | 0755;
    stat_hint->extmask = 0;
    stat_hint->nlink = 2;
    stat_hint->size = 0;
    
    /* Initializing direntry hint */
    if (!(hint->item[1] = aal_calloc(sizeof(reiserfs_item_hint_t), 0)))
	goto error_free_hint0;
    
    hint->item[1]->type = REISERFS_DIRENTRY_ITEM; 
    
    if (!(hint->item[1]->plugin = factory->find(REISERFS_ITEM_PLUGIN,
	direntry_plugin_id)))
    {
	libreiser4_factory_failed(goto error_free_item1, find, item, 
	    direntry_plugin_id);
    }
    
    if (!(hint->item[1]->hint = aal_calloc(sizeof(reiserfs_direntry_hint_t), 0)))
	goto error_free_item1;

    hint->item[1]->key.plugin = key_plugin; 
    aal_memcpy(&hint->item[1]->key.body, parent, sizeof(parent->body));
    direntry_hint = hint->item[1]->hint;

    direntry_hint->count = 2;
    direntry_hint->key_plugin = key_plugin;
    direntry_hint->hash_plugin = NULL;
   
    libreiser4_plugin_call(goto error_free_hint1, key_plugin->key, 
	build_dir_key, &hint->item[1]->key.body, direntry_hint->hash_plugin, 
	parent_objectid, objectid, ".");
    
    if (!(direntry_hint->entry = aal_calloc(2 * sizeof(reiserfs_entry_hint_t *), 0)))
	goto error_free_hint1;
    
    entry = direntry_hint->entry;

    if (!(entry[0] = aal_calloc(sizeof(reiserfs_entry_hint_t), 0)))
	goto error_free_entry;
    
    entry[0]->locality = parent_objectid;
    entry[0]->objectid = objectid;
    entry[0]->name = ".";
    
    if (!(entry[1] = aal_calloc(sizeof(reiserfs_entry_hint_t), 0)))
	goto error_free_entry0;
    
    entry[1]->locality = parent_locality;
    entry[1]->objectid = parent_objectid;
    entry[1]->name = "..";
    
    return hint;

error_free_entry1:
    aal_free(entry[1]);
error_free_entry0:
    aal_free(entry[0]);
error_free_entry:
    aal_free(direntry_hint->entry);
error_free_hint1:
    aal_free(hint->item[1]->hint);
error_free_item1:
    aal_free(hint->item[1]);
error_free_hint0:
    aal_free(hint->item[0]->hint);
error_free_item0:
    aal_free(hint->item[0]);
error_free_item:
    aal_free(hint->item);
error_free_hint:
    aal_free(hint);
error:
    return NULL;
}

static void dir40_destroy(reiserfs_object_hint_t *hint) {
    int i;
    
    aal_assert("umka-750", hint != NULL, return);

    for (i = 0; i < hint->count; i++) {
	reiserfs_item_hint_t *item_hint = hint->item[i];
	
	if (item_hint->type == REISERFS_DIRENTRY_ITEM) {
	    int j;
	    
	    reiserfs_direntry_hint_t *de_hint = 
		(reiserfs_direntry_hint_t *)item_hint->hint;
	
	    for (j = 0; j < de_hint->count; j++)
		aal_free(de_hint->entry[j]);
	    
	    aal_free(de_hint->entry);
	    aal_free(de_hint);
	}
	aal_free(item_hint);
    }
    aal_free(hint->item);
    aal_free(hint);
}

#endif

static reiserfs_plugin_t dir40_plugin = {
    .dir = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_DIR_PLUGIN,
	    .label = "dir40",
	    .desc = "Directory for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
#ifndef ENABLE_COMPACT
	.build = (void *(*)(void *, void *, uint16_t, uint16_t))
	    dir40_build,

	.destroy = (void (*)(void *))dir40_destroy
#else
	.build = NULL,
	.destroy = NULL
#endif
    }
};

static reiserfs_plugin_t *dir40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &dir40_plugin;
}

libreiser4_factory_register(dir40_entry);

