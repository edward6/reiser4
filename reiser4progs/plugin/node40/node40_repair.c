/*
    node40_repair.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "node40.h"

#define INVALID_U16	0xffff

extern errno_t node40_remove(reiser4_entity_t *entity, reiser4_pos_t *pos);
extern uint16_t node40_count(reiser4_entity_t *entity);
extern uint8_t node40_get_level(reiser4_entity_t *entity);

static uint16_t node40_get_offset_at(aal_block_t *block, int pos) {
    if (pos > nh40_get_num_items(nh40(block)))
	return 0;
    
    return nh40_get_num_items(nh40(block)) == pos ?
	nh40_get_free_space_start(nh40(block)) : 
	ih40_get_offset(node40_ih_at(block, pos));
}

static void node40_set_offset_at(aal_block_t *block, int pos, uint16_t offset) {
    if (pos > nh40_get_num_items(nh40(block)))
	return;
    
    if (nh40_get_num_items(nh40(block)) == pos) 
	nh40_set_free_space_start(nh40(block), offset);
    else 
	ih40_set_offset(node40_ih_at(block, pos), offset);
}

static int64_t __length_sum(item40_header_t *ih, uint16_t count) {
    aal_assert("vpf-215", ih != NULL, return 0);

    return count == 1 ? ih40_get_len(ih) : 
	ih40_get_len(ih) + __length_sum(ih - 1, count - 1);
}

static errno_t node40_region_fix_offsets(reiser4_entity_t *entity, uint16_t start_pos, 
    uint16_t end_pos) 
{
    int i;
    item40_header_t *ih;
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-195", node != NULL, return -1);
    aal_assert("vpf-196", node->block != NULL, return -1);
    aal_assert("vpf-217", start_pos < end_pos, return -1);
    aal_assert("vpf-218", start_pos > 0, return -1);
    aal_assert("vpf-219", end_pos <= node40_count(entity), return -1);

    ih = node40_ih_at(node->block, start_pos);
    for (i = start_pos; i < end_pos; i++, ih--) {
	if (ih40_get_offset(ih + 1) + ih40_get_len(ih + 1) != 
	    ih40_get_offset(ih)) 
	{
	    aal_exception_error("Node (%llu): the item (%d) offset was fixed "
		"to (%u).", aal_block_number(node->block), i, 
		ih40_get_offset(ih + 1) + ih40_get_len(ih + 1));
	    ih40_set_offset(ih, ih40_get_offset(ih + 1) + ih40_get_len(ih + 1));
	}
    }
    
    return 0;
}

static errno_t node40_region_delete(reiser4_entity_t *entity, uint16_t start_pos, 
    uint16_t end_pos) 
{
    int i;
    item40_header_t *ih;
    reiser4_pos_t pos;
    node40_t *node = (node40_t *)entity;
     
    aal_assert("vpf-201", node != NULL, return -1);
    aal_assert("vpf-202", node->block != NULL, return -1);
    aal_assert("vpf-213", start_pos <= end_pos, return -1);
    aal_assert("vpf-214", end_pos <= node40_count(entity), return -1);
    
    ih = node40_ih_at(node->block, start_pos);
    for (i = start_pos; i <= end_pos; i++, ih--) {
	if (i != end_pos || i == node40_count(entity)) {
	    node40_set_offset_at(node->block, i, ih40_get_offset(ih + 1) + 1);
	    ih40_set_len(ih + 1, 1);
	} else {
	    ih40_set_len(ih + 1, node40_get_offset_at(node->block, i) - 
		node40_get_offset_at(node->block, i - 1));
	}
    }

    pos.unit = ~0ul;
    pos.item = start_pos - 1;
    for (i = start_pos - 1; i < end_pos; i++) {
	if (node40_remove(entity, &pos)) {
	    aal_exception_bug("Node (%llu): Failed to delete the item (%d) "
		"of a region (%d-%d).", aal_block_number(node->block), 
		i - start_pos + 1, start_pos, end_pos);
	    return -1;
	}
    }
    
    return 0;    
}

static errno_t node40_region_check(reiser4_entity_t *entity, uint16_t start_pos, 
    uint16_t end_pos) 
{
    int count, i, j, inval_len = 0, inval_off = 0;
    uint64_t items_num, max_len, sum_length = 0;
    item40_header_t *ih;
    node40_t *node = (node40_t *)entity;

    aal_assert("vpf-203", node != NULL, return -1);
    aal_assert("vpf-204", node->block != NULL, return -1);

    items_num = node40_count(entity);
    
    aal_assert("vpf-205", end_pos <= items_num, return -1);
    aal_assert("vpf-206", start_pos <= items_num, return -1);
    aal_assert("vpf-207", start_pos > 0, return -1);

    for (count = end_pos - start_pos + 1; count > 0; count--) {
	for (i = start_pos - 1; i <= end_pos - count; i++) {
	    if (__length_sum(node40_ih_at(node->block, i), count) == 
		node40_get_offset_at(node->block, i + count) - 
		node40_get_offset_at(node->block, i)) 	    
		return node40_region_fix_offsets(entity, i + 1, i + count);
	}
    }

    /* 
	Some lengths between start_pos and end_pos are corrupted or the region 
	is not right-bounded by a reliable position. Check if the sum of 
	lengths	is valid or the set of all offsets are in increasing order.
    */
    max_len = node40_free_space_end(node->block) - sizeof(node40_header_t);
    sum_length = node40_get_offset_at(node->block, start_pos - 1) - 
	sizeof(node40_header_t);
    ih = node40_ih_at(node->block, start_pos);
    for (i = start_pos; i <= end_pos; i++, ih--) {	
	sum_length += ih40_get_len(ih + 1);
	inval_off = inval_len = 0;
	
	if (sum_length > max_len)
	    inval_len = 1;

	if (node40_get_offset_at(node->block, i - 1) >= 
	    node40_get_offset_at(node->block, i) || 
	    node40_get_offset_at(node->block, i) == INVALID_U16)
	    inval_off = 1;

	if ((inval_len && inval_off) || (!inval_len && !inval_off)) {
	    return node40_region_delete(entity, i, end_pos);
	} else if (inval_len) {
	    aal_exception_error("Node (%llu): the item (%d) length was fixed "
		"to (%d).", aal_block_number(node->block), i - 1, 
		node40_get_offset_at(node->block, i) - 
		node40_get_offset_at(node->block, i - 1));
	    sum_length -= ih40_get_len(ih + 1);
	    ih40_set_len(ih + 1, node40_get_offset_at(node->block, i) - 
		node40_get_offset_at(node->block, i - 1));
	    sum_length += ih40_get_len(ih + 1);
	} else {
	    if (i == node40_count(entity))
		aal_exception_error("Node (%llu): the start of the free space "
		    "was fixed to (%d).", aal_block_number(node->block), 
		    node40_get_offset_at(node->block, i - 1) + 
		    ih40_get_len(ih + 1));
	    else 
		aal_exception_error("Node (%llu): the item (%d) offset was fixed "
		    "to (%d).", aal_block_number(node->block), i, 
		    node40_get_offset_at(node->block, i - 1) + 
		    ih40_get_len(ih + 1));
	    node40_set_offset_at(node->block, i, node40_get_offset_at(node->block, 
		i - 1) + ih40_get_len(ih + 1));
	}
    }
