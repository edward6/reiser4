/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../reiser4.h"

/*
 * symlink plugin's specific functions
 */

int symlink_create( struct inode * symlink, /* inode of symlink */
		    struct inode * dir, /* parent directory */
		    reiser4_object_create_data * data /* info passed
								 * to us, this
								 * is filled by
								 * reiser4()
								 * syscall in
								 * particular */)
{
	int result;

	assert( "nikita-680", symlink != NULL );
	assert( "nikita-681", S_ISLNK( symlink -> i_mode ) );
	assert( "nikita-685", inode_get_flag( symlink, REISER4_NO_STAT_DATA ) );
	assert( "nikita-682", dir != NULL );
	assert( "nikita-684", data != NULL );
	assert( "nikita-686", data -> id == SYMLINK_FILE_PLUGIN_ID );


	/* stat data of symlink has symlink extension */
	reiser4_inode_data (symlink)->extmask |= (1 << SYMLINK_STAT);

	assert ("vs-838", symlink->u.generic_ip == 0);
	symlink->u.generic_ip = (void *)data->name;

	assert ("vs-843", symlink->i_size == 0);
	symlink->i_size = strlen (data->name);

	/* insert stat data appended with data->name */
	result = common_file_save (symlink);
	if (result) {
		/* FIXME-VS: Make sure that symlink->u.generic_ip is not attached
		   to kmalloced data */
	} else {
		assert( "vs-849", symlink->u.generic_ip &&
			inode_get_flag (symlink, REISER4_GENERIC_VP_USED));
		assert( "vs-850", !memcmp ((char *)symlink->u.generic_ip,
					   data->name, (size_t)symlink->i_size + 1));
	}
	return result;
}
