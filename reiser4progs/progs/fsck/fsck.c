/*
    fsck.c -- reiser4 filesystem checking and recovering program.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "fsck.h"

static void fsck_print_usage(const char *name) {
    progs_progress("Usage: %s [ options ] FILE\n", name);
    
    progs_progress("Modes:\n"
	"  --check                        consistency checking (default).\n"
	"  --rebuild                      fixes all fs corruptions.\n"
	"Options:\n"
	"  -l | --logfile                 complains into the logfile\n"
	"  -V | --version                 prints the current version.\n"
	"  -? | -h | --help               prints program usage.\n"
	"  -n | --no-log                  makes fsck to not complain.\n"
	"  -q | --quiet                   suppresses the most of the progress information.\n"
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

static int warn_what_will_be_done(reiser4_program_data_t *prog_data, char *host_name) 
{    
    if (fsck_mode(prog_data) == FSCK_CHECK) {
	progs_progress(CHECK_WARNING, host_name);
    } else if (fsck_mode(prog_data) == FSCK_REBUILD) {
	progs_progress(REBUILD_WARNING, host_name);	
    } else if (fsck_mode(prog_data) == FSCK_ROLLBACK) {
	progs_progress("Will rollback all data saved in %s into %s\n", "", host_name);
    }

    progs_progress("Will use %s profile\n", prog_data->profile->label);
 
    return NO_ERROR; 
}

static int fsck_init(reiser4_program_data_t *prog_data, int argc, char *argv[], 
    char **host_name) 
{
    int c, ro = 0;
    static int flag, mode = FSCK_CHECK;
    char *str, *profile_label = NULL;
    FILE *stream;
    aal_exception_streams_t *streams;

    static struct option long_options[] = {
	/* Fsck modes */
	{"check", no_argument, &mode, FSCK_CHECK},
        {"fix", no_argument, &mode, FSCK_REBUILD},
	/* Fsck hidden modes. */
	{"rollback-fsck-changes", no_argument, &mode, FSCK_ROLLBACK},
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
	{"no-journal", required_argument, &flag, FSCK_OPT_NO_JOURNAL},
	/* Fsck hidden options. */
	{"passes-dump", required_argument, 0, 'U'},
        {"rollback-data", required_argument, 0, 'R'},
	{0, 0, 0, 0}
    };

    progs_init();
    *host_name = NULL;
    prog_data->profile = progs_profile_get_default();

    if (argc < 2) {
	fsck_print_usage(argv[0]);
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
		    streams = aal_exception_get_streams();

		    streams->error = stream;
		    streams->warn = stream;
		}
		break;
	    case 'n':
		streams = aal_exception_get_streams();
		streams->info = streams->warn = streams->error = NULL;
		break;
	    case 'U':
		break;
	    case 'R':
		break;
	    case 'f':
		fsck_set_option(FSCK_OPT_FORCE, prog_data);
		break;
	    case 'a':
	    case 'p':
		fsck_set_option(FSCK_OPT_AUTO, prog_data);
		break;
	    case 'v':
		progs_set_verbose();
		break;
	    case 'd':
		profile_label = optarg;
		break;
	    case 'K':
		progs_profile_print_list();
		return NO_ERROR;
	    case 'k':
		progs_plugins_print();
		return NO_ERROR;
	    case 'o':
		str = aal_strsep(&optarg, "=");
		if (!optarg || progs_profile_override_plugin_id_by_name(
		    prog_data->profile, str, optarg)) 
		{
		    progs_fatal("Cannot load a plugin '%s' of the type '%s'.\n", str, optarg);
		    return USER_ERROR;
		}
		break;
	    case 'h': 
	    case '?':
		fsck_print_usage(argv[0]);
		return NO_ERROR;	    
	    case 'V': 
		progs_progress(BANNER("reiser4fsck"));
		return NO_ERROR;	    
	    case 'q': 
		fsck_set_option(FSCK_OPT_QUIET, prog_data);
		break;
	    case 'r':
		break;
	}
    }
    if (profile_label && !(prog_data->profile = progs_profile_find(profile_label))) {
	progs_fatal("Can't find profile by specified label \"%s\".\n", profile_label);
	return USER_ERROR;
    }
    if (optind == argc && profile_label) {
	/* print profile */
	progs_profile_print(prog_data->profile);
	return NO_ERROR;
    } else if (optind != argc - 1) {
	fsck_print_usage(argv[0]);
	return USER_ERROR;
    }
    

    if (mode != FSCK_CHECK) {
	progs_progress("Sorry, only check mode is supported yet.\n");
	return USER_ERROR;
    }
    
    fsck_mode(prog_data) = mode;

    *host_name = argv[optind];
    
    if (progs_is_mounted(*host_name)) {
	ro = progs_is_mounted_ro(*host_name);
	if (!ro) {
	    progs_progress ("Partition %s is mounted w/ write permissions, cannot fsck it\n",
                *host_name);
	    return USER_ERROR;
	}
	    
	if (mode == FSCK_REBUILD) {	    
	} else {
	}
    }
    
    
    return warn_what_will_be_done(prog_data, argv[optind]);
}

