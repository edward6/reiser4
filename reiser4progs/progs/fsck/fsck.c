/*
    fsck.c -- reiser4 filesystem checking and recovering program.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <fsck.h>

static void fsck_print_usage() {
    fprintf(stderr, BANNER("fsck.reiser4"));
    fprintf(stderr, "Usage: fsck.reiser4 [ options ] FILE\n");
    
    fprintf(stderr, "Modes:\n"
	"  --check                        consistency checking (default).\n"
	"  --rebuild                      fixes all fs corruptions.\n"
	"Options:\n"
	"  -l | --logfile                 complains into the logfile\n"
	"  -V | --version                 prints the current version.\n"
	"  -? | -h | --help               prints program usage.\n"
	"  -n | --no-log                  makes fsck to not complain.\n"
	"  -q | --quiet                   suppresses the most of the progress.\n"
	"  -a | -p | --auto | --preen     automatically checks the file system\n"
        "                                 without any questions.\n"
	"  -f | --force                   forces checking even if the file system\n"
        "                                 seems clean.\n"
	"  -v | --verbose                 makes fsck to be verbose.\n"
	"  -r                             ignored.\n"
	"Plugins options:\n"
	"  -K | --known-profiles          prints known profiles.\n"
	"  -k | --known-plugins           prints known plugins.\n"	
	"  -d | --default PROFILE         profile 'PROFILE' to be used or printed.\n"
	"  -o | --override 'TYPE=plugin'  overrides the default plugin of the type 'TYPE'\n"
	"                                 by the plugin 'plugin'.\n"
	"Expert options:\n"
	"  --no-journal                   does not open nor replay journal.\n");
}

#define REBUILD_WARNING \
"  *************************************************************\n\
  **     This is an experimental version of reiser4fsck.     **\n\
  **                 MAKE A BACKUP FIRST!                    **\n\
  ** Do not run rebuild unless something  is broken.  If you **\n\
  ** have bad sectors on a drive it is usually a bad idea to **\n\
  ** continue using it.  Then  you  probably  should  get  a **\n\
  ** working hard drive,  copy the file system from  the bad **\n\
  ** drive  to the good one -- dd_rescue is  a good tool for **\n\
  ** that -- and only then run this program.If you are using **\n\
  ** the latest reiser4progs and  it fails  please email bug **\n\
  ** reports to reiserfs-list@namesys.com, providing as much **\n\
  ** information  as  possible  --  your  hardware,  kernel, **\n\
  ** patches, settings, all reiser4fsck messages  (including **\n\
  ** version), the reiser4fsck  logfile,  check  the  syslog **\n\
  ** file for  any  related information.                     **\n\
  ** If you would like advice on using this program, support **\n\
  ** is available  for $25 at  www.namesys.com/support.html. **\n\
  *************************************************************\n\
\nWill fix the filesystem on (%s)\n"

#define CHECK_WARNING \
"  *************************************************************\n\
  ** If you are using the latest reiser4progs and  it fails **\n\
  ** please  email bug reports to reiserfs-list@namesys.com, **\n\
  ** providing  as  much  information  as  possible --  your **\n\
  ** hardware,  kernel,  patches,  settings,  all  reiserfsk **\n\
  ** messages  (including version), the reiser4fsck logfile, **\n\
  ** check  the  syslog file  for  any  related information. **\n\
  ** If you would like advice on using this program, support **\n\
  ** is available  for $25 at  www.namesys.com/support.html. **\n\
  *************************************************************\n\
\nWill check consistency of the filesystem on (%s)\n"

static int fsck_warn_what_will_be_done(repair_data_t *data, char *host_name) 
{    
    if (repair_mode(data) == REPAIR_CHECK) {
	fprintf(stderr, CHECK_WARNING, host_name);
    } else if (repair_mode(data) == REPAIR_REBUILD) {
	fprintf(stderr, REBUILD_WARNING, host_name);	
    } else if (repair_mode(data) == REPAIR_ROLLBACK) {
	fprintf(stderr, "Will rollback all data saved in %s into %s\n", "", host_name);
    }

    fprintf(stderr, "Will use %s profile\n", data->profile->label);

 
    return NO_ERROR; 
}

static void fsck_init_streams(repair_data_t *data) {
    progs_exception_set_stream(EXCEPTION_WARNING, stderr);    
    progs_exception_set_stream(EXCEPTION_INFORMATION, stderr);
    progs_exception_set_stream(EXCEPTION_ERROR, stderr);
    progs_exception_set_stream(EXCEPTION_FATAL, stderr);
    progs_exception_set_stream(EXCEPTION_BUG, stderr);
    data->logfile = NULL;
}

static int fsck_init(repair_data_t *data, int argc, char *argv[], 
    char **host_name) 
{
    int c, ro = 0;
    static int flag, mode = REPAIR_CHECK;
    char *str, *profile_label = NULL;
    FILE *stream;

    static struct option long_options[] = {
	/* Fsck modes */
	{"check", no_argument, &mode, REPAIR_CHECK},
        {"fix", no_argument, &mode, REPAIR_REBUILD},
	/* Fsck hidden modes. */
	{"rollback-fsck-changes", no_argument, &mode, REPAIR_ROLLBACK},
	/* Fsck options */
	{"logfile", required_argument, 0, 'l'},
	{"version", no_argument, NULL, 'V'},
	{"help", no_argument, NULL, 'h'},
	{"quiet", no_argument, NULL, 'q'},
	{"no-log", no_argument, NULL, 'n'},
	{"auto", no_argument, NULL, 'a'},
	{"preen", no_argument, NULL, 'p'},
	{"force", no_argument, NULL, 'f'},
	{"verbose", no_argument, NULL, 'v'},
	{"default", required_argument, NULL, 'd'},
	{"known-profiles", no_argument, NULL, 'K'},
	{"known-plugins", no_argument, NULL, 'k'},
	{"override", required_argument, NULL, 'o'},
	{"no-journal", required_argument, &flag, REPAIR_OPT_NO_JOURNAL},
	/* Fsck hidden options. */
	{"passes-dump", required_argument, 0, 'U'},
        {"rollback-data", required_argument, 0, 'R'},
	{0, 0, 0, 0}
    };

    *host_name = NULL;
    data->profile = progs_profile_default();
    fsck_init_streams(data);

    if (argc < 2) {
	fsck_print_usage();
	return USER_ERROR;
    }

    while ((c = getopt_long(argc, argv, "l:Vhnqapfvd:Kko:U:R:r?", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'l':
		if ((stream = fopen(optarg, "w")) == NULL)
		    progs_fatal("Cannot not open the logfile \'%s\'\n", optarg);
		else {
		    data->logfile = stream;
		    progs_exception_set_stream(EXCEPTION_ERROR, stream);
		    progs_exception_set_stream(EXCEPTION_WARNING, stream);
		}
		break;
	    case 'n':
		progs_exception_set_stream(EXCEPTION_INFORMATION, NULL);
		progs_exception_set_stream(EXCEPTION_WARNING, NULL);
		progs_exception_set_stream(EXCEPTION_ERROR, NULL);
		break;
	    case 'U':
		break;
	    case 'R':
		break;
	    case 'f':
		repair_set_option(REPAIR_OPT_FORCE, data);
		break;
	    case 'a':
	    case 'p':
		repair_set_option(REPAIR_OPT_AUTO, data);
		break;
	    case 'v':
		repair_set_option(REPAIR_OPT_VERBOSE, data);
		break;
	    case 'd':
		profile_label = optarg;
		break;
	    case 'k':
		progs_misc_factory_list();
		return NO_ERROR;
	    case 'K':
		progs_profile_list();
		return NO_ERROR;
	    case 'o':
		str = aal_strsep(&optarg, "=");
		if (!optarg || progs_profile_override(data->profile, str, optarg)) {
		    progs_fatal("Cannot load a plugin '%s' of the type '%s'.\n", str, optarg);
		    return USER_ERROR;
		}
		break;
	    case 'h': 
	    case '?':
		fsck_print_usage();
		return NO_ERROR;	    
	    case 'V': 
		fprintf(stderr, BANNER("reiser4fsck"));
		return NO_ERROR;	    
	    case 'q': 
		repair_set_option(REPAIR_OPT_QUIET, data);
		break;
	    case 'r':
		break;
	}
    }
    if (profile_label && !(data->profile = progs_profile_find(profile_label))) {
	progs_fatal("Can't find profile by specified label \"%s\".\n", profile_label);
	return USER_ERROR;
    }
    if (optind == argc && profile_label) {
	/* print profile */
	progs_profile_print(data->profile);
	return NO_ERROR;
    } else if (optind != argc - 1) {
	fsck_print_usage();
	return USER_ERROR;
    }
    

    if (mode != REPAIR_CHECK) {
	fprintf(stderr, "Sorry, only check mode is supported yet.\n");
	return USER_ERROR;
    }
    
    repair_mode(data) = mode;

    *host_name = argv[optind];
    
    if (progs_misc_dev_mounted(*host_name, NULL)) {
	if (!progs_misc_dev_mounted(*host_name, "ro")) {
	    fprintf(stderr, "Partition %s is mounted w/ write permissions, cannot fsck it\n",
                *host_name);
	    return USER_ERROR;
	}
	    
	if (mode == REPAIR_REBUILD) {	    
	} else {
	}
    }
    
    
    return fsck_warn_what_will_be_done(data, argv[optind]);
}

