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

#define REISER40_PROFILE 0x1

static reiserfs_profile_t profile40 = {
    .label = "profile40",
    .desc = "Default profile for reiser4 filesystem",
    
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
    .journal = 0x0
};
    
static error_t mkfs_setup_profile(reiserfs_profile_t *profile, int number) {
    aal_assert("vpf-104", profile != NULL, return -1);

    switch (number) {
	case REISER40_PROFILE: {
	    *profile = profile40;
	    break;
	}
	default: {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Unknown profile has detected %x.", number);
	    return -1;
	}
    }
    return 0;
}

static void mkfs_print_usage(void) {
    fprintf(stderr, "Usage: mkfs.reiser4 [ profile [ profile-options ] ] "
	"[ options ] device [ size[K|M|G] ]\n");
    
    fprintf(stderr, "Options:\n"
	"  -v | --version                  prints current version\n"
	"  -u | --usage                    prints program usage\n"
	"  -p | --profile                  prints known profiles\n"
	"  -b N | --block-size=N           block size (1024, 2048, 4096...)\n"
	"  -f FORMAT | --format=FORMAT     reiserfs version (3.5, 3.6, 4.0)\n"
	"  -l LABEL | --label=LABEL        volume label\n"
	"  -d UUID | --uuid=UUID           sets universally unique identifier\n");
}

int main(int argc, char *argv[]) {
    int c, error;
    char *host_dev;
    char uuid[17], label[17];
    count_t fs_len = 0, dev_len = 0;
    uint16_t blocksize = REISERFS_DEFAULT_BLOCKSIZE;
    
    reiserfs_fs_t *fs;
    aal_device_t *device;
    reiserfs_profile_t profile;
    
    static struct option long_options[] = {
	{"version", no_argument, NULL, 'v'},
	{"usage", no_argument, NULL, 'u'},
	{"profile", no_argument, NULL, 'p'},
	{"block-size", required_argument, NULL, 'b'},
	{"format", required_argument, NULL, 'f'},
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
    
    while ((c = getopt_long_only(argc, argv, "uvpb:f:i:l:", long_options, 
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
	    case 'f': {
		if (strncmp(optarg, "4.0", 3)) {
		    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
			"Sorry, at the moment only 4.0 format is supported.");
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
	    "Can't open device %s.", argv[1]);
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

    mkfs_setup_profile(&profile, REISER40_PROFILE);
    
    if (!(c = reiser4progs_misc_choose_propose("ynYN", "Please select (y/n) ", 
	    "All data on %s will be lost. Do you realy want to create reiserfs 4.0 "
	    "(y/n) ", host_dev)))
	goto error_free_device;
	
    if (c == 'n' || c == 'N')
        goto error_free_device;

    fprintf(stderr, "Creating reiserfs with default profile...");
    fflush(stderr);
    
    if (!(fs = reiserfs_fs_create(device, &profile, blocksize, 
	(const char *)uuid, (const char *)label, fs_len, device, NULL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create filesystem on %s.", argv[1]);
	goto error_free_device;
    }
    fprintf(stderr, "done\n");

    fprintf(stderr, "Synchronizing filesystem...");
    fflush(stderr);
    
    if (reiserfs_fs_sync(fs)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize created filesystem.");
	goto error_free_fs;
    }
    fprintf(stderr, "done\n");

    fprintf(stderr, "Synchronizing device...");
    fflush(stderr);
	
    if (aal_device_sync(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize device %s.", argv[1]);
	goto error_free_fs;
    }

    fprintf(stderr, "done\n");
    
    reiserfs_fs_close(fs);
    libreiser4_fini();
    aal_file_close(device);
    
    return 0;

error_free_fs:
    reiserfs_fs_close(fs);
error_free_device:
    aal_file_close(device);
error_free_libreiser4:
    libreiser4_fini();
error:
    return 0xff;
}
