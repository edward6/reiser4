/*
    internal40.h -- reiser4 dafault internal item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef INTERNAL40_H
#define INTERNAL40_H

struct reiserfs_internal40 {
    /* 
	Vitaly! Here must be uint64_t! Sorry, but why I should fix 
	your bugs every time you edited our shared sources. Be careful 
	next time please.
    */
    uint64_t block_nr;
};

typedef struct reiserfs_internal40 reiserfs_internal40_t;

#endif

