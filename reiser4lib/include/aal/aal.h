/*
    aal.h -- the central AAL header. AAL - (Application Abstraction Library).
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef AAL_H
#define AAL_H

#undef NULL
#if defined(__cplusplus)
#  define NULL 0
#else
#  define NULL ((void *)0)
#endif

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

typedef int error_t;

#include "device.h"
#include "file.h"
#include "exception.h"
#include "list.h"
#include "malloc.h"
#include "print.h"
#include "string.h"
#include "math.h"
#include "endian.h"
#include "debug.h"

#endif

