/*
    item.c -- reiserfs item api.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

blk_t reiserfs_item_down_link(reiserfs_item_t *item) {
    aal_assert("vpf-041", item != NULL, return 0);

    reiserfs_check_method(item->plugin->item.specific.internal, down_link, return 0);
    return item->plugin->item.specific.internal.down_link(item_info->data, coord->unit_pos);
}

/*int reiserfs_item_down_link (reiserfs_item_t * item) {
    aal_assert("vpf-042", item != NULL, return 0);

    reiserfs_check_method(item->plugin->item.common, is_internal, return 0);
    return item->plugin->item.common.is_internal(item_info->data, coord->unit_pos);
}*/


