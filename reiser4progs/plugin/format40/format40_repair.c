/*
    format40_repair.c -- repair methods for the default disk-layout plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

/* Remove it when exception will know how to get an open answer. */
#define _GNU_SOURCE

#include <aux/aux.h>
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
	} else if (!(result = reiser4_aux_strtol(answer, error)) && *error) {
	    aal_exception_error("Invalid answer (%ld).", result);
	    free(answer);
	}
    }

    free(answer);
    return result;
}

static int callback_len_check(int64_t len, void *data) {
    if (len > *(int64_t *)data) {
	aal_exception_error("Invalid partition size was specified (%lld)", 
	    *(int64_t *)data);
	
	return 0;
    }
    
    return 1;
}

static int callback_tail_check(int64_t tail, void *data) {
    if (tail >= *(int *)data) {
	aal_exception_error("Invalid tail policy was specified (%ld)", 
	    *(int *)data);
	
	return 0;
    }
    
    return 1;
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
	    
	if (aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_YESNO, 
	    "Number of blocks found in the superblock (%llu) is not equal to "
	    "the size of the partition (%llu).\nHave you used resizer?", 
	    get_sb_block_count(super), aal_device_len(format->device)) == 
	    EXCEPTION_NO) 
	{
	    aal_exception_error("Size of the partition was fixed to (%llu).", 
		aal_device_len(format->device));
	} else {
	    uint64_t len = aal_device_len(format->device);
		
	    result = aal_ui_get_numeric(aal_device_len(format->device), 
		callback_len_check, &len, "Enter the number of blocks on your "
		"partition");
	}
	set_sb_block_count(super, result);
    }

    /* Check the free block count. */
    if (get_sb_free_blocks(super) > get_sb_block_count(super)) {
	aal_exception_error("Invalid free block count (%llu) found in the "
	    "superblock. Zeroed.", get_sb_free_blocks(super));
	set_sb_free_blocks(super, get_sb_block_count(super));
    }
    
    /* Check the root block number. */
    if (get_sb_root_block(super) > get_sb_block_count(super)) {
	aal_exception_error("Invalid root block (%llu) found in the superblock."
	    " Zeroed.", get_sb_root_block(super));
	set_sb_root_block(super, get_sb_block_count(super));
    }
    
    /* Some extra check for root block? */    

    /* Check the tail policy. */
    if (get_sb_tail_policy(super) >= TAIL_LAST_ID) {
	int tail_id = TAIL_LAST_ID;
	aal_exception_error("Invalid tail policy (%u) found in the superblock.", 
	    get_sb_tail_policy(super));
	
	result = aal_ui_get_numeric(0, callback_tail_check, &tail_id, "Enter "
	    "the preferable tail policy (0-%u)",  TAIL_LAST_ID - 1);
	
	set_sb_tail_policy(super, result);
    }
    return 0;
}

errno_t format40_print(reiser4_entity_t *entity, char *buff, 
    size_t n, uint16_t options) 
{
    format40_super_t *super;
    
    aal_assert("vpf-246", entity != NULL, return -1);
    
    if (!buff) return -1;

    super = format40_super(((format40_t *)entity)->block);
    
    reiser4_aux_strcat(buff, n, "Format40 on-disk format\n");    
    reiser4_aux_strcat(buff, n, "Count of blocks free/all: (%llu)/(%llu)\n", 
	get_sb_free_blocks(super), get_sb_block_count(super));        
    reiser4_aux_strcat(buff, n, "Root block (%llu)\n", get_sb_root_block(super));
    reiser4_aux_strcat(buff, n, "Tail policy (%u)\n", get_sb_tail_policy(super));
    reiser4_aux_strcat(buff, n, "Oid (%llu)\n", get_sb_oid(super));
    reiser4_aux_strcat(buff, n, "File count (%llu)\n", get_sb_file_count(super));
    reiser4_aux_strcat(buff, n, "Flushes (%llu)\n", get_sb_flushes(super));
    reiser4_aux_strcat(buff, n, "Magic (%s)\n", super->sb_magic);
    reiser4_aux_strcat(buff, n, "Tree height (%u)\n", get_sb_tree_height(super));
    
    return 0;
}