int fsck_check_fs(reiser4_program_data_t *prog_data, reiserfs_fs_t *fs) {
    int retval;
    time_t t;
    
    time(&t);
    progs_progress ("###########\nreiserfsck --check started at %s###########\n", 
	ctime (&t));
    
    if (progs_fs_check(fs)) {
	progs_progress("Filesystem check failed. File system need to be rebuild\n");
	return OPERATION_ERROR;
    }

    progs_progress ("###########\nreiserfsck --check finished at %s###########\n", 
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
    reiser4_program_data_t prog_data;
    fsck_data_t fsck_data;
    
    char *host_name;
    reiserfs_fs_t *fs;
    
    memset(&prog_data, 0, sizeof(prog_data));
    memset(&fsck_data, 0, sizeof(fsck_data));
    memset(&fs, 0, sizeof(fs));
    prog_data.data = &fsck_data;    

    if (libreiser4_init()) {
	progs_fatal("Can't initialize libreiser4.\n");
	exit(OPERATION_ERROR);
    }
   
    if (((exit_code = fsck_init(&prog_data, argc, argv, &host_name)) != NO_ERROR) || 
	host_name == NULL) 
    {
	goto free_device;
    }

    if (!(prog_data.host_device = aal_file_open(host_name, REISERFS_DEFAULT_BLOCKSIZE, 
	O_RDWR))) 
    {
	progs_fatal("Can't open the partition %s: %s\n", host_name, 
	    strerror(errno));
	exit_code = OPERATION_ERROR;
	goto free_libreiser4;
    }
 
    if (!(fs = reiserfs_fs_open(prog_data.host_device, prog_data.host_device, 1))) {
	progs_fatal("Can't open filesystem on %s.\n", host_name);
	goto free_device;
    }
    
    switch (fsck_mode(&prog_data)) {
	case FSCK_CHECK:
	    exit_code = fsck_check_fs(&prog_data, fs);
	    break;
	case FSCK_REBUILD:
	    exit_code = fsck_rebuild_fs();
	    break;
	case FSCK_ROLLBACK:
	    exit_code = fsck_rollback();
	    break;
    }
	
    progs_progress("Synchronizing...");
    
    if (reiserfs_fs_sync(fs)) {
	progs_fatal("Can't synchronize created filesystem.\n");
	goto free_fs;
    }

    if (aal_device_sync(prog_data.host_device)) {
	progs_fatal("Can't synchronize device %s.", 
	    aal_device_name(prog_data.host_device));
	goto free_fs;
    }

    progs_progress("done\n");

free_fs:
    reiserfs_fs_close(fs);
free_device:
    aal_file_close(prog_data.host_device);
free_libreiser4:
    libreiser4_done();

    return exit_code;
}

