/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __IOCTL_H__ )
#define __IOCTL_H__

#include <linux/fs.h>

#define REISER4_IOC_UNPACK _IOW(0xCD,1,long)

extern int reiser4_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);

extern int reiser4_unpack(struct inode *inode, struct file *filp);

#endif				/* __IOCTL_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
