/*
    progsmisc.c -- miscellaneous useful tools for reiser4 progs.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef HAVE_CONFIG_H
#   include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <utime.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <progs/progsmisc.h>

FILE *prog_log = NULL;

inline FILE *progs_get_log() {
    return prog_log;
}

inline void progs_set_log(FILE *log) {
    prog_log = log;
}

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
	prog_error("Stat on '%s' failed: %s", filename, strerror(errno));
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
	prog_error("Stat on '/' failed: %s", strerror(errno));
	return -1;
    }
    if (stat(filename, &st) == -1) {
	prog_error("Stat on '%s' failed: %s", filename, strerror(errno));
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

static reiserfs_profile_t reiser4profiles[] = {
    [0] = {
	.label = "default40",
        .desc = "Profile for reiser4 with smart tail policy",
	.node		= REISERFS_NODE40_ID,
	.object = {
	    .file	= REISERFS_FILE40_ID, 
	    .dir	= REISERFS_DIR40_ID,
	    .symlink	= REISERFS_SYMLINK40_ID,
	    .special	= REISERFS_SPECIAL40_ID,
	},
	.item = {
	    .internal = REISERFS_INTERNAL40_ID,
	    .statdata	= REISERFS_STATDATA40_ID,
	    .direntry	= REISERFS_CDE40_ID,
	    .file_body = {
		.tail	= REISERFS_TAIL40_ID,
		.extent	= REISERFS_EXTENT40_ID,
	    },
	    .acl	= REISERFS_ACL40_ID,
	},       
	.hash		= REISERFS_R5_HASH_ID,
	.tail_policy	= REISERFS_SMART_TAIL_ID,
	.perm		= REISERFS_RWX_PERM_ID,
	.format		= REISERFS_FORMAT40_ID,
	.oid		= REISERFS_OID40_ID,
	.alloc		= REISERFS_ALLOC40_ID,
	.journal	= REISERFS_JOURNAL40_ID,
	.key		= REISERFS_KEY40_ID
    },
    [1] = {
	.label = "extent40",
	.desc = "Profile for reiser4 with extents turned on",
	.node		= REISERFS_NODE40_ID,	
	.object = {
	    .file	= REISERFS_FILE40_ID,
	    .dir	= REISERFS_DIR40_ID,
	    .symlink	= REISERFS_SYMLINK40_ID,
	    .special	= REISERFS_SPECIAL40_ID,
	},
	.item = {
	    .internal	= REISERFS_INTERNAL40_ID,
	    .statdata	= REISERFS_STATDATA40_ID,
	    .direntry	= REISERFS_CDE40_ID,
	    .file_body = {
		.tail	= REISERFS_TAIL40_ID,
		.extent	= REISERFS_EXTENT40_ID,
	    },
	    .acl	= REISERFS_ACL40_ID,
	},
	.hash		= REISERFS_R5_HASH_ID,
	.tail_policy	= REISERFS_NEVER_TAIL_ID,
	.perm		= REISERFS_RWX_PERM_ID,
	.format		= REISERFS_FORMAT40_ID,
	.oid		= REISERFS_OID40_ID,
	.alloc		= REISERFS_ALLOC40_ID,
	.journal	= REISERFS_JOURNAL40_ID,
	.key		= REISERFS_KEY40_ID
    },
    [2] = {
	.label = "tail40",
	.desc = "Profile for reiser4 with tails turned on",     
	.node		= REISERFS_NODE40_ID,
	.object = {
	    .file	= REISERFS_FILE40_ID,
	    .dir	= REISERFS_DIR40_ID,
	    .symlink	= REISERFS_SYMLINK40_ID,
	    .special	= REISERFS_SPECIAL40_ID,
	},
	.item = {
	    .internal	= REISERFS_INTERNAL40_ID,
	    .statdata	= REISERFS_STATDATA40_ID,
	    .direntry	= REISERFS_CDE40_ID,
	    .file_body = {
		.tail	= REISERFS_TAIL40_ID,
		.extent	= REISERFS_EXTENT40_ID,
	    },
	    .acl	= REISERFS_ACL40_ID,
	},
	.hash		= REISERFS_R5_HASH_ID,
	.tail_policy	= REISERFS_ALWAYS_TAIL_ID,
	.perm		= REISERFS_RWX_PERM_ID,
	.format		= REISERFS_FORMAT40_ID,
	.oid		= REISERFS_OID40_ID,
	.alloc		= REISERFS_ALLOC40_ID,
	.journal	= REISERFS_JOURNAL40_ID,
	.key		= REISERFS_KEY40_ID
    }
};

/* 0 profile is the default one. */
reiserfs_profile_t*progs_get_default_profile() {
    return &reiser4profiles[0];
}

reiserfs_profile_t *progs_find_profile(const char *profile) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++) {
       if (!strncmp(reiser4profiles[i].label, profile, strlen(reiser4profiles[i].label)))
           return &reiser4profiles[i];
    }

    return NULL;
}

