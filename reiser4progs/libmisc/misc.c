/*
    misc.c -- miscellaneous useful code. 
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>

#include <misc/misc.h>

/* 
    This implements binary search for "needle" among "count" elements.
    
    Return values: 
    1 - key on *pos found exact key on *pos position; 
    0 - exact key has not been found. key of *pos < then wanted.
*/
int reiserfs_misc_bin_search(
    void *array,		    /* array search will be performed on */ 
    uint32_t count,		    /* array size */
    void *needle,		    /* element to be found */
    reiserfs_elem_func_t elem_func, /* getting next element function */
    reiserfs_comp_func_t comp_func, /* comparing function */
    void *data,			    /* user-specified data will be passed to both callbacks */
    uint64_t *pos)		    /* result position will be stored here */
{
    void *elem;
    int ret = 0;
    int right, left, j;

    if (count == 0) {
        *pos = 0;
        return 0;
    }

    left = 0;
    right = count - 1;

    for (j = (right + left) / 2; left <= right; j = (right + left) / 2) {
	if (!(elem = elem_func(array, j, data)))
	    return -1;
	
        if ((ret = comp_func(elem, needle, data)) < 0) { 
            left = j + 1;
            continue;
        } else if (ret > 0) { 
            if (j == 0) 
		break;
            right = j - 1;
            continue;
        } else { 
            *pos = j;
            return 1;
        }
    }

    *pos = left;
    return 0;
}

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

long int reiser4progs_misc_strtol(const char *str, int *error) {
    char *err;
    long result = 0;

    if (error)
	*error = 0;
	
    if (!str) {
	if (error) *error = 1; 
	return 0;
    }	
	
    result = strtol(str, &err, 10);
	
    if (errno == ERANGE || *err) {
	if (error) *error = 1;
	return 0;
    }	
	
    return result;
}

int reiser4progs_misc_choose_check(const char *chooses, int choose) {
    unsigned i;
	
    if (!chooses) return 0;
	
    for (i = 0; i < strlen(chooses); i++)
	if (chooses[i] == choose) return 1;
	
    return 0;
}

int reiser4progs_misc_choose_propose(const char *chooses, 
    const char *error, const char *format, ...) 
{
    va_list args;
    int choose, prompts = 0;
    char mess[4096], buf[255];
	
    if (!chooses || !format || !error)
	return 0;
	
    memset(mess, 0, 4096);
	
    va_start(args, format);
    vsprintf(mess, format, args);
    va_end(args);
	
    fprintf(stderr, mess);
    fflush(stderr);
	
    do {
	memset(buf, 0, 255);
		
	fgets(buf, 255, stdin);
	choose = buf[0];
			
	if (reiser4progs_misc_choose_check(chooses, choose)) 
	    break;
		
	if (prompts < 2) {
	    fprintf(stderr, error);
	    fflush(stderr);
	}	
    } while (prompts++ < 2);
	
    if (!reiser4progs_misc_choose_check(chooses, choose))
	choose = 0;
	
    return choose;
}

int reiser4progs_misc_dev_check(const char *dev) {
    struct stat st;
	
    if (!dev)
	return 0;
	
    if (stat(dev, &st) == -1)
	return 0;
	
    if (!S_ISBLK(st.st_mode)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, 
	    "Device %s isn't a block device.", dev);
    }
	
    return 1;
}

unsigned long long reiser4progs_misc_size_parse(const char *str, int *error) {
    unsigned long long size;
    char number[255], label = 0;
	 
    if (error)
	*error = 0;
	
    if (!str || strlen(str) == 0) {
	if (error) *error = 1;
	return 0;
    }	
	
    memset(number, 0, 255);
    strncpy(number, str, strlen(str));
    label = number[strlen(number) - 1];
	
    if (label == 'K' || label == 'M' || label == 'G')
	number[strlen(number) - 1] = '\0';
    else
	label = 0;	
	
    if ((size = reiser4progs_misc_strtol(number, error)) == 0 && *error)
	return 0;
	
    if (label == 0 || label == 'M')
	size = size * MB;
    else if (label == 'K')
	size = size * KB;
    else if (label == 'G')
	size = size * GB;

    return size;
}

int reiser4progs_misc_size_check(const char *str) {
    int error = 0;

    reiser4progs_misc_size_parse(str, &error);
    return !error;
}

static reiserfs_profile_t reiser4profiles[] = {
    [0] = {
	.label = "default40",
	.desc = "Profile for reiser4 with default tail policy",
    
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM,
	    .tail     = REISERFS_TAIL_ITEM,
	    .extent   = REISERFS_EXTENT_ITEM,
	},
	.file = 0x0,
	.dir = 0x0,
	.hash = 0x0,
	.tail = REISERFS_SMART_TAIL,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0
    },
    [1] = {
	.label = "extent40",
	.desc = "Profile for reiser4 with extents turned on",
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM,
	    .tail     = REISERFS_TAIL_ITEM,
	    .extent   = REISERFS_EXTENT_ITEM,
	},
	.file = 0x0,
	.dir = 0x0,
	.hash = 0x0,
	.tail = REISERFS_NEVER_TAIL,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0
    },
    [2] = {
	.label = "tail40",
	.desc = "Profile for reiser4 with tail only turned on",
    
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM,
	    .tail     = REISERFS_TAIL_ITEM,
	    .extent   = REISERFS_EXTENT_ITEM,
	},
	.file = 0x0,
	.dir = 0x0,
	.hash = 0x0,
	.tail = REISERFS_ALWAYS_TAIL,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0
    }
};

reiserfs_profile_t *reiser4progs_find_profile(const char *profile) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++) {
	if (!strncmp(reiser4profiles[i].label, profile, strlen(reiser4profiles[i].label)))
	    return &reiser4profiles[i];
    }

    return NULL;
}

void reiser4progs_list_profile(void) {
    unsigned i;
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++)
	printf("(%d) %s (%s).\n", i + 1, reiser4profiles[i].label, reiser4profiles[i].desc);
}

