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

extern int libreiser4_get_max_interface_version(void);
extern int libreiser4_get_min_interface_version(void);
extern const char *libreiser4_get_version(void);

extern errno_t libreiser4_init(uint32_t mem_limit);
extern void libreiser4_done(void);

extern uint32_t libreiser4_mlimit_get(void);
extern void libreiser4_mlimit_set(uint32_t mem_limit);

#ifdef __cplusplus
}
#endif

#endif