void progs_print_profile_list(void) {
    unsigned i;
    
    printf("\n");
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++)
       printf("Profile %s: %s.\n", reiser4profiles[i].label, reiser4profiles[i].desc);
    printf("\n");
}

enum plugin_internal_type {
    PROGS_NODE_PLUGIN,
    PROGS_FILE_PLUGIN,
    PROGS_DIR_PLUGIN,
    PROGS_SYMLINK_PLUGIN,
    PROGS_SPECIAL_PLUGIN,
    PROGS_INTERNAL_PLUGIN, 
    PROGS_STATDATA_PLUGIN, 
    PROGS_DIRENTRY_PLUGIN,
    PROGS_TAIL_PLUGIN,
    PROGS_EXTENT_PLUGIN,
    PROGS_ACL_PLUGIN,
    PROGS_HASH_PLUGIN,
    PROGS_TAIL_POLICY_PLUGIN,
    PROGS_PERM_PLUGIN,
    PROGS_FORMAT_PLUGIN,
    PROGS_OID_PLUGIN,
    PROGS_ALLOC_PLUGIN,
    PROGS_JOURNAL_PLUGIN,
    PROGS_KEY_PLUGIN,
    PROGS_LAST_PLUGIN
};

static char *progs_internal_plugin_name[] = {
    "NODE",
    "REGULAR_FILE",
    "DIR_FILE",
    "SYMLINK_FILE",
    "SPECIAL_FILE",
    "INTERNAL_ITEM",
    "STATDATA_ITEM",
    "DIRENTRY_ITEM",
    "TAIL_ITEM",
    "EXTENT_ITEM",
    "ACL_ITEM",
    "HASH",
    "TAIL_POLICY",
    "PERM",
    "FORMAT",
    "OID",
    "ALLOC",
    "JOURNAL",
    "KEY"
};

typedef enum plugin_internal_type plugin_internal_type_t;

static reiserfs_id_t *progs_get_profile_field(reiserfs_profile_t *profile, 
    plugin_internal_type_t internal_type) 
{
    if (!profile)
	return NULL;
	
    if (internal_type >= PROGS_LAST_PLUGIN) 
	return NULL;
    switch (internal_type) {
	case PROGS_NODE_PLUGIN:
	    return &profile->node;
	case PROGS_FILE_PLUGIN:
	    return &profile->object.file;
	case PROGS_DIR_PLUGIN:
	    return &profile->object.dir;
	case PROGS_SYMLINK_PLUGIN:
	    return &profile->object.symlink;
	case PROGS_SPECIAL_PLUGIN:
	    return &profile->object.special;
	case PROGS_INTERNAL_PLUGIN:
	    return &profile->item.internal;
	case PROGS_STATDATA_PLUGIN:
	    return &profile->item.statdata;
	case PROGS_DIRENTRY_PLUGIN:
	    return &profile->item.direntry;
	case PROGS_TAIL_PLUGIN:
	    return &profile->item.file_body.tail;
	case PROGS_EXTENT_PLUGIN:
	    return &profile->item.file_body.extent;
	case PROGS_ACL_PLUGIN:
	    return &profile->item.acl;
	case PROGS_HASH_PLUGIN:
	    return &profile->hash;
	case PROGS_TAIL_POLICY_PLUGIN:
	    return &profile->tail_policy;
	case PROGS_PERM_PLUGIN:
	    return &profile->perm;
	case PROGS_FORMAT_PLUGIN:
	    return &profile->format;
	case PROGS_OID_PLUGIN:
	    return &profile->oid;
	case PROGS_ALLOC_PLUGIN:
	    return &profile->alloc;
	case PROGS_JOURNAL_PLUGIN:
	    return &profile->journal;
	case PROGS_KEY_PLUGIN:
	    return &profile->key;
	case PROGS_LAST_PLUGIN:
	    /* make the gcc to shut up */
	    return NULL;	    
    }
    return NULL;
}

