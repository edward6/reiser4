/*
    item.c -- reiserfs item api.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

reiserfs_item_t *reiserfs_item_open(reiserfs_coord_t *coord) {
    reiserfs_plugin_id_t plugin_id;
    reiserfs_item_t *item;
    
    aal_assert("vpf-046", coord != NULL, return NULL);
    
    if (!(item = aal_calloc(sizeof(*item), 0)))
       return NULL;

    item->coord = coord;

    if (!(plugin_id = reiserfs_node_get_item_plugin_id (coord->node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't get plugin id of the item (%u) in the node (%llu).", coord->item_pos, 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	goto free_item;
    }    

    if (!(item->plugin = reiserfs_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin by its id %x.", plugin_id);
	goto free_item;
    }
    
    if (item->plugin->item.common.open != NULL) {
	if (item->plugin->item.common.open(item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't open item (%u) in node (%llu).",	coord->item_pos, 
		aal_device_get_block_nr(coord->node->device, coord->node->block));
	    goto free_item;
	}
    }
    
    return item;

free_item:
    aal_free(item);
    return NULL;
}

error_t reiserfs_item_init(reiserfs_item_t *item, reiserfs_coord_t *coord) {
    reiserfs_plugin_id_t plugin_id;

    aal_assert("vpf-045", item != NULL, return -1);
    aal_assert("vpf-046", coord != NULL, return -1);

    if (item->entity) {
	/* close item first */
	if (!item->plugin) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't close item (%u) in the node (%llu).", item->coord->item_pos, 
		aal_device_get_block_nr(item->coord->node->device, 
		item->coord->node->block));
	    return -1;
	}
	
	if (item->plugin->item.common.close != NULL) {
	    if (item->plugin->item.common.close (item)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't close item (%u) in node (%llu).", item->coord->item_pos, 
		    aal_device_get_block_nr(item->coord->node->device, 
		    item->coord->node->block));
		return -1;
	    }
	}
    }
    
    item->coord = coord;

    if (!(plugin_id = reiserfs_node_get_item_plugin_id (coord->node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't get plugin id of the item (%u) in the node (%llu).", coord->item_pos, 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }
    
    if (!(item->plugin = reiserfs_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin by its id %x.", plugin_id);
	return -1;
    }
    
    if (item->plugin->item.common.open != NULL) {
	if (item->plugin->item.common.open(item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't open item (%u) in node (%llu).",	coord->item_pos, 
		aal_device_get_block_nr(coord->node->device, coord->node->block));
	    return -1;
	}
    }
    
    return 0;
}


blk_t reiserfs_item_down_link(reiserfs_item_t *item) {
    aal_assert("vpf-041", item != NULL, return 0);

    if (item->plugin->item.specific.internal.down_link == NULL)
	return 0;
    
    return item->plugin->item.specific.internal.down_link(item);
}

int reiserfs_item_is_internal (reiserfs_item_t * item) {
    aal_assert("vpf-042", item != NULL, return 0);

    reiserfs_check_method(item->plugin->item.common, is_internal, return 0);
    return item->plugin->item.common.is_internal();
}

int reiserfs_item_estimate (reiserfs_coord_t *coord, reiserfs_item_info_t *item_info, 
    reiserfs_plugin_id_t id) 
{
    reiserfs_plugin_id_t plugin_id;

    if (item_info->plugin == NULL) {
	if (coord == NULL || coord->unit_pos == -1) 
	    plugin_id = id;
	else {
	    if (!(plugin_id = reiserfs_node_get_item_plugin_id (coord->node, 
		coord->item_pos))) 
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't get plugin id of the item (%u) in the node (%llu).", 
		    coord->item_pos, 
		    aal_device_get_block_nr(coord->node->device, coord->node->block));
		return -1;
	    }	    
	}
	
	if (!(item_info->plugin = reiserfs_plugins_find_by_coords
		(REISERFS_ITEM_PLUGIN, plugin_id))) 
	{	
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't find internal item plugin by its identifier %x.", plugin_id);
	    return -1;
	}
    }

    reiserfs_check_method (item_info->plugin->item.common, estimate, return -1);    

    item_info->plugin->item.common.estimate(coord, item_info);  
    
    return 0;
}
