/*
    mkfs.c -- the program to create reiser4 filesystem.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <mkfs.h>

static void mkfs_print_usage(char *name) {
    fprintf(stderr, "\nUsage: %s [ options ] FILE [ size[K|M|G] ]\n", name);
    
    fprintf(stderr, "Options:\n"
	"  -b | --block-size N            block size, 4096 by default,\n"
	"                                 other are not supported yet.\n"
	"  -l | --label LABEL             volume label, lets the filesystem\n"
	"                                 to be mounted by it.\n"
	"  -d | --uuid UUID               universally unique identifier, lets\n"
	"                                 the filesystem to be mounted by it.\n"
	"  -f | --force                   specified once, make mkreiserfs the whole\n"
	"                                 disk, not block device or mounted partition;\n"
	"                                 specified twice, do not ask for confirmation.\n"
	"  -v | --version                 prints current version.\n"
	"  -? | -h | --help               prints program usage.\n\n"
	"  Plugins options:\n"
	"  -K | --known-profiles          prints known profiles.\n"
	"  -k | --known-plugins           prints known plugins.\n"	
	"  -p | --profile PROFILE         profile 'PROFILE' to be used or printed.\n"
	"  -o | --plugin 'TYPE=plugin'    override the default plugin of the type 'TYPE'\n"
	"                                 by the plugin 'plugin'.\n\n");
}

int progs_can_be_formatted(reiser4_program_data_t *prog_data, const char *dev) {
    struct stat dev_stat;
    int need_force = 0;
    
    if (!prog_data || !dev)
	return 0;

    if (stat(dev, &dev_stat) == -1) {
	prog_error("'%s' cannot be stated.\n", dev);
	return -1;
    }
    if (progs_is_mounted(dev)) {
	prog_fatal("'%s' looks mounted.\n", dev);
	need_force = 1;	
    }
    if (!S_ISBLK(dev_stat.st_mode)) {
	prog_fatal("'%s' is not a block device.\n", dev);
	need_force = 1;
    } else 
	if ((IDE_DISK_MAJOR(MAJOR(dev_stat.st_rdev)) && MINOR(dev_stat.st_rdev) % 64 == 0) ||
	    (SCSI_BLK_MAJOR(MAJOR(dev_stat.st_rdev)) && MINOR(dev_stat.st_rdev) % 16 == 0))
    {
	prog_fatal("'%s' is an entire harddrive, not just one partition.\n", dev);
	need_force = 1;
    }
    if (need_force && !mkfs_test_force_all(prog_data)) {
	prog_fatal("Use -f to force over.\n");
	return 0;
    }

    return 1;
}

int mkfs_init(reiser4_program_data_t *prog_data, int argc, char *argv[], char **host_name) 
{
    char *profile_label = NULL;
    int c, quite;
    char uuid[37];
    int64_t parsed;
    char *str;
	
    static struct option long_options[] = {
	{"profile", required_argument, NULL, 'p'},
	{"known-profiles", no_argument, NULL, 'K'},
	{"known-plugins", no_argument, NULL, 'k'},
	{"plugin", required_argument, NULL, 'o'},
	{"block-size", required_argument, NULL, 'b'},
	{"label", required_argument, NULL, 'l'},
	{"uuid", required_argument, NULL, 'd'},
	{"force", no_argument, NULL, 'f'},
	{"version", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},	
	{"quiet", no_argument, NULL, 'q'},
	{0, 0, 0, 0}
    };

    memset(uuid, 0, sizeof(uuid)); 
    mkfs_data(prog_data)->blocksize = REISERFS_DEFAULT_BLOCKSIZE;
    aal_exception_init_streams(NULL);
    *host_name = NULL;
    
    if (argc < 2) {
	mkfs_print_usage(argv[0]);
	return USER_ERROR;
    }

    prog_data->profile = progs_get_default_profile();
	
    while ((c = getopt_long_only(argc, argv, "p:Kko:b:l:d:fvh?", long_options, (int *)0)) 
	!= EOF) 
    {
	switch (c) {
	    case 'p': 
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
	    case 'b': 
	        if ((parsed = progs_misc_size_parse(optarg)) < 0) {
		    prog_error("Invalid blocksize was specified (%s).\n", optarg);
		    return USER_ERROR;
		}
		
		mkfs_data(prog_data)->blocksize = parsed;
		if (!mkfs_data(prog_data)->blocksize || 
		    !aal_pow_of_two(mkfs_data(prog_data)->blocksize)) 
		{
		    prog_error("Invalid block size was specified (%d). It must "
			"be not 0 and power of two.\n", mkfs_data(prog_data)->blocksize);
		    return USER_ERROR;
		}
		break;
	    case 'l': 
		strncpy(mkfs_data(prog_data)->label, optarg, 
		    sizeof(mkfs_data(prog_data)->label) - 1);
	        break;		
	    case 'd': 
		if ((strlen(optarg) != 36)) {
		    prog_error("Invalid uuid was specified (%s).\n", optarg);
		    return USER_ERROR;
		}
		
		strncpy(uuid, optarg, sizeof(uuid) - 1);
		if (uuid_parse(uuid, mkfs_data(prog_data)->uuid) < 0) {
		    prog_error("Invalid uuid was specified (%s).\n", optarg);
		    return USER_ERROR; 
		}
		break;
	    case 'f':
		mkfs_test_force_all(prog_data) == 1 ? mkfs_set_force_quiet(prog_data) : 
		    mkfs_set_force_all(prog_data);
		break;
	    case 'h':
	    case '?':
		mkfs_print_usage(argv[0]);
		return NO_ERROR;
	    case 'v': 
		printf("%s %s\n", argv[0], VERSION);
		return NO_ERROR;
	}
    }

    if (profile_label && !(prog_data->profile = progs_find_profile(profile_label))) {
	prog_error("Can't find profile by specified label \"%s\".\n", profile_label);
	return USER_ERROR;
    }

    if (optind == argc - 2) {
	if ((parsed = progs_misc_size_parse(argv[argc - 1])) < 0) {
	    prog_error("Invalid filesystem size (%s).\n", argv[argc - 1]);
	    return USER_ERROR;
	}
	mkfs_data(prog_data)->host_size = parsed / mkfs_data(prog_data)->blocksize;
	optind++;
    } else if (optind == argc && profile_label) {
	/* print profile */
	progs_print_profile(prog_data->profile);
	return NO_ERROR;
    } else if (optind != argc - 1) {
	mkfs_print_usage(argv[0]);
	return USER_ERROR;
    }
    
    if (!progs_can_be_formatted(prog_data, argv[optind])) {
	return USER_ERROR;
    }

    *host_name = argv[optind];

    return NO_ERROR;
}

