/*
    misc.c -- some common tools for all reiser4 utilities.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <stdio.h>
#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <progs/misc.h>

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

static reiserfs_profile_t reiser4profiles[] = {
    [0] = {
	.label = "default40",
	.desc = "Profile for reiser4 with default drop policy",
    
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM,
	    .extent = REISERFS_EXTENT_ITEM,
	    .drop = REISERFS_DROP_ITEM,
	},

	.object = {
	    .file = 0x0,
	    .dir = 0x0,
	},
	
	.hash = 0x0,
	.drop_policy = REISERFS_SMART_DROP,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0,
	
	.sdext = 1 << REISERFS_UNIX_SDEXT
    },
    [1] = {
	.label = "extent40",
	.desc = "Profile for reiser4 with extents turned on",
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM,
	    .extent = REISERFS_EXTENT_ITEM,
	    .drop = REISERFS_DROP_ITEM,
	},

	.object = {
	    .file = 0x0,
	    .dir = 0x0
	},
	.hash = 0x0,
	.drop_policy = REISERFS_NEVER_DROP,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0,
	
	.sdext = 1 << REISERFS_UNIX_SDEXT
    },
    [2] = {
	.label = "tail40",
	.desc = "Profile for reiser4 with drops only turned on",
    
	.node = 0x0,
	.item = {
	    .internal = REISERFS_INTERNAL_ITEM,
	    .statdata = REISERFS_STATDATA_ITEM,
	    .direntry = REISERFS_CDE_ITEM,
	    .extent = REISERFS_EXTENT_ITEM,
	    .drop = REISERFS_DROP_ITEM,
	},

	.object = {
	    .file = 0x0,
	    .dir = 0x0
	},
	
	.hash = 0x0,
	.drop_policy = REISERFS_ALWAYS_DROP,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0,
	
	.sdext = 1 << REISERFS_UNIX_SDEXT
    }
};

/* Converts string denoted a size into digits */
long int progs_misc_strtol(
    const char *str,	    /* string to be converted */
    int *error		    /* error will be stored here */
) {
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

/* Converts human readable patition size like "256M" into digits */
unsigned long long progs_misc_size_parse(
    const char *str,		/* string to be converted */
    int *error			/* error will be stored here */
) {
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
	
    if (toupper(label) == toupper('k') || toupper(label) == toupper('m') || 
	    toupper(label) == toupper('g'))
	number[strlen(number) - 1] = '\0';
    else
	label = 0;	
	
    if ((size = progs_misc_strtol(number, error)) == 0 && *error)
	return 0;
	
    if (label == 0 || toupper(label) == toupper('m'))
	size = size * MB;
    else if (toupper(label) == toupper('k'))
	size = size * KB;
    else if (toupper(label) == toupper('g'))
	size = size * GB;

    return size;
}

/* Checks human readable size for validness */
int progs_misc_size_check(
    const char *str	    /* string to bwe checked */
) {
    int error = 0;

    progs_misc_size_parse(str, &error);
    return !error;
}

/* 
    Checking if specified partition is mounted. It is possible devfs is used, and 
    all devices are links like /dev/hda1 -> ide/host0/bus0/target0/lun0/part1. 
    Therefore we use stat function, rather than lstat: stat(2) follows 
    links and return stat information for link target. In this case it will return 
    stat information for /dev/ide/host0/bus0/target0/lun0/part1 file. Then we just 
    compare its st_rdev field with st_rdev filed of every device from /etc/mtab. If 
    match occurs then we have device existing in /etc/mtab file, which is therefore 
    mounted at the moment.

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
		while (ops && (token = aal_strsep((char **)&ops, ","))) {
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

/* Finds profile by its name */
reiserfs_profile_t *progs_misc_profile_find(
    const char *profile		    /* needed profile name */
) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++) {
	if (!strncmp(reiser4profiles[i].label, profile, strlen(reiser4profiles[i].label)))
	    return &reiser4profiles[i];
    }

    return NULL;
}

/* Shows all knows profiles */
void progs_misc_profile_list(void) {
    unsigned i;
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++)
	printf("(%d) %s (%s).\n", i + 1, reiser4profiles[i].label, reiser4profiles[i].desc);
}

/* 
    Common reiser4progs exception handler functions. This one returns number of
    specified turned on options.
*/
static int __progs_exception_bit_count(
    aal_exception_option_t options,	    /* options to be inspected */
    int start				    /* options will be inspected started from */
) {
    int i, res = 0;
    
    for (i = start; i < aal_log2(EXCEPTION_LAST); i++)
	res += ((1 << i) & options) ? 1 : 0;

    return res;
}

