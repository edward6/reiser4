/*
    fsck.c -- reiser4 filesystem checking and recovering program.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>

#include <reiser4/reiser4.h>
#include <progs/misc.h>

static void fsck_print_usage(void) {
    fprintf(stderr, "Usage: fsck.reiser4 [ options ] FILE\n");
    
    fprintf(stderr, "Options:\n"
	"  -v | --version                 prints current version.\n"
	"  -u | -h | --usage | --help     prints program usage.\n"
	"  -q | --quiet                   forces filesystem check without\n"
	"                                 warning message.\n"
	"  -p | --profile                 profile to be used for recovering.\n"
	"  -k | --known-profiles          prints known profiles.\n"
	"  -n | --no-journal              do not replay the journal.\n"
	"  -l | --cache-limit             tree-cache limit in form -l 256M.\n"
	"  -r                             ignored.\n"
	"  -c | --check                   consistency check (default).\n"
	"  -b | --rebuild                 rebuild internal tree.\n");
}

int main(int argc, char *argv[]) {
    uint64_t limit = 0;
    int check = 0, rebuild = 0;
    int c, error, quiet = 0, replay = 1;
    char *host_dev, *profile_label = "default40";
    
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_profile_t *profile;
    
    static struct option long_options[] = {
	{"version", no_argument, NULL, 'v'},
	{"usage", no_argument, NULL, 'u'},
	{"help", no_argument, NULL, 'h'},
	{"quiet", no_argument, NULL, 'q'},
	{"profile", no_argument, NULL, 'p'},
	{"known-profiles", no_argument, NULL, 'k'},
	{"no-journal", no_argument, NULL, 'n'},
	{"check", no_argument, NULL, 'c'},
	{"rebuild", no_argument, NULL, 'b'},
	{"ignored", no_argument, NULL, 'r'},
	{"cache-limit", no_argument, NULL, 'l'},
	{0, 0, 0, 0}
    };
    
    if (argc < 2) {
	fsck_print_usage();
	return ERROR_USER;
    }

    aal_exception_set_handler(__progs_exception_handler);
    
    while ((c = getopt_long_only(argc, argv, "uhvp:kqcnbrl:", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'u': 
	    case 'h': {
		fsck_print_usage();
		return ERROR_NONE;
	    }
	    case 'v': {
		printf("%s %s\n", argv[0], VERSION);
		return ERROR_NONE;
	    }
	    case 'q': {
		quiet = 1;
		break;
	    }
	    case 'p': {
		profile_label = optarg;
		break;
	    }
	    case 'k': {
		progs_misc_profile_list();
		return ERROR_NONE;
	    }
	    case 'n': {
		replay = 0;
	        break;
	    }
	    case 'c': {
		check = 1;
	        break;
	    }
	    case 'b': {
		rebuild = 1;
	        break;
	    }
	    case 'l': {
		int error;
		
		/* Parsing tree cache limit */
		if (!(limit = (progs_misc_size_parse(optarg, 
		    &error))) && error) 
		{
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Invalid tree-cache limit (%s).", optarg);
		    return ERROR_USER;
		}
		break;
	    }
	    case 'r': break;
	    case '?': {
	        fsck_print_usage();
	        return ERROR_USER;
	    }
	}
    }

    if (optind >= argc) {
	fsck_print_usage();
	return ERROR_USER;
    }
    
    if (!(profile = progs_misc_profile_find(profile_label))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find profile by its label \"%s\".", profile_label);
	return ERROR_PROG;
    }
    
    if (!check && !rebuild)
	check = 1;
    
    host_dev = argv[optind++];

    if (progs_misc_dev_mounted(host_dev, "rw")) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Device \"%s\" is mounted with write permisions. "
	    "Cannot fsck it.", host_dev);
	goto error;
    }
    
    if (!(device = aal_file_open(host_dev, 
	REISERFS_DEFAULT_BLOCKSIZE, O_RDWR))) 
    {
	char *error = strerror(errno);
	
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s. %s.", host_dev, error);
	goto error;
    }
    
    limit /= device->blocksize;
    
    if (libreiser4_init(limit)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	goto error_free_device;
    }
    
    if (check && rebuild) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid mode. Please select one of --check or --rebuild.");
	goto error_free_libreiser4;
    }
    
    if (rebuild) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, filesystem rebuilding not supported yet!");
	goto error_free_libreiser4;
    }
    
    if (!quiet) {
	if (aal_exception_throw(EXCEPTION_INFORMATION, EXCEPTION_YESNO, 
		"All data on \"%s\" will be lost.", host_dev) == EXCEPTION_NO)
	    goto error_free_libreiser4;
    }

    /*
	Most probably filesystem will not be openable, due to incorrect control 
	structures. So, we should add one more argument to reiserfs_fs_open function, 
	which will force it do not any checks on control structures.
    */
    if (!(fs = reiserfs_fs_open(device, device, replay))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open filesystem on %s.", host_dev);
	goto error_free_libreiser4;
    }
    
    if (check)
	fprintf(stderr, "Checking reiser4 with \"%s\" profile...", profile->label);
    else
	fprintf(stderr, "Rebuilding reiser4 with \"%s\" profile...", profile->label);
    
    fflush(stderr);

    
    if (check) {
	if (reiserfs_fs_check(fs)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't check filesystem on %s.", host_dev);
	    goto error_free_fs;
	}
    } else {
	/* Rebuilding will be here */
    }

    fprintf(stderr, "done\n");

    fprintf(stderr, "Synchronizing...");
    
    if (reiserfs_fs_sync(fs)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize created filesystem.");
	goto error_free_fs;
    }

    if (aal_device_sync(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize device %s.", aal_device_name(device));
	goto error_free_fs;
    }

    fprintf(stderr, "done\n");
    
    reiserfs_fs_close(fs);
    libreiser4_done();
    aal_file_close(device);
    
    return ERROR_NONE;

error_free_fs:
    reiserfs_fs_close(fs);
error_free_libreiser4:
    libreiser4_done();
error_free_device:
    aal_file_close(device);
error:
    return ERROR_PROG;
}

