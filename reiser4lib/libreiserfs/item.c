/*
    item.c -- reiserfs item api.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

reiserfs_item_t *reiserfs_item_open(reiserfs_node_t *node, uint16_t pos) {
    reiserfs_item_t *info;
    reiserfs_plugin_id_t plugin_id;
    void * item;

    if (!(info = aal_calloc(sizeof(*info), 0)))
	return NULL;
    
    reiserfs_check_method(node->plugin->node, item_length, goto free_info);
    info->length = node->plugin->node.item_length(node->entity, pos); 
    
    reiserfs_check_method(node->plugin->node, item_min_key, goto free_info); 
    if (!(info->key = node->plugin->node.item_min_key(node->entity, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't get key of the item (%u) in node (%llu).",
	    pos, aal_device_get_block_nr(node->device, node->block));
	goto free_info;
    }

    reiserfs_check_method(node->plugin->node, item_plugin_id, goto free_info); 
    plugin_id = node->plugin->node.item_plugin_id(node->entity, pos);
    
    if (!(info->plugin = reiserfs_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin by its id %x.", plugin_id);
	    goto free_info;
    }
    
    reiserfs_check_method(node->plugin->node, item, goto free_info);
    reiserfs_check_method(info->plugin->item.common, open, goto free_info);
    
    if (!(info->entity = info->plugin->item.common.open(node->plugin->node.item))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open item (%u) in node (%llu).",
	    pos, aal_device_get_block_nr(node->device, node->block));
	goto free_info;
    }

    return info;

free_info:
    aal_free(info);
    return NULL;
}


blk_t reiserfs_item_down_link(reiserfs_item_t *item, uint16_t unit_pos) {
    aal_assert("vpf-041", item != NULL, return 0);

    reiserfs_check_method(item->plugin->item.specific.internal, down_link, return 0);
    return item->plugin->item.specific.internal.down_link(item->entity, unit_pos);
}

int reiserfs_item_is_internal (reiserfs_item_t * item) {
    aal_assert("vpf-042", item != NULL, return 0);

    reiserfs_check_method(item->plugin->item.common, is_internal, return 0);
    return item->plugin->item.common.is_internal();
}