static reiserfs_plugin_type_t progs_get_plugin_type(plugin_internal_type_t internal_type) {
    if (internal_type >= PROGS_LAST_PLUGIN) 
	return -1;    
    switch (internal_type) {
	case PROGS_NODE_PLUGIN:
	    return REISERFS_NODE_PLUGIN;
	case PROGS_FILE_PLUGIN:
	    return REISERFS_OBJECT_PLUGIN;
	case PROGS_DIR_PLUGIN:
	    return REISERFS_OBJECT_PLUGIN;
	case PROGS_SYMLINK_PLUGIN:
	    return REISERFS_OBJECT_PLUGIN;
	case PROGS_SPECIAL_PLUGIN:
	    return REISERFS_OBJECT_PLUGIN;
	case PROGS_INTERNAL_PLUGIN:
	    return REISERFS_ITEM_PLUGIN;
	case PROGS_STATDATA_PLUGIN:
	    return REISERFS_ITEM_PLUGIN;
	case PROGS_DIRENTRY_PLUGIN:
	    return REISERFS_ITEM_PLUGIN;
	case PROGS_TAIL_PLUGIN:
	    return REISERFS_ITEM_PLUGIN;
	case PROGS_EXTENT_PLUGIN:
	    return REISERFS_ITEM_PLUGIN;
	case PROGS_ACL_PLUGIN:
	    return REISERFS_ITEM_PLUGIN;
	case PROGS_HASH_PLUGIN:
	    return REISERFS_HASH_PLUGIN;
	case PROGS_TAIL_POLICY_PLUGIN:
	    return REISERFS_TAIL_POLICY_PLUGIN;
	case PROGS_PERM_PLUGIN:
	    return REISERFS_PERM_PLUGIN;
	case PROGS_FORMAT_PLUGIN:
	    return REISERFS_FORMAT_PLUGIN;
	case PROGS_OID_PLUGIN:
	    return REISERFS_OID_PLUGIN;
	case PROGS_ALLOC_PLUGIN:
	    return REISERFS_ALLOC_PLUGIN;
	case PROGS_JOURNAL_PLUGIN:
	    return REISERFS_JOURNAL_PLUGIN;
	case PROGS_KEY_PLUGIN:
	    return REISERFS_KEY_PLUGIN;
	case PROGS_LAST_PLUGIN:
	    /* Make the gcc to shut up. */
	    return -1;
    }
    return -1;
}

static plugin_internal_type_t progs_get_plugin_internal_type(const char *plugin_type_name) {
    if (!plugin_type_name)
	return -1;

    if (!aal_strncmp(plugin_type_name, "NODE", 4)) {
	return PROGS_NODE_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "FILE", 4)) {
	return PROGS_FILE_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "DIR", 3)) {
	return PROGS_DIR_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "SYMLINK", 7)) {
	return PROGS_SYMLINK_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "SPECIAL", 7)) {
	return PROGS_SPECIAL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "INTERNAL", 8)) {	
	return PROGS_INTERNAL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "STATDATA", 8)) {
	return PROGS_STATDATA_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "DIRENTRY", 8)) {
	return PROGS_DIRENTRY_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "TAIL", 4)) {
	return PROGS_TAIL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "EXTENT", 6)) {
	return PROGS_EXTENT_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "ACL", 3)) {
	return PROGS_ACL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "HASH", 4)) {
	return PROGS_HASH_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "TAIL_POLICY", 11)) {
	return PROGS_TAIL_POLICY_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "PERM", 4)) {
	return PROGS_PERM_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "FORMAT", 6)) {
	return PROGS_FORMAT_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "OID", 3)) {
	return PROGS_OID_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "ALLOC", 5)) {
	return PROGS_ALLOC_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "JOURNAL", 7)) {
	return PROGS_JOURNAL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "KEY", 3)) {
	return PROGS_KEY_PLUGIN;
    } else {
	return -1;
    }    
}

static char *progs_get_plugin_internal_type_name(plugin_internal_type_t internal_type) {
    return (internal_type >= PROGS_LAST_PLUGIN) ? NULL : 
	progs_internal_plugin_name[internal_type];
}


int progs_profile_override_plugin_id_by_name(reiserfs_profile_t *profile, 
    const char *plugin_type_name, const char *plugin_label) 
{
    plugin_internal_type_t int_type;
    reiserfs_plugin_type_t type;
    reiserfs_id_t *id;
    reiserfs_plugin_t *plugin;

    if (!profile || !plugin_type_name || !plugin_label)
	return -1;
       	
    if ((int_type = progs_get_plugin_internal_type(plugin_type_name)) == 
	(plugin_internal_type_t)-1)
	return -1;
    
    if ((type = progs_get_plugin_type(int_type)) == (reiserfs_plugin_type_t)-1)
	return -1;

    if (!(plugin = libreiser4_factory_find_by_name(type, plugin_label))) 
	return -1;
    
    if ((id = progs_get_profile_field(profile, int_type)) == NULL)
	return -1;
    
    *id = plugin->h.id;
 
    return 0;
}

void progs_print_profile(reiserfs_profile_t *profile) {
    int i;
    reiserfs_plugin_t *plugin;

    if (!profile)
	return;
	
    printf("\nProfile %s: %s.\n", profile->label, profile->desc);
    for (i = 0; i < PROGS_LAST_PLUGIN; i++) {

	if ((plugin = libreiser4_factory_find_by_id(
	    progs_get_plugin_type(i), *progs_get_profile_field(profile, i))) != NULL) 
	{
	    printf("%s plugin is %s: %s.\n", progs_get_plugin_internal_type_name(i), 
		plugin->h.label, plugin->h.desc);
	} else {
	    printf("%s plugin is NOT FOUND.\n", progs_get_plugin_internal_type_name(i));
	}
    }
    printf("\n");
}

void progs_print_plugins() {
    reiserfs_plugin_t *plugin = NULL;

    printf("\nKnown plugins are:\n");
    while ((plugin = libreiser4_factory_get_next(plugin)) != NULL) {
	printf("%s: %s.\n", plugin->h.label, plugin->h.desc);
    }
    printf("\n");
}
