/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "debug.h"
#include "reiser4.h"
#include "ioctl.h"
#include "tree.h"

/*
 * reiser4_ioctl - handler for ioctl for inode supported commands:
 * 
 * REISER4_IOC_UNPACK - try to unpack tail from into extent and prevent packing 
 * file (argument arg has to be non-zero)
 */
int reiser4_ioctl(struct inode *inode, struct file *filp, 
    unsigned int cmd, unsigned long arg)
{
    int result;

    REISER4_ENTRY(filp->f_dentry->d_inode->i_sb);
    
    switch (cmd) {
        case REISER4_IOC_UNPACK:
		
	    if (arg) {
	        result = reiser4_unpack(inode, filp);
		break;
	    }
	    
	default:
	    result = -ENOTTY;
	    break;
    }
    
    REISER4_EXIT(result);
}

/*
 * reiser4_unpack -- function try to convert tail into extent by means of using
 * tail2extent function.
 */
int reiser4_unpack(struct inode *inode, struct file *filp) {
    int result;
    
    if (inode->i_size == 0)
        return -EINVAL;
    
    get_nonexclusive_access(inode);
    
    if ((result = tail2extent(inode) != 0))
	return result;

    return 0;
}

