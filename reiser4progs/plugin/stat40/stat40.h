/*
    stat40.h -- reiser4 default stat data structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef STAT40_H
#define STAT40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

/* 
    This is even not minimal stat data. Object can live without stat data at all, 
    just do not allow to link to it. Or size could be stored in the container if 
    there are objects of the same size only. 
*/
struct stat40 {
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
};

typedef struct stat40 stat40_t;  

#define st40_get_mode(stat)		aal_get_le16(stat, mode)
#define st40_set_mode(stat, val)	aal_set_le16(stat, mode, val)

#define st40_get_extmask(stat)		aal_get_le16(stat, extmask)
#define st40_set_extmask(stat, val)	aal_set_le16(stat, extmask, val)

#define st40_get_nlink(stat)		aal_get_le32(stat, nlink)
#define st40_set_nlink(stat, val)	aal_set_le32(stat, nlink, val)

#define st40_get_size(stat)		aal_get_le64(stat, size)
#define st40_set_size(stat, val)	aal_set_le64(stat, size, val)

#endif

