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
#if defined(__cplusplus)
#  define NULL 0
#else
#  define NULL ((void *)0)
#endif

#include "plugin.h"
#include "tools.h"
#include "endian.h"

#define PLUGIN_MAX_NAME 255

struct reiserfs_item {
	reiserfs_plugin_type_t type;
	reiserfs_plugin_id_t id;
	char name[PLUGIN_MAX_NAME];
};

typedef struct reiserfs_item reiserfs_item_t;

extern int libreiserfs_get_max_interface_version(void);
extern int libreiserfs_get_min_interface_version(void);
extern const char *libreiserfs_get_version(void);

#ifdef __cplusplus
}
#endif

#endif