/*  
    // The previous simple and reliable algorithm. 

    if (__length_sum(node40_ih_at(node->block, start_pos - 1), 
	end_pos - start_pos + 1) > max_len)
	inval_len = 1;

    for (i = start_pos; i <= end_pos; i++) {
	if (node40_get_offset_at(node->block, i - 1) >= 
	    node40_get_offset_at(node->block, i)) 
	{
	    inval_off = 1;
	    break;
	}
    }
    
    if (node40_get_offset_at(node->block, end_pos) == INVALID_U16)
	inval_off = 1;

    if ((inval_len && inval_off) || (!inval_len && !inval_off)) {
	return node40_region_delete(node, start_pos, end_pos);
    } else if (inval_len) {
	// Fix all lengths according to offsets. 	
	for (i = start_pos; i <= end_pos; i++) {
	    aal_exception_error("Node (%llu): the item (%d) length was fixed "
		"to (%d).", aal_block_number(node->block), i - 1, 
		node40_get_offset_at(node->block, i) - 
		node40_get_offset_at(node->block, i - 1));
	    ih40_set_len(node40_ih_at(node->block, i - 1), 
		node40_get_offset_at(node->block, i) - 
		node40_get_offset_at(node->block, i - 1));
	}
    } else {
	// Fix all offsets according to lengths. 
	ih = node40_ih_at(node->block, start_pos);
	for (i = start_pos; i <= end_pos; i++, ih--) {
	    aal_exception_error("Node (%llu): the item (%d) offset was fixed "
		"to (%d).", aal_block_number(node->block), i, 
		node40_get_offset_at(node->block, i - 1) + 
		ih40_get_len(ih + 1));
	    node40_set_offset_at(node->block, i, node40_get_offset_at(node->block, i - 1) + 
		ih40_get_len(ih + 1));
	}
    }
*/
    return 0;
}


