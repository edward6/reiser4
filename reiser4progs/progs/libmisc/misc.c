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
#include <sys/vfs.h>

#include <misc.h>

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

/* Converts string denoted as size into digits */
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

/* Converts human readable size string like "256M" into kb */
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

/* 
    Common reiser4progs exception handler functions. This one returns number of
    specified turned on options.
*/
static int progs_exception_bit_count(
    aal_exception_option_t options,	    /* options to be inspected */
    int start				    /* options will be inspected started from */
) {
    int i, res = 0;
    
    for (i = start; i < aal_log2(EXCEPTION_LAST); i++)
	res += ((1 << i) & options) ? 1 : 0;

    return res;
}

/* This function print turned on options */
static void progs_exception_print_options(aal_exception_option_t options) {
    int i;

    if (progs_exception_bit_count(options, 0) == 0)
	return;
    
    printf("(");
    for (i = 1; i < aal_log2(EXCEPTION_LAST); i++) {
	if ((1 << i) & options) {
	    if (i < aal_log2(EXCEPTION_LAST) - 1 && 
		    progs_exception_bit_count(options, i + 1) > 0)
		printf("%s/", aal_exception_option_string(1 << i));
	    else
		printf("%s)? ", aal_exception_option_string(1 << i));
	}
    }
}

/* This function makes serach for option by its name */
static aal_exception_option_t progs_exception_option_by_name(char *name) {
    int i;
    
    if (!name || aal_strlen(name) == 0)
	return EXCEPTION_UNHANDLED;
    
    for (i = 0; i < aal_log2(EXCEPTION_LAST); i++) {
	char *opt = aal_exception_option_string(1 << i);
	
	/* 
	    Function fgets puts '\n' symbol at end of entered string. Because of 
	    this we should check for strlen(name) == 2.
	*/
	if (aal_strncmp(opt, name, aal_strlen(name)) == 0 || 
		(aal_strlen(name) == 2 && toupper(opt[0]) == toupper(name[0])))
	    return 1 << i;
    }
    
    return EXCEPTION_UNHANDLED;
}

/* This function gets user enter */
static aal_exception_option_t progs_exception_selected_option(void) {
    char str[256];
    
    aal_memset(str, 0, sizeof(str));
    fgets(str, sizeof(str), stdin);

    return progs_exception_option_by_name(str);
}

/* Streams assigned with exception type are stored here */
static void *streams[10];

/* This function sets up exception streams */
void progs_exception_set_stream(
    aal_exception_type_t type,	/* type to be assigned with stream */
    void *stream		/* stream to be assigned */
) {
    streams[type] = stream;
}

/* This function gets exception streams */
void *progs_exception_get_stream(
    aal_exception_type_t type	/* type exception stream will be obtained for */
) {
    return streams[type];
}

/* 
    Common exception handler for all reiser4progs. It implements exception handling 
    in "question-answer" maner and used for all communications with user.
*/
aal_exception_option_t progs_exception_handler(
    aal_exception_t *exception		/* exception to be processed */
) {
    void *stream = stderr;
    aal_exception_option_t opt;
    
    if (exception->type == EXCEPTION_ERROR || 
	exception->type == EXCEPTION_FATAL ||
	exception->type == EXCEPTION_BUG)
        aal_gauge_failed(); 
    else
	aal_gauge_pause();

    if (progs_exception_bit_count(exception->options, 0) == 1) {
	if (!(stream = streams[exception->type]))
	    stream = stderr;
    }
    
    /* Printing exception type */
    if (exception->type != EXCEPTION_BUG)
        fprintf(stream, "%s: ", aal_exception_type_string(exception->type));
	
    /* Printing exception message */
    fprintf(stream, "%s\n", exception->message);
    
    if (progs_exception_bit_count(exception->options, 0) == 1) {
        if (exception->type == EXCEPTION_WARNING || 
		exception->type == EXCEPTION_INFORMATION)
	    aal_gauge_resume();
	    
	return exception->options;
    }
	    
    do {
	progs_exception_print_options(exception->options);
	opt = progs_exception_selected_option();
    } while (opt == EXCEPTION_UNHANDLED);

    if (exception->type == EXCEPTION_WARNING || 
	    exception->type == EXCEPTION_INFORMATION)
	aal_gauge_resume();
	    
    return opt;
}

/* Common gauge handler */
#define GAUGE_BITS_SIZE 4

static inline void progs_gauge_blit(void) {
    static short bitc = 0;
    static const char bits[] = "|/-\\";

    putc(bits[bitc], stderr);
    putc('\b', stderr);
    fflush(stderr);
    bitc++;
    bitc %= GAUGE_BITS_SIZE;
}

/* This functions "draws" gauge header */
static inline void progs_gauge_header(
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
static inline void progs_gauge_footer(
    const char *name,	    /* footer name */
    aal_gauge_type_t type   /* gauge type */
) {
    if (name)
	fputs(name, stderr);
}

/* Common gauge handler */
void progs_gauge_handler(aal_gauge_t *gauge) {
    if (gauge->state == GAUGE_PAUSED) {
	putc('\r', stderr);
	fflush(stderr);
	return;
    }
	
    if (gauge->state == GAUGE_STARTED)
	progs_gauge_header(gauge->name, gauge->type);
	
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
	    progs_gauge_blit();
	    break;
	}
	case GAUGE_SILENT: break;
    }

    if (gauge->state == GAUGE_DONE)
	progs_gauge_footer("done\n", gauge->type);
    
    if (gauge->state == GAUGE_FAILED)
	progs_gauge_footer("failed\n", gauge->type);
	
    fflush(stderr);
}

/*void progs_misc_factory_print(void) {
    reiserfs_plugin_t *plugin = NULL;

    printf("\nKnown plugins are:\n");
    while ((plugin = libreiser4_factory_get_next(plugin)) != NULL)
	printf("%s: %s.\n", plugin->h.label, plugin->h.desc);
    
    printf("\n");
}*/

