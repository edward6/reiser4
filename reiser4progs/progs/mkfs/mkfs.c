/*
    mkfs.c -- program to create reiser4 filesystem.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#ifdef HAVE_LIBUUID
#  include <uuid/uuid.h>
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <reiser4/reiser4.h>

#include <aux/aux.h>
#include <misc/misc.h>

static void mkfs_print_usage(char *name) {
    fprintf(stderr, "Usage: %s [ options ] "
	"FILE1 FILE2 ... [ size[K|M|G] ]\n", name);
    
    fprintf(stderr, 
	"Common options:\n"
	"  -? | -h | --help               prints program usage.\n"
	"  -V | --version                 prints current version.\n"
	"  -q | --quiet                   forces creating filesystem without\n"
	"                                 any questions.\n"
	"  -f | --force                   makes mkfs to use whole disk, not\n"
	"                                 block device or mounted partition.\n"
	"  -s | --lost-found              forces mkfs to create lost+found\n"
	"                                 directory.\n"
	"  -b | --block-size N            block size, 4096 by default,\n"
	"                                 other are not supported at the moment.\n"
	"  -l | --label LABEL             volume label lets to mount\n"
	"                                 filesystem by its label.\n"
	"  -i | --uuid UUID               universally unique identifier.\n"
	"Plugins options:\n"
	"  -e | --profile PROFILE         profile to be used.\n"
	"  -K | --known-profiles          prints known profiles.\n");
}

static void mkfs_init(void) {
    int i;

    /* Setting up exception streams*/
    for (i = 0; i < 5; i++)
	progs_exception_set_stream(i, stderr);
	
    aal_exception_set_handler(progs_exception_handler);
}

/* Crates lost+found directory */
static reiser4_object_t *mkfs_create_lost_found(reiser4_fs_t *fs, 
    reiser4_profile_t *profile) 
{
    reiser4_plugin_t *plugin;
    reiser4_object_hint_t hint;

    /* Getting the plugin from entity of the root directory */
    plugin = fs->dir->entity->plugin;
	
    /* Preparing object hint */
    hint.statdata_pid = profile->item.statdata;
    hint.direntry_pid = profile->item.direntry;
    hint.hash_pid = profile->hash;
	
    /* Creating lost+found */
    return reiser4_dir_create(fs, &hint, plugin, fs->dir, "lost+found");
}

