/*
    dir.c -- reiserfs directory code. 
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

reiserfs_dir_t *reiserfs_dir_create(reiserfs_dir_t *parent, 
    reiserfs_dir_info_t *info) 
{
    reiserfs_dir_t *dir;
    
    aal_assert("umka-590", info != NULL, return NULL);
    
    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    return dir;
}

reiserfs_dir_t *reiserfs_dir_init(void) {
    return NULL;
}

void reiserfs_dir_fini(reiserfs_dir_t *dir) {
    aal_assert("umka-589", dir != NULL, return);
    aal_free(dir);
}
