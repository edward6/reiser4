/*
    misc.c -- some common tools for all reiser4 utilities.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <stdio.h>
#include <mntent.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>

#include <aux/aux.h>
#include <reiser4/reiser4.h>

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

/* Converts human readable size string like "256M" into kb */
long long progs_misc_size_parse(
    const char *str,		/* string to be converted */
    int *error			/* error will be stored here */
) {
    long long size;
    char number[255], label = 0;
	 
    if (error)
	*error = 0;
	
    if (!str || strlen(str) == 0) {
	if (error) *error = 1;
	return 0;
    }	
	
    memset(number, 0, 255);
    aal_strncpy(number, str, strlen(str));
    label = number[strlen(number) - 1];
	
    if (toupper(label) == toupper('k') || toupper(label) == toupper('m') || 
	    toupper(label) == toupper('g'))
	number[strlen(number) - 1] = '\0';
    else
	label = 0;	
	
    if ((size = reiser4_aux_strtol(number, error)) == 0 && *error)
	return 0;
	
    if (toupper(label) == toupper('m'))
	size = size * MB;
    else if (toupper(label) == toupper('k'))
	size = size * KB;
    else if (toupper(label) == toupper('g'))
	size = size * GB;

    *error = ~0;
    return size;
}

/* 
    Checking if specified partition is mounted. It is possible devfs is used, and 
    all devices are links like /dev/hda1 -> ide/host0/bus0/target0/lun0/part1. 
    Therefore we use stat function, rather than lstat: stat(2) follows links and 
    return stat information for link target. In this case it will return stat info 
    for /dev/ide/host0/bus0/target0/lun0/part1 file. Then we compare its st_rdev 
    field with st_rdev filed of every device from /proc/mounts. If match occurs then 
    we have device existing in /proc/mounts file, which is therefore mounted at the 
    moment.

    Also this function checks whether passed device mounted with specified options.
    Options string may look like "ro,noatime".
    
    We are using stating of every mount entry instead of just name comparing, because 
    file /proc/mounts may contain as devices like /dev/hda1 as ide/host0/bus0/targ...
*/
int progs_misc_dev_mounted(
    const char *name,	/* device name to be checked */
    const char *mode	/* mount options for check */
) {
    FILE *mnt;
    struct mntent *ent;
    struct stat giv_st;
    struct stat mnt_st;
    struct statfs fs_st;

    /* Stating given device */
    if (stat(name, &giv_st) == -1) 
	return -1;
 
    /* Procfs magic is 0x9fa0 */
    if (statfs("/proc", &fs_st) == -1 || fs_st.f_type != 0x9fa0) {
	/* Proc is not mounted, check if it is the root partition. */
	if (stat("/", &mnt_st) == -1) 
	    return -1;
 
	if (mnt_st.st_dev == giv_st.st_rdev) 	    
	    return 1;	
        
	return -1;
    }
    
    /* Going to check /proc/mounts */
    if (!(mnt = setmntent("/proc/mounts", "r")))
	return -1;
    
    while ((ent = getmntent(mnt))) {
	if (stat(ent->mnt_fsname, &mnt_st) == 0) {
	    if (mnt_st.st_rdev == giv_st.st_rdev) {
		char *token;
		
		while (mode && (token = aal_strsep((char **)&mode, ","))) {
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

void progs_misc_upper(char *dst, const char *src) {
    int i = 0;
    const char *s;

    s = src;
    while (*s) dst[i++] = toupper(*s++);
    dst[i] = '\0';
}

static errno_t callback_print_plugin(reiser4_plugin_t *plugin, void *data) {
    printf("%s: %s.\n", plugin->h.label, plugin->h.desc);
    return 0;
}

void progs_misc_factory_list(void) {
    printf("\nKnown plugins are:\n");
    libreiser4_factory_foreach(callback_print_plugin, NULL);
    printf("\n");
}

