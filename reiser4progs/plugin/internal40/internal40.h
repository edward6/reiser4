/*
    internal40.h -- reiser4 dafault internal item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef INTERNAL40_H
#define INTERNAL40_H

#include <aal/aal.h>

struct internal40 {
    blk_t pointer;
};

typedef struct internal40 internal40_t;

#define it40_get_pointer(it)		aal_get_le64(it, pointer)
#define it40_set_pointer(it, val)	aal_set_le64(it, pointer, val)

#endif

