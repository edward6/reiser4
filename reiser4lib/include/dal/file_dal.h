/*
	file_dal.h -- standard file device abstraction layer
	Copyright (C) 2001, 2002 Yury Umanets <torque@ukrpost.net>, see COPYING for 
	licensing and copyright details.
*/

#ifndef FILE_DAL_H
#define FILE_DAL_H

#include <dal/dal.h>

extern dal_t *file_dal_open(const char *dev, size_t blocksize, int flags);
extern int file_dal_reopen(dal_t *dal, int flags);
extern void file_dal_close(dal_t *dal);

#endif
