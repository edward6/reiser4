/*
    stat40.h -- reiser4 default stat data structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef STAT40_H
#define STAT40_H

struct reiserfs_stat40 {
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
};

typedef struct reiserfs_stat40 reiserfs_stat40_t;  

#endif

