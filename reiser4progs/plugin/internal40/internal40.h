/*
    internal40.h -- reiser4 dafault internal item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef INTERNAL40_H
#define INTERNAL40_H

#include <aal/aal.h>

struct reiserfs_internal40 {
    blk_t block_nr;
};

typedef struct reiserfs_internal40 reiserfs_internal40_t;

#define int40_get_blk(int40)	    get_le64(int40, block_nr)
#define int40_set_blk(int40, val)   set_le64(int40, block_nr, val)

#endif

