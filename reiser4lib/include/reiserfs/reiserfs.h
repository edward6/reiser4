/*
 	reiserfs.h -- the central libreiserfs include.
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef REISERFS_H
#define REISERFS_H

#ifdef __cplusplus
extern "C" {
#endif

#undef NULL
#ifdef __cplusplus
#  define NULL 0
#else
#  define NULL ((void *)0)
#endif

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

#include "filesystem.h"
#include "super.h"
#include "journal.h"
#include "alloc.h"
#include "tree.h"
#include "plugin.h"
#include "tools.h"
#include "node.h"
#include "key.h"

extern int libreiserfs_get_max_interface_version(void);
extern int libreiserfs_get_min_interface_version(void);
extern const char *libreiserfs_get_version(void);

extern int libreiserfs_init(void);
extern void libreiserfs_done(void);

#ifdef __cplusplus
}
#endif

#endif