int main(int argc, char *argv[]) {
    struct stat st;
    
    char uuid[17], label[17];
    count_t fs_len = 0, dev_len = 0;
    char *host_dev, *profile_label = "default40";
    uint16_t blocksize = REISER4_DEFAULT_BLOCKSIZE;
    int c, error, force = 0, quiet = 0, lost_found = 0;
    
    reiser4_fs_t *fs;
    aal_device_t *device;
    reiser4_profile_t *profile;

    aal_list_t *walk = NULL;
    aal_list_t *devices = NULL;
    
    static struct option long_options[] = {
	{"version", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{"profile", required_argument, NULL, 'e'},
	{"force", no_argument, NULL, 'f'},
	{"known-profiles", no_argument, NULL, 'K'},
	{"quiet", no_argument, NULL, 'q'},
	{"block-size", required_argument, NULL, 'b'},
	{"label", required_argument, NULL, 'l'},
	{"uuid", required_argument, NULL, 'i'},
	{"lost-found", required_argument, NULL, 's'},
	{0, 0, 0, 0}
    };
    
    if (argc < 2) {
	mkfs_print_usage(argv[0]);
	return USER_ERROR;
    }

    mkfs_init();
    
    memset(uuid, 0, sizeof(uuid));
    memset(label, 0, sizeof(label));

    /* Parsing parameters */    
    while ((c = getopt_long_only(argc, argv, "uhve:qfKb:i:l:s", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'h': {
		mkfs_print_usage(argv[0]);
		return NO_ERROR;
	    }
	    case 'V': {
		printf(BANNER(argv[0]));
		return NO_ERROR;
	    }
	    case 'e': {
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
	    case 'K': {
		progs_profile_list();
		return NO_ERROR;
	    }
	    case 'b': {
		
		/* Parsing blocksize */
	        if (!(blocksize = (uint16_t)reiser4_aux_strtol(optarg, &error)) && error) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		        "Invalid blocksize (%s).", optarg);
		    return USER_ERROR;
		}
		if (!aal_pow_of_two(blocksize)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid block size %u. It must power of two.", (uint16_t)blocksize);
		    return USER_ERROR;	
		}
		break;
	    }
	    case 'i': {
		
		/* Parsing passed by user uuid */
		if (aal_strlen(optarg) != 36) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Invalid uuid was specified (%s).", optarg);
		    return USER_ERROR;
		}
#ifdef HAVE_LIBUUID
		{
		    if (uuid_parse(optarg, uuid) < 0) {
			aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			    "Invalid uuid was specified (%s).", optarg);
			return USER_ERROR;
		    }
		}
#endif		
		break;
	    }
	    case 'l': {
		aal_strncpy(label, optarg, sizeof(label) - 1);
	        break;
	    }
	    case 's': {
		lost_found = 1;
	        break;
	    }
	    case '?': {
	        mkfs_print_usage(argv[0]);
	        return USER_ERROR;
	    }
	}
    }
    
    printf(BANNER(argv[0]));

    if (optind >= argc) {
	mkfs_print_usage(argv[0]);
	return USER_ERROR;
    }
    
    /* Initializing passed profile */
    if (!(profile = progs_profile_find(profile_label))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find profile by its label \"%s\".", profile_label);
	goto error;
    }
    
    /* 
	Initializing libreiser4. We are using zero as tree cache limit. In this 
	case, libreiser4 will set up it by itself. We do this because mkfs doesn't
	need a big cache.
    */
    if (libreiser4_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	goto error;
    }

    /* Building list of devices filesystem will be created on */
    for (; optind < argc; optind++) {
	if (stat(argv[optind], &st) == -1) {
	    fs_len = (progs_misc_size_parse(argv[optind], &error));
	    if (!error) {
		if (fs_len < blocksize) {
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
    
    if (aal_gauge_create(GAUGE_SILENT, "", progs_gauge_handler, NULL))
	goto error_free_libreiser4;
    
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

#ifdef HAVE_LIBUUID
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
		   "Reiser4 is going to be created on \"%s\".", host_dev) == EXCEPTION_NO)
		goto error_free_device;
	}
    
	aal_gauge_rename("Creating reiser4 on \"%s\" with "
	    "\"%s\" profile", host_dev, profile->label);
	aal_gauge_start();

	/* Creating filesystem */
	if (!(fs = reiser4_fs_create(profile, device, blocksize, 
	    (const char *)uuid, (const char *)label, fs_len, device, NULL))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't create filesystem on %s.", aal_device_name(device));
	    goto error_free_device;
	}

	/* Creating lost+found directory */
	if (lost_found) {
	    reiser4_object_t *object;
	    
	    if (!(object = mkfs_create_lost_found(fs, profile))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't create lost+found directory.");
		goto error_free_device;
	    }
	    
	    reiser4_object_close(object);
	}
	
	/* Flushing all filesystem buffers onto the device */
	if (reiser4_fs_sync(fs)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't synchronize created filesystem.");
	    goto error_free_fs;
	}

	aal_gauge_done();
	
	aal_gauge_rename("Synchronizing \"%s\"", host_dev);
	aal_gauge_start();
	
	/* Synchronizing device */
	if (aal_device_sync(device)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't synchronize device %s.", aal_device_name(device));
	    goto error_free_fs;
	}

	/* 
	    Zeroing uuid in order to force mkfs to generate it on its own for 
	    next device form built device list.
	*/
	aal_memset(uuid, 0, sizeof(uuid));

	/* 
	    Zeroing fs_len in order to force mkfs on next turn to calc its size
	    from actual device length.
	*/
	fs_len = 0;
	
	aal_gauge_done();

	reiser4_fs_close(fs);
	aal_file_close(device);
    }
    
    /* Freeing the all used objects */
    aal_gauge_free();
    aal_list_free(devices);
    libreiser4_done();
    
    return NO_ERROR;

error_free_fs:
    reiser4_fs_close(fs);
error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_done();
error:
    return OPER_ERROR;
}