int fsck_check_fs(repair_data_t *data, reiser4_fs_t *fs) {
    int retval;
    time_t t;
    
    time(&t);
    fprintf(stderr, "###########\nreiserfsck --check started at %s###########\n", 
	ctime (&t));
    
    if (repair_fs_check(fs)) {
	fprintf(stderr, "Filesystem check failed. File system need to be rebuild\n");
	return OPERATION_ERROR;
    }

    fprintf(stderr, "###########\nreiserfsck --check finished at %s###########\n", 
	ctime (&t));
    return NO_ERROR;
}

int fsck_rebuild_fs() {
    return NO_ERROR;
}

int fsck_rollback() {
    return NO_ERROR;
}

int main(int argc, char *argv[]) {
    int exit_code = NO_ERROR;
    repair_data_t data;
    aal_device_t *host_device = NULL;
    
    char *host_name;
    reiser4_fs_t *fs;
    
    memset(&data, 0, sizeof(data));
    memset(&fs, 0, sizeof(fs));

    if (libreiser4_init(0)) {
	progs_fatal("Can't initialize libreiser4.\n");
	exit(OPERATION_ERROR);
    }
   
    if (((exit_code = fsck_init(&data, argc, argv, &host_name)) != NO_ERROR) || 
	host_name == NULL) 
    {
	goto free_device;
    }

    if (!(host_device = aal_file_open(host_name, REISER4_DEFAULT_BLOCKSIZE, 
	O_RDWR))) 
    {
	progs_fatal("Can't open the partition %s: %s\n", host_name, 
	    strerror(errno));
	exit_code = OPERATION_ERROR;
	goto free_libreiser4;
    }
 
    if (!(fs = reiser4_fs_open(host_device, host_device, 1))) {
	progs_fatal("Can't open filesystem on %s.\n", host_name);
	goto free_device;
    }

    fs->data = &data;    
    
    switch (repair_mode(&data)) {
	case REPAIR_CHECK:
	    exit_code = fsck_check_fs(&data, fs);
	    break;
	case REPAIR_REBUILD:
	    exit_code = fsck_rebuild_fs();
	    break;
	case REPAIR_ROLLBACK:
	    exit_code = fsck_rollback();
	    break;
    }
	
    fprintf(stderr, "Synchronizing...");
    
    if (reiser4_fs_sync(fs)) {
	progs_fatal("Can't synchronize the filesystem.\n");
	goto free_fs;
    }

    if (aal_device_sync(host_device)) {
	progs_fatal("Can't synchronize device %s.", 
	    aal_device_name(host_device));
	goto free_fs;
    }

    fprintf(stderr, "done\n");

free_fs:
    reiser4_fs_close(fs);
free_device:
    aal_file_close(host_device);
free_libreiser4:
    libreiser4_done();

    return exit_code;
}

