/*
    internal40.h -- reiser4 dafault internal item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef INTERNAL40_H
#define INTERNAL40_H

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

struct reiserfs_internal40 {
    /* 
	Vitaly! Here must be uint64_t! Sorry, but why I should fix 
	your bugs every time you edited our shared sources. Be careful 
	next time please.
	Umka! here must be just a block number! Sorry, but if you will 
	change block number size every time, you will have to fix all 
	related stuff. Moreover, if you will add a wrong structure, take 
	the responsibility for the bug on yourself please, it is very 
	boring to point you every time that it was your bug when you are 
	trying to blame me. Please think before next time.
    */
    blk_t block_nr;
};

typedef struct reiserfs_internal40 reiserfs_internal40_t;

#endif

