/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined( __REISER4_SYMLINK_H__ )
#define __REISER4_SYMLINK_H__

#include "../forward.h"
#include <linux/fs.h>		/* for struct inode */

int symlink_create(struct inode *symlink, struct inode *dir, reiser4_object_create_data * data);

/* __REISER4_SYMLINK_H__ */
#endif
