/*
     io.h -- tools for handling and checking io for reiser4 progs.
     Copyright (C) 1996 - 2002 Hans Reiser
     Author Vitaly Fertman.
*/

#ifndef PROGS_IO_H
#define PROGS_IO_H

#include <linux/major.h>

#ifndef MAJOR
#define MAJOR(rdev)      ((rdev)>>8)
#define MINOR(rdev)      ((rdev) & 0xff)
#endif /* MAJOR */

#ifndef SCSI_DISK_MAJOR
#define SCSI_DISK_MAJOR(maj) ((maj) == SCSI_DISK0_MAJOR || \
	((maj) >= SCSI_DISK1_MAJOR && (maj) <= SCSI_DISK7_MAJOR))
#endif /* SCSI_DISK_MAJOR */

#ifndef SCSI_BLK_MAJOR
#define SCSI_BLK_MAJOR(maj)  (SCSI_DISK_MAJOR(maj) || (maj) == SCSI_CDROM_MAJOR)
#endif /* SCSI_BLK_MAJOR */

#ifndef IDE_DISK_MAJOR
#ifdef IDE9_MAJOR
#define IDE_DISK_MAJOR(maj) ((maj) == IDE0_MAJOR || (maj) == IDE1_MAJOR || \
			    (maj) == IDE2_MAJOR || (maj) == IDE3_MAJOR || \
			    (maj) == IDE4_MAJOR || (maj) == IDE5_MAJOR || \
			    (maj) == IDE6_MAJOR || (maj) == IDE7_MAJOR || \
			    (maj) == IDE8_MAJOR || (maj) == IDE9_MAJOR)
#else
#define IDE_DISK_MAJOR(maj) ((maj) == IDE0_MAJOR || (maj) == IDE1_MAJOR || \
			    (maj) == IDE2_MAJOR || (maj) == IDE3_MAJOR || \
			    (maj) == IDE4_MAJOR || (maj) == IDE5_MAJOR)
#endif /* IDE9_MAJOR */
#endif /* IDE_DISK_MAJOR */
    

#endif
