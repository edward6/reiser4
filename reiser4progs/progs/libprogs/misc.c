/*
    misc.c -- some common tools for all reiser4 utilities.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <stdio.h>
#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <progs/misc.h>

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

long int progs_misc_strtol(const char *str, int *error) {
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

int progs_misc_choose_check(const char *chooses, int choose) {
    unsigned i;
	
    if (!chooses) return 0;
	
    for (i = 0; i < strlen(chooses); i++)
	if (chooses[i] == choose) return 1;
	
    return 0;
}

int progs_misc_choose_propose(const char *chooses, 
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
			
	if (progs_misc_choose_check(chooses, choose)) 
	    break;
		
	if (prompts < 2) {
	    fprintf(stderr, error);
	    fflush(stderr);
	}	
    } while (prompts++ < 2);
	
    if (!progs_misc_choose_check(chooses, choose))
	choose = 0;
	
    return choose;
}

unsigned long long progs_misc_size_parse(const char *str, int *error) {
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
	
    if ((size = progs_misc_strtol(number, error)) == 0 && *error)
	return 0;
	
    if (label == 0 || label == 'M')
	size = size * MB;
    else if (label == 'K')
	size = size * KB;
    else if (label == 'G')
	size = size * GB;

    return size;
}

int progs_misc_size_check(const char *str) {
    int error = 0;

    progs_misc_size_parse(str, &error);
    return !error;
}

/* 
    Checking if specified partition is mounted. It is possible devfs is used, and 
    all devices are links like /dev/hda1 -> ide/host0/bus0/target0/lun0/part1. 
    Therefore we are use stat function, not lstat. As you know stat funtion follows 
    links and return stat information for link target. In this case it will return 
    stat information for /dev/ide/host0/bus0/target0/lun0/part1 file. Then we just 
    compare its st_rdev field with st_rdev filed of every device from /etc/mtab. If 
    match occurs then we have passed device existent in /etc/mtab file and therefore 
    passed device is mounted at the moment.

    Also this function checks whether passed device mounted with specified options.
    Options string may look like "ro,noatime".
    
    We are using stating of every mtab entry instead of just name comparing, because 
    file /etc/mtab may contain as devices like /dev/hda1 as ide/host0/bus0/target0...
*/
int progs_misc_dev_mounted(const char *name, 
    const char *ops) 
{
    FILE *mnt;
    struct mntent *ent;
    struct stat giv_st;
    struct stat mnt_st;

    if (!(mnt = setmntent("/etc/mtab", "r")))
	return 0;
    
    if (stat(name, &giv_st) == -1)
	return 0;
    
    while ((ent = getmntent(mnt))) {
	if (stat(ent->mnt_fsname, &mnt_st) == 0) {
	    if (mnt_st.st_rdev == giv_st.st_rdev) {
		char *token;
		while ((token = aal_strsep(ops ? (char **)&ops : NULL, ","))) {
		    if (!hasmntopt(ent, token))
			goto error_free_mnt;
		}
		endmntent(mnt);
		return 1;
	    }
	}
    }

error_free_mnt:
    endmntent(mnt);
    return 0;
}

static reiserfs_profile_t reiser4profiles[] = {
    [0] = {
	.label = "default40",
	.desc = "Profile for reiser4 with default tail policy",
    
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM
	},

	.object = {
	    .file = 0x0,
	    .dir = 0x0,
	},
	
	.hash = 0x0,
	.tail_policy = REISERFS_SMART_TAIL,
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
	},

	.object = {
	    .file = 0x0,
	    .dir = 0x0
	},
	.hash = 0x0,
	.tail_policy = REISERFS_NEVER_TAIL,
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
	},

	.object = {
	    .file = 0x0,
	    .dir = 0x0
	},
	
	.hash = 0x0,
	.tail_policy = REISERFS_ALWAYS_TAIL,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0
    }
};

reiserfs_profile_t *progs_misc_profile_find(const char *profile) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++) {
	if (!strncmp(reiser4profiles[i].label, profile, strlen(reiser4profiles[i].label)))
	    return &reiser4profiles[i];
    }

    return NULL;
}

void progs_misc_profile_list(void) {
    unsigned i;
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++)
	printf("(%d) %s (%s).\n", i + 1, reiser4profiles[i].label, reiser4profiles[i].desc);
}
