/*
    fsck.c -- reiser4 filesystem checking and recovering program.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#include <fsck/fsck.h>

static void fsck_print_usage(const char *name) {
    fprintf(stderr, "Usage: %s [ options ] FILE\n", name);
    
    fprintf(stderr, "Modes:\n"
	"  --check                        consistency checking (default).\n"
	"  --fix                          fix all fs corruptions.\n"
	"Options:\n"
	"  -l | --logfile                 complains into the logfile\n"
	"  -V | --version                 prints the current version.\n"
	"  -? | -h | --help               prints program usage.\n"
	"  -n | --no-log                  makes fsck to not complain.\n"
	"  -q | --quiet                   reduces the progress infermation.\n"
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

static int fsck_init(reiser4_program_data_t *prog_data, int argc, char *argv[], 
    char **host_name) 
{
    int c;
    static int mode, flag;
    char *profile_label, *str;

    static struct option long_options[] = {
	/* Fsck modes */
	{"check", no_argument, &mode, FSCK_CHECK},
        {"fix", no_argument, &mode, FSCK_FIX},
	/* Fsck hidden modes. */
	{"rollback-fsck-changes", no_argument, &mode, FSCK_ROLLBACK},
	/* Fsck options */
	{"logfile", required_argument, 0, 'l'},
	{"version", no_argument, NULL, 'V'},
	{"help", no_argument, NULL, 'h'},
	{"quiet", no_argument, NULL, 'q'},
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

    if (argc < 2) {
	fsck_print_usage(argv[0]);
	return NO_ERROR;
    }
    while ((c = getopt_long_only(argc, argv, "l:Vhqapfvd:Kko:U:R:r?", long_options, (int *)0)) 
	!= EOF) 
    {
	switch (c) {
	    case 'l':
		break;
	    case 'U':
		break;
	    case 'R':
		break;
	    case 'f':
		break;
	    case 'a':
	    case 'p':
		break;
	    case 'v':
		break;
	    case 'd':
		profile_label = optarg;
		break;
	    case 'K':
		progs_print_profile_list();
		return NO_ERROR;
	    case 'k':
		progs_print_plugins();
		return NO_ERROR;
	    case 'o':
		str = aal_strsep(&optarg, "=");
		if (!optarg || progs_profile_override_plugin_id_by_name(
		    prog_data->profile, str, optarg)) 
		{
		    prog_error("Cannot load a plugin '%s' of the type '%s'.\n", str, optarg);
		    return USER_ERROR;
		}
		break;
	    case 'h': 
	    case '?':
		fsck_print_usage(argv[0]);
		return NO_ERROR;	    
	    case 'V': 
		prog_info("%s %s\n", argv[0], VERSION);
		return NO_ERROR;	    
	    case 'q': 
		force = 1;
		break;
	    case 'r':
		break;
	}
    }

}

int main(int argc, char *argv[]) {
    int c, error, force = 0, replay = 1;
    int check = 0, rebuild = 0;
    char *host_dev, *profile_label = "default40";
    
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_profile_t *profile;
    
    while ((c = getopt_long_only(argc, argv, "uhvp:kqcr", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'u': 
	    case 'h': {
		fsck_print_usage(argv[0]);
		return 0;
	    }
	    case 'v': {
		printf("%s %s\n", argv[0], VERSION);
		return 0;
	    }
	    case 'q': {
		force = 1;
		break;
	    }
	    case 'p': {
		profile_label = optarg;
		break;
	    }
	    case 'k': {
		progs_list_profile();
		return 0;
	    }
	    case 'w': {
		replay = 0;
	        break;
	    }
	    case 'c': {
		check = 1;
	        break;
	    }
	    case 'r': {
		rebuild = 1;
	        break;
	    }
	    case '?': {
	        fsck_print_usage(argv[0]);
	        return 0xfe;
	    }
	}
    }

    if (optind >= argc) {
	fsck_print_usage(argv[0]);
	return 0xfe;
    }
    
    if (!(profile = progs_find_profile(profile_label))) {
	aal_throw_error(EO_OK, "Can't find profile by its label \"%s\".", profile_label);
	return 0xff;
    }
    
    if (!check && !rebuild)
	check = 1;
    
    host_dev = argv[optind++];
    
    /* Checking given device for validness */
    if (!progs_misc_dev_check(host_dev)) {
	aal_throw_error(EO_OK, "Device %s doesn't exists or invalid.", host_dev);
	return 0xfe;
    }

    if (!(device = aal_file_open(host_dev, REISERFS_DEFAULT_BLOCKSIZE, O_RDWR))) {
	char *error = strerror(errno);
	
	aal_throw_error(EO_OK, "Can't open device %s. %s.", host_dev, error);
	goto error;
    }
    
    if (libreiser4_init()) {
	aal_throw_error(EO_OK, "Can't initialize libreiser4.");
	goto error_free_device;
    }
    
    if (check && rebuild) {
	aal_throw_error(EO_OK, "Invalid mode. Please select one of --check or --rebuild.");
	goto error_free_libreiser4;
    }
    
    if (rebuild) {
	aal_throw_error(EO_OK, "Sorry, filesystem rebuilding not supported yet!");
	goto error_free_libreiser4;
    }
    
    if (!force) {
	if (!(c = progs_misc_choose_propose("ynYN", "Please select (y/n) ", 
		"Do you realy want to run filesystem check on %s? (y/n) ", host_dev)))
	    goto error_free_libreiser4;
	
	if (c == 'n' || c == 'N')
	    goto error_free_libreiser4;
    }

    /*
	Most probably filesystem will not be openable, due to incorrect 
	control structures. So, we should add one more argument to 
	reiserfs_fs_open function, which will force it do not any checks 
	on control structures.
    */
    if (!(fs = reiserfs_fs_open(device, device, replay))) {
	aal_throw_error(EO_OK, "Can't open filesystem on %s.", host_dev);
	goto error_free_libreiser4;
    }
    
    if (check)
	fprintf(stderr, "Checking reiser4 with \"%s\" profile...", profile->label);
    else
	fprintf(stderr, "Rebuilding reiser4 with \"%s\" profile...", profile->label);
    
    fflush(stderr);

    
    if (check) {
	if (reiserfs_fs_check(fs)) {
	    aal_throw_error(EO_OK, "Can't check filesystem on %s.", host_dev);
	    goto error_free_fs;
	}
    } else {
	/* Rebuilding will be here */
    }

    fprintf(stderr, "done\n");

    fprintf(stderr, "Synchronizing...");
    
    if (reiserfs_fs_sync(fs)) {
	aal_throw_error(EO_OK, "Can't synchronize created filesystem.");
	goto error_free_fs;
    }

    if (aal_device_sync(device)) {
	aal_throw_error(EO_OK, "Can't synchronize device %s.", aal_device_name(device));
	goto error_free_fs;
    }

    fprintf(stderr, "done\n");
    
    reiserfs_fs_close(fs);
    libreiser4_done();
    aal_file_close(device);
    
    return 0;

error_free_fs:
    reiserfs_fs_close(fs);
error_free_libreiser4:
    libreiser4_done();
error_free_device:
    aal_file_close(device);
error:
    return 0xff;
}

