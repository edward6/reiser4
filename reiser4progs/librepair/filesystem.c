/*
    librepair/filesystem.c - methods are needed mostly by fsck for work 
    with broken filesystems.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <repair/librepair.h>
#include <reiser4/reiser4.h>

static reiser4_node_t *__node_open(aal_block_t *block, void *data) {
    return reiser4_node_open(block);
}

static errno_t __before_traverse(reiser4_node_t *node, void *data) {
    repair_check_t *check_data = data;
    
    aal_assert("vpf-251", node != NULL, return -1);
    aal_assert("vpf-253", check_data != NULL, return -1);
    aal_assert("vpf-254", check_data->format != NULL, return -1);

    if (!check_data->level)
	check_data->level = (node->entity->plugin->node_ops.get_level ?
	    node->entity->plugin->node_ops.get_level(node->entity) : 
	    reiser4_format_get_height(check_data->format));
    
    check_data->level--;

    return 0;
}

static errno_t __after_traverse(reiser4_node_t *node, void *data) {
    repair_check_t *check_data = data;
    
    aal_assert("vpf-256", check_data != NULL, return -1);
    
    check_data->level++;
    
    return 0;
}

static errno_t __setup_traverse(reiser4_node_t *node, reiser4_item_t *item, 
    void *data)
{
    repair_check_t *check_data = data;
    
    aal_assert("vpf-255", check_data != NULL, return -1);

    reiser4_node_lkey(node, &check_data->ld_key);
    reiser4_node_rkey(node, &check_data->rd_key);
    return 0;
}

static errno_t __update_traverse(reiser4_node_t *node, reiser4_item_t *item, 
    void *data) 
{
    repair_check_t *check_data = data;
    
    aal_assert("vpf-257", check_data != NULL, return -1);
    
    if (repair_test_flag(check_data, REPAIR_NOT_FIXED)) {
	/* The node corruption was not fixed - delete the internal item. */
	if (reiser4_node_remove(node, item->pos)) {
	    if (item->pos->unit == ~0ul)
		aal_exception_error("Node (%llu): Failed to remove the item "
		    "(%u).", aal_block_number(node->block), item->pos->item);
	    else
		aal_exception_error("Node (%llu): Failed to remove the unit "
		    "(%u) from  item (%u).", aal_block_number(node->block), 
		    item->pos->item, item->pos->unit);
	    return -1;
	}
	if (item->pos->unit == ~0ul) 
	    item->pos->item--;
	else 
	    item->pos->unit--;	
    }
    
    return 0;
}

static errno_t __node_check(reiser4_node_t *node, void *data) {
    repair_check_t *check_data = data;
    errno_t res;
    
    aal_assert("vpf-252", check_data != NULL, return -1);
    
    if ((res = repair_node_check(node, check_data)) > 0) {
	repair_set_flag(check_data, REPAIR_NOT_FIXED);
	return 0;
    } else {
	return res;
    }
    
    return 0;
}

static errno_t repair_fs_check_setup(reiser4_fs_t *fs, repair_check_t *data) {
    /* Prepare a control allocator */
    if (!(data->a_control = reiser4_alloc_create(fs->format, 
	reiser4_format_get_len(fs->format)))) 
    {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, 
	    "Failed to create a control allocator.");
	return -1;
    }

    /* Mark the format area as used in the control allocator */
    reiser4_format_mark(fs->format, data->a_control);

    data->format = fs->format;
    data->options = repair_data(fs)->options;
    
    /* 
	Set the level to 0 for the root block. Rely on the root level more 
	then on super block height.
    */
    data->level = 0;

    /* FIXME-VITALY: Hardcoded key plugin id */
    if (!(data->ld_key.plugin = 
	libreiser4_factory_ifind(KEY_PLUGIN_TYPE, KEY_REISER40_ID)))
    {
	aal_exception_error(
	    "Can't find key plugin by its id 0x%x.", KEY_REISER40_ID);
	return -1;
    }
    
    data->rd_key.plugin = data->ld_key.plugin;
    
    reiser4_key_minimal(&data->ld_key);
    reiser4_key_maximal(&data->rd_key);
    
    return 0;
}

errno_t repair_fs_check(reiser4_fs_t *fs) {
    repair_check_t data;
    blk_t blk;
    aal_block_t *block;
    int res;    

    aal_assert("vpf-180", fs != NULL, return -1);
    aal_assert("vpf-181", fs->format != NULL, return -1);
    aal_assert("vpf-182", fs->format->device != NULL, return -1);

    aal_memset(&data, 0, sizeof(data));
    
    blk = reiser4_format_get_root(fs->format);
    if ((res = reiser4_format_layout(fs->format, callback_data_block_check, &blk)) &&
	res != -1) 
    {
	aal_exception_error("Bad root block (%llu). (A previous recovery did "
	    "not complete probably).", blk);
	return 0;
    }
 
    if (!(block = aal_block_open(fs->format->device, blk))) {
	aal_exception_error(
	    "Can't read block %llu. %s.", blk, fs->format->device->error);
	return -1;
    }
    
    if (repair_fs_check_setup(fs, &data))
	return -1;    
    
    reiser4_node_traverse(block, __node_open, __node_check, __before_traverse, 
	__setup_traverse, __update_traverse, __after_traverse, &data);

    return 0;
}

