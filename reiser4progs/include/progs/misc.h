/*
    misc.h -- reiser4progs common include.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef REISER4_MISC_H
#define REISER4_MISC_H

#include <aal/aal.h>
#include <reiser4/filesystem.h>

#include <linux/major.h>

#ifndef MAJOR
#  define MAJOR(rdev) ((rdev) >> 8)
#  define MINOR(rdev) ((rdev) & 0xff)
#endif

#ifndef SCSI_DISK_MAJOR
#  define SCSI_DISK_MAJOR(maj) ((maj) == SCSI_DISK0_MAJOR || \
	((maj) >= SCSI_DISK1_MAJOR && (maj) <= SCSI_DISK7_MAJOR))
#endif

#ifndef SCSI_BLK_MAJOR
#  define SCSI_BLK_MAJOR(maj) (SCSI_DISK_MAJOR(maj) || \
	(maj) == SCSI_CDROM_MAJOR)
#endif

#ifndef IDE_DISK_MAJOR
#  ifdef IDE9_MAJOR
#  define IDE_DISK_MAJOR(maj) ((maj) == IDE0_MAJOR || \
	(maj) == IDE1_MAJOR || (maj) == IDE2_MAJOR || \
	(maj) == IDE3_MAJOR || (maj) == IDE4_MAJOR || \
	(maj) == IDE5_MAJOR || (maj) == IDE6_MAJOR || \
	(maj) == IDE7_MAJOR || (maj) == IDE8_MAJOR || \
	(maj) == IDE9_MAJOR)
#  else
#  define IDE_DISK_MAJOR(maj) ((maj) == IDE0_MAJOR || \
	(maj) == IDE1_MAJOR || (maj) == IDE2_MAJOR || \
	(maj) == IDE3_MAJOR || (maj) == IDE4_MAJOR || \
	(maj) == IDE5_MAJOR)
#  endif
#endif

#define ERROR_USER (0xfe)
#define ERROR_PROG (0xff)
#define ERROR_NONE (0)

extern long int progs_misc_strtol(const char *str, int *error);

extern int progs_misc_dev_mounted(const char *name, 
    const char *ops);

extern int progs_misc_size_check(const char *str);
extern unsigned long long progs_misc_size_parse(const char *str, 
    int *error);

extern reiserfs_profile_t *progs_misc_profile_find(const char *profile);
extern void progs_misc_profile_list(void);

extern aal_exception_option_t __progs_exception_handler(aal_exception_t *exception);
extern void __progs_gauge_handler(aal_gauge_t *gauge);

#endif

