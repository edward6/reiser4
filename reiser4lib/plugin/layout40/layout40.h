/*
	layout40.h -- default disk-layout plugin implementation for reiserfs 4.0
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef LAYOUT40_H
#define LAYOUT40_H

#include <dal/dal.h>

struct reiserfs_layout40 {
	dal_t *dal;
};

typedef struct reiserfs_layout40 reiserfs_layout40_t;

#endif

