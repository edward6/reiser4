/*
    mkfs.c -- program to create reiser4 filesystem.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#ifdef HAVE_UUID
#  include <uuid/uuid.h>
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <reiser4/reiser4.h>
#include <progs/misc.h>

static void mkfs_print_usage(void) {
    fprintf(stderr, "Usage: mkfs.reiser4 [ options ] FILE [ size[K|M|G] ]\n");
    
    fprintf(stderr, "Options:\n"
	"  -v | --version                 prints current version.\n"
	"  -u | -h | --usage | --help     prints program usage.\n"
	"  -q | --quiet                   forces creating filesystem without\n"
	"                                 any questions.\n"
	"  -f | --force                   makes mkfs to use whole disk, not block device\n"
	"                                 or mounted partition.\n"
	"  -p | --profile                 profile to be used.\n"
	"  -k | --known-profiles          prints known profiles.\n"
	"  -b | --block-size=N            block size, 4096 by default,\n"
	"                                 other are not supported for awhile.\n"
	"  -l | --label=LABEL             volume label lets to mount\n"
	"                                 filesystem by its label.\n"
	"  -d | --uuid=UUID               universally unique identifier.\n");
}

int main(int argc, char *argv[]) {
    struct stat st;
    char uuid[17], label[17];
    aal_list_t *walk = NULL;
    aal_list_t *devices = NULL;
    count_t fs_len = 0, dev_len = 0;
    int c, error, force = 0, quiet = 0;
    char *host_dev, *profile_label = "default40";
    uint16_t blocksize = REISERFS_DEFAULT_BLOCKSIZE;
    
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_profile_t *profile;

    static struct option long_options[] = {
	{"version", no_argument, NULL, 'v'},
	{"usage", no_argument, NULL, 'u'},
	{"help", no_argument, NULL, 'h'},
	{"profile", required_argument, NULL, 'p'},
	{"force", no_argument, NULL, 'f'},
	{"known-profiles", no_argument, NULL, 'k'},
	{"quiet", no_argument, NULL, 'q'},
	{"block-size", required_argument, NULL, 'b'},
	{"label", required_argument, NULL, 'l'},
	{"uuid", required_argument, NULL, 'i'},
	{0, 0, 0, 0}
    };
    
    if (argc < 2) {
	mkfs_print_usage();
	return ERROR_USER;
    }
    
    aal_exception_set_handler(__progs_exception_handler);
    
    memset(uuid, 0, sizeof(uuid));
    memset(label, 0, sizeof(label));

    /* Parsing parameters */    
    while ((c = getopt_long_only(argc, argv, "uhvp:qfkb:i:l:", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'u': 
	    case 'h': {
		mkfs_print_usage();
		return ERROR_NONE;
	    }
	    case 'v': {
		printf("%s %s\n", argv[0], VERSION);
		return ERROR_NONE;
	    }
	    case 'p': {
		profile_label = optarg;
		break;
	    }
	    case 'f': {
		force = 1;
		break;
	    }
	    case 'q': {
		quiet = 1;
		break;
	    }
	    case 'k': {
		progs_misc_profile_list();
		return ERROR_NONE;
	    }
	    case 'b': {
		
		/* Parsing blocksize */
	        if (!(blocksize = (uint16_t)progs_misc_strtol(optarg, &error)) && error) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		        "Invalid blocksize (%s).", optarg);
		    return ERROR_USER;
		}
		if (!aal_pow_of_two(blocksize)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid block size %u. It must power of two.", (uint16_t)blocksize);
		    return ERROR_USER;	
		}
		break;
	    }
	    case 'i': {
		
		/* Parsing passed by user uuid */
		if (aal_strlen(optarg) != 36) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Invalid uuid was specified (%s).", optarg);
		    return ERROR_USER;
		}
#ifdef HAVE_UUID
		{
		    if (uuid_parse(optarg, uuid) < 0) {
			aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			    "Invalid uuid was specified (%s).", optarg);
			return ERROR_USER;
		    }
		}
