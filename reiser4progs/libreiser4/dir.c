/*
    dir.c -- directory specific code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

reiserfs_object_t *reiserfs_dir_open(reiserfs_fs_t *fs, 
    const char *name) 
{
    reiserfs_object_t *dir;

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;


    
    return 0;
}
