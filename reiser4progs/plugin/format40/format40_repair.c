/*
    format40_repair.c -- repair methods for the default disk-layout plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

/* Remove it when exception will know how to get an open answer. */
#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>
#include <comm/misc.h>

#include "format40.h"

static long int __get_number(int *error, char *ask, ...) __check_format__(printf, 2, 3);
static long int __get_number(int *error, char *ask, ...) {
    char *answer = NULL;
    int n = 0;
    long int result;
    char buf[4096];
   
    va_list arg_list;

    aal_memset(buf, 0, 4096);
    va_start(arg_list, ask);
    aal_vsnprintf(buf, 4096, ask, arg_list);
    va_end(arg_list);
    
    while (1) {
	result = 0;
	*error = 0;
	fprintf(stderr, buf);
	getline (&answer, &n, stdin);
	if (!aal_strncmp(answer, "\n", 1)) {
	    *error = 1;
	    break;
	} else if (!(result = reiser4_comm_strtol(answer, error)) && *error) {
	    aal_exception_error("Invalid answer (%ld).", result);
	    free(answer);
	}
    }

    free(answer);
    return result;
}

errno_t format40_check(reiser4_entity_t *entity, uint16_t options) {
    format40_super_t *super;
    format40_t *format = (format40_t *)entity;
    
    char *answer = NULL;
    long int result;
    int error, n = 0;
    
    aal_assert("vpf-160", entity != NULL, return -1);
    
    super = format40_super(format->block);
    
    /* Check the fs size. */
    if (aal_device_len(format->device) != get_sb_block_count(super)) {
	result = aal_device_len(format->device);
	    
	if (aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_YES|EXCEPTION_NO, 
	    "Number of blocks found in the superblock is not equal to the size of the "
	    "partition.\nHave you used resizer?") == EXCEPTION_NO) 
	{
	    aal_exception_error("Invalid size (%llu) found in the superblock. Fixed "
		"to (%llu)", get_sb_block_count(super), aal_device_len(format->device));
	} else {
	    while (1) {
		if (!(result = __get_number(&error, "Enter the number of blocks on your "
		    "partition [%llu]: ", aal_device_len(format->device))) && error == 1)	
		{
		    result = aal_device_len(format->device);
		    break;
		} else if ((uint64_t)result > aal_device_len(format->device)) {
		    aal_exception_error("Specified number of blocks (%ld) is greater then "
			"the partition size (%lld).", result, aal_device_len(format->device));
		} else 
		    break;
	    }
	}
	set_sb_block_count(super, result);
    }

    /* Check the free block count. */
    if (get_sb_free_blocks(super) > get_sb_block_count(super)) 
	aal_exception_error("Invalid free block count (%llu) found in the superblock. Zeroed.", 
	    get_sb_free_blocks(super));
    
    /* Check the root block number. */
    if (get_sb_root_block(super) > get_sb_block_count(super)) 
	aal_exception_error("Invalid root block (%llu) found in the superblock. Zeroed.", 
	    get_sb_root_block(super));
    
    /* Some extra check for root block? */    

    /* Check the drop policy. */
    if (get_sb_drop_policy(super) >= DROP_LAST_ID) {
	aal_exception_error("Invalid drop policy found in the superblock.");
	while (1) {
	    if (!(result = __get_number(&error, "Enter the preferable drop policy "
		"(0-%u)[0]: ", DROP_LAST_ID - 1)) && error == 1) 
	    {
		result = 0;
		break;
	    } else if (result >= DROP_LAST_ID) {
		aal_exception_error("Invalid drop policy was specified (%ld)", result);
	    } else 
		break;
	}
    }
    return 0;
}

void format40_print(char *buf, size_t n, reiser4_entity_t *entity, uint16_t options) 
{
    format40_super_t *super;
    
    aal_assert("vpf-160", entity != NULL, return);
    
    if (!buf)
	return;

    super = format40_super(((format40_t *)entity)->block);
    
    reiser4_comm_strcat(buf, n, "Format40 on-disk format\n");
    reiser4_comm_strcat(buf, n, "Count of blocks free/all: (%llu)/(%llu)\n", 
	get_sb_free_blocks(super), get_sb_block_count(super));
    reiser4_comm_strcat(buf, n, "Root block (%llu)\n", get_sb_root_block(super));
    reiser4_comm_strcat(buf, n, "Drop policy (%u)\n", get_sb_drop_policy(super));
    reiser4_comm_strcat(buf, n, "Oid (%llu)\n", get_sb_oid(super));
    reiser4_comm_strcat(buf, n, "File count (%llu)\n", get_sb_file_count(super));
    reiser4_comm_strcat(buf, n, "Flushes (%llu)\n", get_sb_flushes(super));
    reiser4_comm_strcat(buf, n, "Magic (%s)\n", super->sb_magic);
    reiser4_comm_strcat(buf, n, "Tree height (%u)\n", get_sb_tree_height(super));
}