/* This function print turned on options */
static void __progs_exception_print_options(aal_exception_option_t options) {
    int i;

    if (__progs_exception_bit_count(options, 0) == 0)
	return;
    
    printf("(");
    for (i = 1; i < aal_log2(EXCEPTION_LAST); i++) {
	if ((1 << i) & options) {
	    if (i < aal_log2(EXCEPTION_LAST) - 1 && 
		    __progs_exception_bit_count(options, i + 1) > 0)
		printf("%s/", aal_exception_option_string(1 << i));
	    else
		printf("%s)? ", aal_exception_option_string(1 << i));
	}
    }
}

/* This function makes serach for option by its name */
static aal_exception_option_t __progs_exception_option_by_name(char *name) {
    int i;
    
    if (!name || aal_strlen(name) == 0)
	return EXCEPTION_UNHANDLED;
    
    for (i = 0; i < aal_log2(EXCEPTION_LAST); i++) {
	char *opt = aal_exception_option_string(1 << i);
	
	if (aal_strncmp(opt, name, aal_strlen(name)) == 0 || 
		(aal_strlen(name) == 1 && toupper(opt[0]) == toupper(name[0])))
	    return 1 << i;
    }
    
    return EXCEPTION_UNHANDLED;
}

/* This function gets user enter */
static aal_exception_option_t __progs_exception_selected_option(void) {
    char str[256];
    
    aal_memset(str, 0, sizeof(str));
    return __progs_exception_option_by_name(gets(str));
}

/* 
    Common exception handler for all reiser4progs. It implements exception handling 
    in "question-answer" maner and used for all communications with user.
*/
aal_exception_option_t __progs_exception_handler(
    aal_exception_t *exception		/* exception to be processed */
) {
    aal_exception_option_t opt;
    
    if (exception->type == EXCEPTION_ERROR || 
	exception->type == EXCEPTION_FATAL ||
	exception->type == EXCEPTION_BUG)
        aal_gauge_failed(); 
    else
	aal_gauge_pause();

    do {
	fflush(stdout);
	
	/* Printing exception type */
	if (exception->type != EXCEPTION_BUG)
	    printf("%s: ", aal_exception_type_string(exception->type));
    
	/* Printing exception message */
	printf("%s ", exception->message);
    
	if (__progs_exception_bit_count(exception->options, 0) == 1) {
	    printf("\n");
	    
	    if (exception->type == EXCEPTION_WARNING || 
		    exception->type == EXCEPTION_INFORMATION)
		aal_gauge_resume();
	    
	    return exception->options;
	}
	    
	__progs_exception_print_options(exception->options);
	opt = __progs_exception_selected_option();
	
    } while (opt == EXCEPTION_UNHANDLED);

    if (exception->type == EXCEPTION_WARNING || 
	    exception->type == EXCEPTION_INFORMATION)
	aal_gauge_resume();
	    
    return opt;
}

/* Common gauge handler */
#define GAUGE_BITS_SIZE 4

static inline void __progs_gauge_blit(void) {
    static short bitc = 0;
    static const char bits[] = "|/-\\";

    putc(bits[bitc], stderr);
    putc('\b', stderr);
    fflush(stderr);
    bitc++;
    bitc %= GAUGE_BITS_SIZE;
}

/* This functions "draws" gauge header */
static inline void __progs_gauge_header(
    const char *name,		/* gauge name */
    aal_gauge_type_t type	/* gauge type */
) {
    if (name) {
	if (type != GAUGE_SILENT)
	    fprintf(stderr, "\r%s: ", name);
	else
	    fprintf(stderr, "\r%s...", name);
    }
}

/* This function "draws" gauge footer */
static inline void __progs_gauge_footer(
    const char *name,	    /* footer name */
    aal_gauge_type_t type   /* gauge type */
) {
    if (name)
	fputs(name, stderr);
}

/* Common gauge handler */
void __progs_gauge_handler(aal_gauge_t *gauge) {
    if (gauge->state == GAUGE_PAUSED) {
	putc('\r', stderr);
	fflush(stderr);
	return;
    }
	
    if (gauge->state == GAUGE_STARTED)
	__progs_gauge_header(gauge->name, gauge->type);
	
    switch (gauge->type) {
	case GAUGE_PERCENTAGE: {
	    unsigned int i;
	    char display[10] = {0};
		
	    sprintf(display, "%d%%", gauge->value);
	    fputs(display, stderr);
		
	    for (i = 0; i < strlen(display); i++)
		fputc('\b', stderr);
	    break;
	}
	case GAUGE_INDICATOR: {
	    __progs_gauge_blit();
	    break;
	}
	case GAUGE_SILENT: break;
    }

    if (gauge->state == GAUGE_DONE)
	__progs_gauge_footer("done\n", gauge->type);
    
    if (gauge->state == GAUGE_FAILED)
	__progs_gauge_footer("failed\n", gauge->type);
	
    fflush(stderr);
}