/* 
    Checks lengths and offsets of item_heads.
    If a corrupted region found returns 1 and set *start_pos at the first 
    dubious position, *end_pos at the first reliable position.
    Otherwise, returns 0 and set *start_pos at 0 and *end_pos at count of item.
*/
static int node40_region_find_bad(reiser4_entity_t *entity, uint16_t *start_pos, 
    uint16_t *end_pos) 
{
    int i;
    uint64_t offset, prev_offset, free_end, max_len;
    item40_header_t *ih;
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-208", node != NULL, return -1);
    aal_assert("vpf-209", node->block != NULL, return -1);
    aal_assert("vpf-210", start_pos != NULL, return -1);
    aal_assert("vpf-211", end_pos != NULL, return -1);

    *start_pos = 0;
    *end_pos = node40_count(entity);
    ih = node40_ih_at(node->block, 0);
    prev_offset = ih40_get_offset(ih);
    
    free_end = node40_free_space_end(node->block);
    max_len = free_end - sizeof(node40_header_t);

    for(i = 1, ih--; i <= node40_count(entity); i++, ih--) {
	offset = node40_get_offset_at(node->block, i);
	
	/* Check if the previous item length is invalid. */
	if (ih40_get_len(ih + 1) > max_len) {
	    aal_exception_error("Node (%llu): the length (%d) of the item (%d) "
		"is invalid.", aal_block_number(node->block), 
		ih40_get_len(ih + 1), i - 1);
	    ih40_set_len(ih + 1, INVALID_U16);
	}
	
	/* Check if the item offset is invalid. */
	if (offset < sizeof(node40_header_t) || offset > free_end) {
	    if (i == node40_count(entity))
		aal_exception_error("Node (%llu): the start of the free space (%llu) "
		    "is invalid.", aal_block_number(node->block), offset);
	    else
		aal_exception_error("Node (%llu): the offset (%llu) of the item (%d) "
		    "is invalid.", aal_block_number(node->block), offset, i);
	    node40_set_offset_at(node->block, i, INVALID_U16);
	    offset = INVALID_U16;
	}
    	
	if (!(*start_pos)) {
	    /* Looking for the start of the corrupted region. */
	    if (prev_offset + ih40_get_len(ih + 1) != offset)
		*start_pos = i;
	} else {
	    /* Looking for the end of the corrupted region. */
	    /* 
		Left reliable offset should be less then the right reliable at 
		least on 'number of items betreen them' bytes.
	    */
	    if ((prev_offset + ih40_get_len(ih + 1) == offset) && 
		(node40_get_offset_at(node->block, *start_pos - 1) < offset -
		 (i - *start_pos + 1))) 
	    {
		*end_pos = i - 1;
		return 1;
	    }
	}
	prev_offset = offset;
    }

    return *start_pos ? 1 : 0;
}