#endif		
		break;
	    }
	    case 'l': {
		aal_strncpy(label, optarg, sizeof(label) - 1);
	        break;
	    }
	    case '?': {
	        mkfs_print_usage();
	        return ERROR_USER;
	    }
	}
    }

    if (optind >= argc) {
	mkfs_print_usage();
	return ERROR_USER;
    }
    
    /* Initializing passed profile */
    if (!(profile = progs_misc_profile_find(profile_label))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find profile by its label \"%s\".", profile_label);
	goto error;
    }
    
    /* 
	Initializing libreiser4. We are using zero as tree cache limit. In this 
	case, libreiser4 will set up it by itself. We do this because mkfs doesn't
	need a big cache.
    */
    if (libreiser4_init(0)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	goto error;
    }

    /* Building list of devices filesystem will be created on */
    for (; optind < argc; optind++) {
	if (stat(argv[optind], &st) == -1) {
	    if (progs_misc_size_check(argv[optind])) {
		fs_len = (progs_misc_size_parse(argv[optind], &error));
		if (!error && fs_len < blocksize) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
			"Strange filesystem size has been detected (%s).", argv[optind]);
		    goto error_free_libreiser4;
		}
		fs_len /= blocksize;
	    } else {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Something strange has been detected while parsing "
		    "parameetrs (%s).", argv[optind]);
		goto error_free_libreiser4;
	    }
	} else
	    devices = aal_list_append(devices, argv[optind]);
    }
    
    if (aal_list_length(devices) > 1) {
	aal_memset(uuid, 0, sizeof(uuid));
	aal_memset(label, 0, sizeof(label));
    }
    
    /* The loop through all devices */
    aal_list_foreach_forward(walk, devices) {
    
    host_dev = (char *)walk->item;
    
    if (stat(host_dev, &st) == -1)
	goto error_free_libreiser4;
    
    if (!S_ISBLK(st.st_mode)) {
	if (!force) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Device \"%s\" is not block device. Use -f to force over.", host_dev);
	    goto error_free_libreiser4;
	}
    } else {
	if (((IDE_DISK_MAJOR(MAJOR(st.st_rdev)) && MINOR(st.st_rdev) % 64 == 0) ||
	    (SCSI_BLK_MAJOR(MAJOR(st.st_rdev)) && MINOR(st.st_rdev) % 16 == 0)) && !force)
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Device \"%s\" is an entire harddrive, not just one partition.", host_dev);
	    goto error_free_libreiser4;
	}
    }
   
    /* Checking if passed partition is mounted */
    if (progs_misc_dev_mounted(host_dev, NULL) && !force) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Device \"%s\" is mounted at the moment. Use -f to force over.", host_dev);
	goto error_free_libreiser4;
    }

#ifdef HAVE_UUID
    if (aal_strlen(uuid) == 0)
	uuid_generate(uuid);
#endif

    /* Opening device */
    if (!(device = aal_file_open(host_dev, blocksize, O_RDWR))) {
	char *error = strerror(errno);
	
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device \"%s\". %s.", host_dev, error);
	goto error_free_libreiser4;
    }
    
    /* Preparing filesystem length */
    dev_len = aal_device_len(device);
    
    if (!fs_len)
        fs_len = dev_len;
	
    if (fs_len > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Filesystem wouldn't fit into device %llu blocks long,"
	    " %llu blocks required.", dev_len, fs_len);
	goto error_free_device;
    }

    /* Checking for "quiet" mode */
    if (!quiet) {
	if (aal_exception_throw(EXCEPTION_INFORMATION, EXCEPTION_YESNO, 
		"All data on \"%s\" will be lost.", host_dev) == EXCEPTION_NO)
	    goto error_free_device;
    }

    fprintf(stderr, "Creating reiser4 on \"%s\" with "
	"\"%s\" profile...", host_dev, profile->label);
    
    fflush(stderr);
    
    /* Creating filesystem */
    if (!(fs = reiserfs_fs_create(profile, device, blocksize, 
	(const char *)uuid, (const char *)label, fs_len, device, NULL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create filesystem on %s.", aal_device_name(device));
	goto error_free_device;
    }
    fprintf(stderr, "done\n");

    fprintf(stderr, "Synchronizing...");
    
    /* Flushing all filesystem buffers onto the device */
    if (reiserfs_fs_sync(fs)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize created filesystem.");
	goto error_free_fs;
    }

    /* Synchronizing device */
    if (aal_device_sync(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize device %s.", aal_device_name(device));
	goto error_free_fs;
    }

    fprintf(stderr, "done\n");
    
    reiserfs_fs_close(fs);
    aal_file_close(device);

    }
    
    aal_list_free(devices);
    libreiser4_done();
    
    return ERROR_NONE;

error_free_fs:
    reiserfs_fs_close(fs);
error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_done();
error:
    return ERROR_PROG;
}

