/*
    mkfs.c -- the program to create reiser4 filesystem.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>

#include <reiser4/reiser4.h>
#include <reiser4progs/misc.h>

static reiserfs_profile_t profiles[] = {
    [0] = {
	.label = "default40",
	.desc = "Default profile for reiser4 filesystem. "
	    "It consists of format40, journal40, alloc40, etc",
    
	.node = 0x0,
	.item = {
	    .internal = 0x3,
	    .statdata = 0x0,
	    .direntry = 0x2,
	    .fileentry = 0x0
	},
	.file = 0x0,
	.dir = 0x0,
	.hash = 0x0,
	.tail = 0x0,
	.hook = 0x0,
	.perm = 0x0,
	.format = 0x0,
	.oid = 0x0,
	.alloc = 0x0,
	.journal = 0x0,
	.key = 0x0
    }
};

static reiserfs_profile_t *mkfs_find_profile(const char *profile) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(profiles) / sizeof(reiserfs_profile_t)); i++) {
	if (!strncmp(profiles[i].label, profile, strlen(profiles[i].label)))
	    return &profiles[i];
    }

    return NULL;
}

static void mkfs_list_profiles(void) {
    unsigned i;
    
    for (i = 0; i < (sizeof(profiles) / sizeof(reiserfs_profile_t)); i++)
	printf("(%d) %s (%s).\n", i + 1, profiles[i].label, profiles[i].desc);
}

static void mkfs_print_usage(void) {
    fprintf(stderr, "Usage: mkfs.reiser4 [ options ] device [ size[K|M|G] ]\n");
    
    fprintf(stderr, "Options:\n"
	"  -v | --version                  prints current version\n"
	"  -u | --usage                    prints program usage\n"
	"  -p | --profile                  profile to be used\n"
	"  -f | --force                    force creating filesystem without warning message\n"
	"  -k | --known-profiles           prints known profiles\n"
	"  -b N | --block-size=N           block size (1024, 2048, 4096...)\n"
	"  -l LABEL | --label=LABEL        volume label\n"
	"  -d UUID | --uuid=UUID           sets universally unique identifier\n");
}

int main(int argc, char *argv[]) {
    int c, error, force = 0;
    char uuid[17], label[17];
    count_t fs_len = 0, dev_len = 0;
    char *host_dev, *profile_label = "default40";
    uint16_t blocksize = REISERFS_DEFAULT_BLOCKSIZE;
    
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_profile_t *profile;
    
    static struct option long_options[] = {
	{"version", no_argument, NULL, 'v'},
	{"usage", no_argument, NULL, 'u'},
	{"profile", no_argument, NULL, 'p'},
	{"force", no_argument, NULL, 'f'},
	{"block-size", required_argument, NULL, 'b'},
	{"label", required_argument, NULL, 'l'},
	{"uuid", required_argument, NULL, 'i'},
	{0, 0, 0, 0}
    };
    
    if (argc < 2) {
	mkfs_print_usage();
	return 0xfe;
    }
    
    memset(uuid, 0, sizeof(uuid));
    memset(label, 0, sizeof(label));
    
    while ((c = getopt_long_only(argc, argv, "uvp:fkb:i:l:", long_options, 
	(int *)0)) != EOF) 
    {
	switch (c) {
	    case 'u': {
		mkfs_print_usage();
		return 0;
	    }
	    case 'v': {
		printf("%s %s\n", argv[0], VERSION);
		return 0;
	    }
	    case 'p': {
		profile_label = optarg;
		break;
	    }
	    case 'f': {
		force = 1;
		break;
	    }
	    case 'k': {
		mkfs_list_profiles();
		return 0;
	    }
	    case 'b': {
	        if (!(blocksize = (uint16_t)reiser4progs_misc_strtol(optarg, &error)) && error) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		        "Invalid blocksize (%s).", optarg);
		    return 0xfe;
		}
		if (!aal_pow_of_two(blocksize)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid block size %u. It must power of two.", (uint16_t)blocksize);
		    return 0xfe;	
		}
		break;
	    }
	    case 'i': {
		if (strlen(optarg) < 16) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Invalid uuid (%s).", optarg);
		    return 0xfe;
		}
		strncpy(uuid, optarg, sizeof(uuid) - 1);
		break;
	    }
	    case 'l': {
		strncpy(label, optarg, sizeof(label) - 1);
	        break;
	    }
	    case '?': {
	        mkfs_print_usage();
	        return 0xfe;
	    }
	}
    }

    if (optind >= argc) {
	mkfs_print_usage();
	return 0xfe;
    }
    
    if (!(profile = mkfs_find_profile(profile_label))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find profile by its label \"%s\".", profile_label);
	return 0xff;
    }
    
    host_dev = argv[optind++];
    
    if (optind < argc) {
	char *len_str = argv[optind];
		
	aal_exception_fetch_all();
	if (!reiser4progs_misc_dev_check(host_dev)) {
	    char *tmp = host_dev;
	    host_dev = len_str;
	    len_str = tmp;
	}
	aal_exception_leave_all();
		
	if (!(fs_len = (reiser4progs_misc_size_parse(len_str, 
	    &error) / blocksize)) && error) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Invalid filesystem size (%s).", len_str);
	    return 0xfe;
	}
    }
    
    /* Checking given device for validness */
    if (!reiser4progs_misc_dev_check(host_dev)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Device %s doesn't exists or invalid.", host_dev);
	return 0xfe;
    }
    
    if (libreiser4_init()) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't initialize libreiser4.");
	goto error;
    }

    if (!(device = aal_file_open(host_dev, blocksize, O_RDWR))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't open device %s.", host_dev);
	goto error_free_libreiser4;
    }
    
    dev_len = aal_device_len(device);
    
    if (!fs_len)
        fs_len = dev_len;
	
    if (fs_len > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Filesystem size is too big for device %llu blocks long.", dev_len);
	goto error_free_libreiser4;
    }

    if (!force) {
	if (!(c = reiser4progs_misc_choose_propose("ynYN", "Please select (y/n) ", 
		"All data on %s will be lost (y/n) ", host_dev)))
	    goto error_free_device;
	
	if (c == 'n' || c == 'N')
	    goto error_free_device;
    }

    fprintf(stderr, "Creating reiserfs with \"%s\" profile...", profile->label);
    fflush(stderr);
    
    if (!(fs = reiserfs_fs_create(device, profile, blocksize, 
	(const char *)uuid, (const char *)label, fs_len, device, NULL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create filesystem on %s.", aal_device_name(device));
	goto error_free_device;
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
    
    return 0;

error_free_fs:
    reiserfs_fs_close(fs);
error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_done();
error:
    return 0xff;
}