/* 
    Checks the set of items within the node. Recovers items lengths, offsets, 
    free space. 
*/
static errno_t node40_item_array_check(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;
    item40_header_t *ih;
    uint16_t start_pos, end_pos, free_space;
    int retval;

    aal_assert("vpf-197", node != NULL, return -1);
    aal_assert("vpf-198", node->block != NULL, return -1);

    ih = node40_ih_at(node->block, 0);
    if (ih40_get_offset(ih) != sizeof(node40_header_t)) {
	aal_exception_error("Node (%llu): item (0) has a wrong offset (%d), "
	    "fixed to (%d).", aal_block_number(node->block), 
	    ih40_get_offset(ih), sizeof(node40_header_t));
	ih40_set_offset(ih, sizeof(node40_header_t));
    }   
    
    /* 
	FIXME-VITALY: it is possible to fix the start_free_space on the base of 
	free_space also, not only vice versa.
    */
    
    while (1) {
	if ((retval = node40_region_find_bad(entity, &start_pos, &end_pos)) == -1)
	{
	    aal_exception_bug("Node (%llu): failed to find a corrupted "
		"region.", aal_block_number(node->block));
	    return -1;
	} else if (retval > 0) {
	    /* Corrupted region was found, try to fix it. */
	    if (node40_region_check(entity, start_pos, end_pos)) {
		aal_exception_bug("Failed to delete a region: block (%llu), "
		    "start (%u), end (%u).", aal_block_number(node->block), 
		    start_pos, end_pos);
		return -1;
	    }
	} else if (start_pos == 0 && end_pos == 0) {
	    /* If no items left, return -1 */
	    return -1;
	} else 
	    break;
    }
  
    free_space = node40_free_space_end(node->block) - 
	nh40_get_free_space_start(nh40(node->block));
 
    if (nh40_get_free_space(nh40(node->block)) != free_space) {	
	aal_exception_error("Node (%llu): free space (%u) is not equal to the "
	    "left after item lengths summation, fixed to (%u).", 
	    aal_block_number(node->block), 
	    nh40_get_free_space(nh40(node->block)), free_space);
	nh40_set_free_space(nh40(node->block), free_space);
    }
	
    return 0;
}

static errno_t node40_item_count_check(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;

    aal_assert("vpf-199", node != NULL, return -1);
    aal_assert("vpf-200", node->block != NULL, return -1);
    aal_assert("vpf-247", node->block->device != NULL, return -1);

    if (node40_count(entity) > 
	(aal_device_get_bs(node->block->device) - sizeof(node40_header_t)) / 
	(sizeof(item40_header_t) + 1)) 
    {
	aal_exception_error("Node (%llu): number of items (%d) exceeds the "
	    "limit.", aal_block_number(node->block), node40_count(entity));
	return -1;
    }
    return 0;
}

static errno_t node40_corrupt(reiser4_entity_t *entity, uint16_t options) {
    int i;
    item40_header_t *ih;
    node40_t *node = (node40_t *)entity;
    
    for(i = 0, ih = node40_ih_at(node->block, 0); 
	i < 2 * node40_count(entity) + 1; i++, ih--) 
    {
	if (aal_test_bit(i, &options)) {
	    if (i < node40_count(entity))
		ih40_set_len(ih, INVALID_U16);
	    else
		node40_set_offset_at(node->block, i - node40_count(entity), 
		    INVALID_U16);
	}
    }
    return 0;
}

errno_t node40_check(reiser4_entity_t *entity, uint16_t options) {
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-194", node != NULL, return -1);
 
    /* Check the count of items */
    if (node40_item_count_check(entity))
	return -1;

    /* Check the item array and free space. */
    if (node40_item_array_check(entity))
	return -1;

    return 0;    
}

/* 
    This checks the level constrains like no internal and extent items 
    at leaf level or no statdata items at internal level.
*/
errno_t node40_item_legal(reiser4_entity_t *entity, reiser4_plugin_t *plugin) {
    node40_t *node = (node40_t *)entity;
    uint16_t level;

    aal_assert("vpf-225", node != NULL, return -1);
    aal_assert("vpf-237", plugin != NULL, return -1);
    
    level = node40_get_level(entity);
    
    if (plugin->item_ops.group == INTERNAL_ITEM_GROUP) {
	if (level == NODE40_LEAF)
	    return 0;
    } else if (plugin->item_ops.group == EXTENT_ITEM_GROUP) {
	if (level != NODE40_TWIG)
	    return 0;
    } else if (level != NODE40_LEAF) 
	return 0;
    

    return 1;
}

