/*
    oid.c -- oid allocator common code and API functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

error_t reiserfs_oid_open(reiserfs_fs_t *fs) {
    reiserfs_plugin_id_t plugin_id;
	
    aal_assert("umka-518", fs != NULL, return -1);
    aal_assert("umka-519", fs->format != NULL, return -1);

    if (!(fs->oid = aal_calloc(sizeof(*fs->oid), 0)))
	return -1;
    
    plugin_id = reiserfs_format_oid_plugin_id(fs);
    if (!(fs->oid->plugin = libreiser4_factory_find_by_coord(REISERFS_OID_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find oid allocator plugin by its id %x.", plugin_id);
	goto error_free_oid;
    }

    if (!(fs->oid->entity = reiserfs_format_oid(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open oid allocator.");
	goto error_free_oid;
    }

    return 0;
    
error_free_oid:
    aal_free(fs->oid);
    fs->oid = NULL;
error:
    return -1;
}

void reiserfs_oid_close(reiserfs_fs_t *fs) {
    aal_assert("umka-520", fs != NULL, return);
    aal_assert("umka-523", fs->oid != NULL, return);

    aal_free(fs->oid);
    fs->oid = NULL;
}

uint64_t reiserfs_oid_alloc(reiserfs_fs_t *fs) {
    aal_assert("umka-521", fs != NULL, return 0);
    aal_assert("umka-522", fs->oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->oid->plugin->oid, 
	alloc, fs->oid->entity);
}

void reiserfs_oid_dealloc(reiserfs_fs_t *fs, uint64_t oid) {
    aal_assert("umka-524", fs != NULL, return);
    aal_assert("umka-525", fs->oid != NULL, return);
    
    libreiser4_plugin_call(return, fs->oid->plugin->oid, 
	dealloc, fs->oid->entity, oid);
}

uint64_t reiserfs_oid_next(reiserfs_fs_t *fs) {
    aal_assert("umka-526", fs != NULL, return 0);
    aal_assert("umka-527", fs->oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->oid->plugin->oid, 
	next, fs->oid->entity);
}

uint64_t reiserfs_oid_used(reiserfs_fs_t *fs) {
    aal_assert("umka-526", fs != NULL, return 0);
    aal_assert("umka-527", fs->oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->oid->plugin->oid, 
	used, fs->oid->entity);
}

uint64_t reiserfs_oid_root_parent_locality(reiserfs_fs_t *fs) {
    aal_assert("umka-531", fs != NULL, return 0);
    aal_assert("umka-532", fs->oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->oid->plugin->oid, 
	root_parent_locality,);
}

uint64_t reiserfs_oid_root_parent_objectid(reiserfs_fs_t *fs) {
    aal_assert("umka-533", fs != NULL, return 0);
    aal_assert("umka-534", fs->oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->oid->plugin->oid, 
	root_parent_objectid,);
}

uint64_t reiserfs_oid_root_objectid(reiserfs_fs_t *fs) {
    aal_assert("umka-535", fs != NULL, return 0);
    aal_assert("umka-536", fs->oid != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->oid->plugin->oid, 
	root_objectid,);
}

