/*
    internal40.h -- reiser4 dafault internal item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef INTERNAL40_H
#define INTERNAL40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct internal40 {
    blk_t ptr;
};

typedef struct internal40 internal40_t;

#define it40_get_ptr(it)	aal_get_le64(it, ptr)
#define it40_set_ptr(it, val)	aal_set_le64(it, ptr, val)

#endif

