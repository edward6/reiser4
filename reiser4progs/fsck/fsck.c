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

#include <progs/progsmisc.h>

static void fsck_print_usage(void) {
    fprintf(stderr, "Usage: fsck.reiser4 [ options ] FILE\n");
    
    fprintf(stderr, "Options:\n"
	"  -v | --version                 prints current version.\n"
	"  -u | -h | --usage | --help     prints program usage.\n"
	"  -q | --quiet                   forces filesystem check without\n"
	"                                 warning message.\n"
	"  -p | --profile                 profile to be used for recovering.\n"
	"  -k | --known-profiles          prints known profiles.\n"
	"  -w | --without-replaying       do not replay the journal.\n"
	"  -c | --check                   check the filesystem (default).\n"
	"  -r | --rebuild                 rebuild internal tree.\n");
}

int main(int argc, char *argv[]) {
    int c, error, force = 0, replay = 1;
    int check = 0, rebuild = 0;
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
	{"without-replaying", no_argument, NULL, 'w'},
	{"check", no_argument, NULL, 'c'},
	{"rebuild", no_argument, NULL, 'r'},
	{0, 0, 0, 0}
    };
    
    if (argc < 2) {
	fsck_print_usage();
	return 0xfe;
    }

    while ((c = getopt_long_only(argc, argv, "uhvp:kqcr", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'u': 
	    case 'h': {
		fsck_print_usage();
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
	        fsck_print_usage();
	        return 0xfe;
	    }
	}
    }

    if (optind >= argc) {
	fsck_print_usage();
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