int main(int argc, char *argv[]) {
    int exit_code = NO_ERROR;
    reiser4_program_data_t prog_data;
    mkfs_data_t mkfs_data;
    
    reiserfs_fs_t *fs;
    char *host_name;
    uint32_t dev_len;

    memset(&prog_data, 0, sizeof(prog_data));
    memset(&mkfs_data, 0, sizeof(mkfs_data));
    prog_data.data = &mkfs_data;
        
    if (libreiser4_init()) {
	prog_error("Can't initialize libreiser4.\n");
	exit(OPERATION_ERROR);
    }

    if (((exit_code = mkfs_init(&prog_data, argc, argv, &host_name)) != NO_ERROR) || 
	host_name == NULL) 
    {
	goto free_libreiser4;
    }

    if (!(prog_data.host_device = aal_file_open(host_name, 
	mkfs_data(&prog_data)->blocksize, O_RDWR))) 
    {	
	prog_error("Can't open the partition %s: %s\n", host_name, 
	    strerror(errno));
	exit_code = OPERATION_ERROR;
	goto free_libreiser4;
    }
    
    dev_len = aal_device_len(prog_data.host_device);
    
    if (!mkfs_data(&prog_data)->host_size)
        mkfs_data(&prog_data)->host_size = dev_len;
    else if (mkfs_data(&prog_data)->host_size > dev_len) {
	prog_error("Specified fs size (%u) is greater then the partition "
	    "size (%u).\n", mkfs_data(&prog_data)->host_size, dev_len);
	exit_code = USER_ERROR;
	goto free_device;
    }    
        
    if (!mkfs_test_force_quiet(&prog_data)) {
	prog_progress("YOU SHOULD REBOOT AFTER FDISK!\n");
	if (prog_ask(EO_YES | EO_NO, EO_NO, "ALL DATA WILL BE LOST ON %s. "
	    "Continue?", aal_device_name(prog_data.host_device)) != EO_YES)
	{	    
	    goto free_device;
	}
    }

    prog_progress("Creating reiser4 with \"%s\" profile...", 
	prog_data.profile->label);
    
    if (!(fs = reiserfs_fs_create(prog_data.profile, prog_data.host_device, 
	mkfs_data(&prog_data)->blocksize,  mkfs_data(&prog_data)->uuid, 
	mkfs_data(&prog_data)->label, mkfs_data(&prog_data)->host_size, 
	prog_data.host_device, NULL))) 
    {
	prog_error("Can't create filesystem on %s.\n", 
	    aal_device_name(prog_data.host_device));
	exit_code = OPERATION_ERROR;
	goto free_device;
    }
    prog_progress("done\n");

    prog_progress("Synchronizing...");
    
    if (reiserfs_fs_sync(fs)) {
	prog_error("Can't synchronize created filesystem.\n");
	exit_code = OPERATION_ERROR;
	goto free_fs;
    }

    if (aal_device_sync(prog_data.host_device)) {
	prog_error("Can't synchronize the partition %s.\n", 
	    aal_device_name(prog_data.host_device));
	exit_code = OPERATION_ERROR;
	goto free_fs;
    }

    prog_progress("done\n");
    
free_fs:
    reiserfs_fs_close(fs);
free_device:
    aal_file_close(prog_data.host_device);
free_libreiser4:
    libreiser4_done();

    return exit_code;
}
