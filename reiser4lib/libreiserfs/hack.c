/*
    hack.c -- temporary code for creating empty b*tree. Will be rewrote later.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <sys/stat.h>

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include <plugin/node40/node40.h>
#include <plugin/stat40/stat40.h>
#include <plugin/internal40/internal40.h>
#include <plugin/direntry40/direntry40.h>

#define OID_CHARS (sizeof(uint64_t) - 1)

static uint64_t pack_string( const char *name, int start_idx) {
    unsigned i;
    uint64_t str;

    str = 0;
    for( i = 0 ; ( i < sizeof str - start_idx ) && name[ i ] ; ++ i ) {
	str <<= 8;
	str |= ( unsigned char ) name[ i ];
    }
    str <<= ( sizeof str - i - start_idx ) << 3;
    return str;
}


blk_t hack_create_tree(reiserfs_fs_t *fs, reiserfs_plugin_id_t node_plugin_id) {
    blk_t root_blk, blk;
    aal_block_t *block;

    reiserfs_nh40_t *node;
    reiserfs_ih40_t *item;
    
    reiserfs_internal40_t *internal_body;
    reiserfs_stat40_base_t *stat_body;
    reiserfs_direntry40_t *direntry_body;

    reiserfs_objid_t *dot_key;
    reiserfs_objid_t *dot_dot_key;

    aal_assert("umka-486", fs != NULL, return -1);

    /* Forming internal node */
    
    if (!(blk = reiserfs_alloc_find(fs)))
	return 0;

    root_blk = blk;
    
    reiserfs_alloc_use(fs, blk);
    if (!(block = aal_device_alloc_block(fs->device, blk, 0)))
	return 0;
    
    node = (reiserfs_nh40_t *)block->data;
    node->header.plugin_id = node_plugin_id;

    nh40_set_level(node, 2);
    nh40_set_magic(node, REISERFS_NODE40_MAGIC);
    nh40_set_num_items(node, 1);
    
    nh40_set_free_space_start(node, sizeof(reiserfs_nh40_t) + 
	sizeof(reiserfs_internal40_t));
    
    nh40_set_free_space(node, block->size - sizeof(reiserfs_nh40_t) - 
	sizeof(reiserfs_ih40_t) - sizeof(reiserfs_internal40_t));

    /* Forming internal item and body */
    item = (reiserfs_ih40_t *)(block->data + block->size) - 1;
    
    aal_memset(&item->key, 0, sizeof(reiserfs_key_t));
    
    set_key_type(&item->key, KEY_SD_MINOR);
    set_key_locality(&item->key, 41);
    set_key_objectid(&item->key, 42);
    
    ih40_set_plugin_id(item, 0x3);
    ih40_set_length(item, sizeof(reiserfs_internal40_t));
    ih40_set_offset(item, sizeof(reiserfs_nh40_t));
    
    internal_body = (reiserfs_internal40_t *)(block->data + ih40_get_offset(item));
    
    if (!(blk = reiserfs_alloc_find(fs))) {
	aal_device_free_block(block);
	return 0;
    }	

    reiserfs_alloc_use(fs, blk);
    internal_body->block_nr = blk;
    
    if (aal_device_write_block(fs->device, block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't write block %llu on device.", 
	    aal_device_get_block_nr(fs->device, block));
	aal_device_free_block(block);
	return 0;
    }
    
    aal_device_free_block(block);
 
    /* Forming leaf node */
    if (!(block = aal_device_alloc_block(fs->device, blk, 0)))
	return 0;
    
    node = (reiserfs_nh40_t *)block->data;
    node->header.plugin_id = node_plugin_id;

    nh40_set_level(node, 1);
    nh40_set_magic(node, REISERFS_NODE40_MAGIC);
    nh40_set_num_items(node, 2);
    
    nh40_set_free_space_start(node, sizeof(reiserfs_nh40_t) + 
	sizeof(reiserfs_stat40_base_t) + sizeof(reiserfs_direntry40_t) + 
	2*sizeof(reiserfs_direntry40_unit_t) + 2*sizeof(reiserfs_objid_t) + 2 + 3);
    
    nh40_set_free_space(node, block->size - (sizeof(reiserfs_nh40_t) + 
	(2*sizeof(reiserfs_ih40_t)) + sizeof(reiserfs_stat40_base_t) + 
	sizeof(reiserfs_direntry40_t) + 2*sizeof(reiserfs_direntry40_unit_t) + 2 + 3 + 
	2*sizeof(reiserfs_objid_t)));

    /* Forming stat data item and body */
    item = (reiserfs_ih40_t *)(block->data + block->size) - 1;
    aal_memset(&item->key, 0, sizeof(reiserfs_key_t));
    
    set_key_type(&item->key, KEY_SD_MINOR);
    set_key_locality(&item->key, 41);
    set_key_objectid(&item->key, 42);
    
    ih40_set_plugin_id(item, 0x0);
    ih40_set_length(item, sizeof(reiserfs_stat40_base_t));
    ih40_set_offset(item, sizeof(reiserfs_nh40_t));
    
    stat_body = (reiserfs_stat40_base_t *)(block->data + ih40_get_offset(item));
    stat40_set_mode(stat_body, S_IFDIR | 0111);
    stat40_set_extmask(stat_body, 0);
    stat40_set_nlink(stat_body, 2);
    stat40_set_size(stat_body, 0);

    /* Forming direntry item and body */
    item = (reiserfs_ih40_t *)(block->data + block->size) - 2;
    aal_memset(&item->key, 0, sizeof(reiserfs_key_t));
    
    set_key_type(&item->key, KEY_FILE_NAME_MINOR);
    set_key_locality(&item->key, 42);
    
    ih40_set_plugin_id(item, 0x2);
    ih40_set_length(item, sizeof(reiserfs_direntry40_t) + 
	2*sizeof(reiserfs_direntry40_unit_t) + 2*sizeof(reiserfs_objid_t) + 
	2 + 3);
    
    ih40_set_offset(item, sizeof(reiserfs_nh40_t) + sizeof(reiserfs_stat40_base_t));
    
    direntry_body = (reiserfs_direntry40_t *)(block->data + ih40_get_offset(item));
    direntry_body->num_entries = 2;
   
    {
	reiserfs_key_t key;
	uint64_t objectid;
	uint64_t offset;
	
	aal_memset(&key, 0, sizeof(key));
	set_key_locality(&key, 42);
	set_key_type(&key, KEY_FILE_NAME_MINOR);
	
	objectid = get_key_objectid(&key);
	offset = get_key_offset(&key);
	
	aal_memcpy(direntry_body->entry[0].hash.objectid, &objectid, 8);
	aal_memcpy(direntry_body->entry[0].hash.offset, &offset, 8);
    }
    
    /* Offset of the first name in directory */
    direntry_body->entry[0].offset = 2*sizeof(reiserfs_direntry40_unit_t) + 
	sizeof(reiserfs_direntry40_t);
    
    {
	reiserfs_key_t key;
	uint64_t objectid;
	uint64_t offset;

	aal_memset(&key, 0, sizeof(key));
	set_key_locality(&key, 42);
	set_key_type(&key, KEY_FILE_NAME_MINOR);
	
	objectid = pack_string("..", 1);
	set_key_objectid(&key, objectid);
	set_key_offset(&key, 0ull);
	
	
	offset = get_key_offset(&key);
	
	aal_memcpy(direntry_body->entry[1].hash.objectid, &objectid, 8);
	aal_memcpy(direntry_body->entry[1].hash.offset, &offset, 8);
    }

    /* 
	Offset of the second name in directory.
	
	It is calculated as following: size of direntry header + 
	size of dir units array + key for first name + size of first 
	name (".") + zero for terminating first name.
    */
    direntry_body->entry[1].offset = 2*sizeof(reiserfs_direntry40_unit_t) + 
	2 + sizeof(reiserfs_objid_t) + sizeof(reiserfs_direntry40_t);

    {
	char *dirname = ((void *)direntry_body) + direntry_body->entry[0].offset + 
	    sizeof(reiserfs_objid_t);
	aal_memcpy(dirname, ".\0", 2);
    }

    dot_key = (reiserfs_objid_t *)(((void *)direntry_body) + 
	direntry_body->entry[0].offset);

    {
	reiserfs_key_t key;
	uint64_t locality;
	uint64_t objectid;

	aal_memset(&key, 0, sizeof(key));
	set_key_type(&key, KEY_SD_MINOR);
	set_key_locality(&key, 41);
	set_key_objectid(&key, 42);
	
	locality = get_key_locality(&key) << 4;
	objectid = get_key_objectid(&key);
	
	aal_memcpy(dot_key->locality, &locality, 8);
	aal_memcpy(dot_key->objectid, &objectid, 8);
    }
    
    {
	char *dirname = ((void *)direntry_body) + direntry_body->entry[1].offset + 
	    sizeof(reiserfs_objid_t);
	aal_memcpy(dirname, "..\0", 3);
    }
    
    dot_dot_key = (reiserfs_objid_t *)(((void *)direntry_body) + 
	direntry_body->entry[1].offset);
    
    {
	reiserfs_key_t key;
	uint64_t locality;
	uint64_t objectid;

	aal_memset(&key, 0, sizeof(key));
	set_key_type(&key, KEY_SD_MINOR);
	set_key_locality(&key, (41 - 3));
	set_key_objectid(&key, 41);

	locality = get_key_locality(&key) << 4;
	objectid = get_key_objectid(&key);
	
	aal_memcpy(dot_dot_key->locality, &locality, 8);
	aal_memcpy(dot_dot_key->objectid, &objectid, 8);

    }
    
    if (aal_device_write_block(fs->device, block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't write block %llu on device.", 
	    aal_device_get_block_nr(fs->device, block));
	aal_device_free_block(block);
	return 0;
    }
    aal_device_free_block(block);

    return root_blk;
}