static errno_t repair_master_check(reiser4_fs_t *fs, 
    callback_ask_user_t ask_blocksize) 
{
    uint16_t blocksize = 0;
    int error = 0;
    int free_master = 0;
    reiser4_plugin_t *plugin;
   
    aal_assert("vpf-161", fs != NULL, return -1);
    aal_assert("vpf-163", ask_blocksize != NULL, return -1);
    aal_assert("vpf-164", repair_data(fs) != NULL, return -1);
    aal_assert("vpf-170", repair_data(fs)->host_device != NULL, return -1);
    
    if (!fs->master) {
	/* Master SB was not opened. Create a new one. */
	if (aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_YESNO, 
	    "Master super block cannot be found. Do you want to build a new "
	    "one on (%s)?", aal_device_name(repair_data(fs)->host_device)) == 
	    EXCEPTION_NO) 
	    return -1;

        if (!(blocksize = ask_blocksize(fs, &error)) && error)
	    return -1;

	/* 
	    FIXME-VITALY: What should be done here with uuid and label? 
	    At least not here as uiud and label seem to be on the wrong place.
	    Move them to specific SB.
	*/
	
	/* Create a new master SB. */
	if (!(fs->master = reiser4_master_create(repair_data(fs)->host_device, 
	    INVALID_PLUGIN_ID, blocksize, NULL, NULL))) 
	{
	    aal_exception_fatal("Cannot create a new master super block.");
	    return -1;
	} else if (repair_verbose(repair_data(fs)))
	    aal_exception_info("A new master superblock was created on (%s).", 
		aal_device_name(repair_data(fs)->host_device));
	free_master = 1;
    } else {
	/* Master SB was opened. Check it for valideness. */

	/* Check the blocksize. */
	if (!aal_pow_of_two(reiser4_master_blocksize(fs->master))) {
	    aal_exception_fatal("Invalid blocksize found in the master super "
		"block (%u).", reiser4_master_blocksize(fs->master));
	    
	    if (!(blocksize = ask_blocksize(fs, &error)) && error)
		return -1;
	}	
    }

    /* Setting actual used block size from master super block */
    if (blocksize && aal_device_set_bs(repair_data(fs)->host_device, 
	reiser4_master_blocksize(fs->master))) 
    {
        aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	   "Invalid block size was specified (%u). It must be power of two.", 
	    reiser4_master_blocksize(fs->master));
	return -1;
    }
    
    return 0;
}    

static errno_t repair_alloc_check(reiser4_fs_t *fs) {
    return 0;
}

static errno_t repair_oid_check(reiser4_fs_t *fs) {
    return 0;
}

reiser4_fs_t *repair_fs_open(repair_data_t *data, 
    callback_ask_user_t ask_blocksize) 
{
    reiser4_fs_t *fs;
    void *oid_area_start, *oid_area_end;

    aal_assert("vpf-159", data != NULL, return NULL);
    aal_assert("vpf-172", data->host_device != NULL, return NULL);
    
    /* Allocating memory and initializing fields */
    if (!(fs = aal_calloc(sizeof(*fs), 0)))
	return NULL;
    
    fs->data = data;
    
    /* Try to open master and rebuild if needed. */
    fs->master = reiser4_master_open(data->host_device);
	
    /* Check opened master or build a new one. */
    if (repair_master_check(fs, ask_blocksize))
	goto error_free_master;
    
    /* Try to open the disk format. */
    fs->format = reiser4_format_open(data->host_device, 
	reiser4_master_format(fs->master));
    
    /* Check the opened disk format or rebuild it if needed. */
    if (repair_format_check(fs))
	goto error_free_format;
    
    fs->alloc = reiser4_alloc_open(fs->format, 
	reiser4_format_get_len(fs->format));
    
    if (repair_alloc_check(fs))
	goto error_free_alloc;

    /* Initializes oid allocator */
    fs->oid = reiser4_oid_open(fs->format);
  
    if (repair_oid_check(fs))
	goto error_free_oid;
    
    return fs;
    
error_free_oid:
    if (fs->oid)
	reiser4_oid_close(fs->oid);
error_free_alloc:
    if (fs->alloc)
	reiser4_alloc_close(fs->alloc);
error_free_format:
    if (fs->format)
	reiser4_format_close(fs->format);
error_free_master:
    if (fs->master)
	reiser4_master_close(fs->master);
error_free_fs:
    aal_free(fs);
error:
    return NULL;
}

errno_t repair_fs_sync(reiser4_fs_t *fs) 
{
    aal_assert("vpf-173", fs != NULL, return -1);
    
    /* Synchronizing block allocator */
    if (reiser4_alloc_sync(fs->alloc))
	return -1;
    
    /* Synchronizing the object allocator */
    if (reiser4_oid_sync(fs->oid))
	return -1;

    /* Synchronizing the disk format */
    if (reiser4_format_sync(fs->format))
	return -1;

    if (reiser4_master_confirm(fs->format->device)) {
	if (reiser4_master_sync(fs->master))
	    return -1;
    }
    
 return 0;
}

/* 
    Closes all filesystem's entities. Calls plugins' "done" routine for every 
    plugin and frees all assosiated memory. 
*/
void repair_fs_close(reiser4_fs_t *fs) 
{
    aal_assert("vpf-174", fs != NULL, return);

    reiser4_oid_close(fs->oid);

    reiser4_alloc_close(fs->alloc);
    reiser4_format_close(fs->format);
    reiser4_master_close(fs->master);

    /* Freeing memory occupied by fs instance */
    aal_free(fs);
}

