/*
    reiser4.h -- the central libreiser4 header.
    Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef REISER4_H
#define REISER4_H

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

#include <aal/aal.h>

#include "filesystem.h"
#include "format.h"
#include "journal.h"
#include "alloc.h"
#include "oid.h"
#include "plugin.h"
#include "tree.h"
#include "node.h"
#include "key.h"
#include "object.h"
#include "coord.h"
#include "cache.h"
#include "dir.h"
#include "master.h"
#include "item.h"
#include "factory.h"

extern errno_t libreiser4_init(void);
extern void libreiser4_done(void);

extern const char *libreiser4_version(void);

extern int libreiser4_max_interface_version(void);
extern int libreiser4_min_interface_version(void);

#ifdef __cplusplus
}
#endif

#endif

