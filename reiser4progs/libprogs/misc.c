/*
    misc.c -- miscellaneous useful tools for reiser4 progs.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <progs/misc.h>

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

long long int progs_misc_strtol(const char *str) {
    char *not_digit;
    long long int result = 0;

    if (!str)
       return -1;
       
    result = strtol(str, &not_digit, 0);
       
    if (errno == ERANGE)
	return -1;
    
    if (*not_digit && strcmp(not_digit, "\n"))
       return -1;
       
    return result;
}

static int progs_get_mount_entry(const char *filename, struct mntent **mntent) {
    int retval;
    FILE *fp;
    struct statfs stfs;
    struct stat st;
    mode_t mode;

    *mntent = NULL;

    if (stat(filename, &st) == -1) {
	progs_error("Stat on '%s' failed: %s", filename, strerror(errno));
	return -1;
    }

    if (!S_ISBLK(st.st_mode))
	return 0;
    
    /* if proc filesystem is mounted */
    if (statfs("/proc", &stfs) == -1 || stfs.f_type != 0x9fa0/*procfs magic*/ ||
        (fp = setmntent("/proc/mounts", "r")) == NULL)
    {
	/* proc filesystem is not mounted, or /proc/mounts does not exist */
	return -1;
    }

    while ((*mntent = getmntent (fp)) != NULL)
        if (!strcmp(filename, (*mntent)->mnt_fsname)) 
	    break;

    endmntent(fp);
    return 0;
}

static int progs_get_mount_point(const char *filename, char *mount_point) {
    struct stat st, root_st;
    struct mntent *mntent;
    
    aal_memset(mount_point, 0, aal_strlen(mount_point));

    if (stat("/", &root_st) == -1) {
	progs_error("Stat on '/' failed: %s", strerror(errno));
	return -1;
    }
    if (stat(filename, &st) == -1) {
	progs_error("Stat on '%s' failed: %s", filename, strerror(errno));
	return -1;
    }
    if (root_st.st_dev == st.st_rdev) {
	aal_strncat(mount_point, "/", 1);
	return 0;
    }
    if (progs_get_mount_entry(filename, &mntent) == -1) 
	return -1;

    if (mntent) 
	aal_strncat(mount_point, mntent->mnt_dir, aal_strlen(mntent->mnt_dir));
    
    return 0;    
}

static int progs_is_file_ro(const char *filename) {
    if (!filename)
	return -1;
    
    return utime(filename, 0) == 0 ? 0 : (errno == EROFS ? 1 : -1);

    return -1;
}

int progs_is_mounted(const char *filename) {
    char mount_point[4096];

    if (progs_get_mount_point(filename, mount_point) == -1)
	return -1;

    return (aal_strlen(mount_point) != 0);
}

int progs_is_mounted_ro(const char *filename) {
    char mount_point[4096];

    if (progs_get_mount_point(filename, mount_point) == -1)
	return -1;

    if (aal_strlen(mount_point) == 0)
	return -1;

    return progs_is_file_ro(filename);
}

long long int progs_misc_size_parse(const char *str) {
    long long int size;
    char number[255], label;
       
    if (!str || strlen(str) == 0 || strlen (str) > 255) 
       return -1;
       
    memset(number, 0, 255);
    strncpy(number, str, 255);
    label = number[strlen(number) - 1];

    if (label == '\n')
	label = number[strlen(number) - 2];

    if (label == 'K' || label == 'M' || label == 'G')
       number[strlen(number) - 1] = '\0';
       
    if ((size = progs_misc_strtol(number)) < 0)
       return -1;

    if (label == 'K')   
	size *= KB;
    else if (label == 'M') 
	size *= MB;
    else if (label == 'G') 
	size *= GB;

    return size;
}

