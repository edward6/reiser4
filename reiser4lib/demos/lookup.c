/*
	lookup.c -- a programm which demonstrates lookup inside reiserfs tree.
	Copyright (C) 2001, 2002 Yury Umanets <torque@ukrpost.net>, see COPYING for 
	licensing and copyright details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <dal/file_dal.h>

#include <reiserfs/reiserfs.h>
#include <reiserfs/libprogs_tools.h>

static char *types[4] = {"SD", "IT", "DT", "DR"};

static int lookup_type_from_str(const char *type) {
	unsigned int i;

	for (i = 0; i < sizeof(types); i++)
		if (!strncmp(types[i], type, 2)) return i;
	
	return -1; 
}

int main(int argc, char *argv[]) {
	dal_t *dal;
	struct key key;
	reiserfs_fs_t *fs;
	int found = 0, error = 0;

	int32_t dirid, objid;
	int64_t offset = 0, type = 0;
	
	reiserfs_path_node_t *leaf;
	reiserfs_path_t *path;
	
	if (argc < 4) {
		fprintf(stderr, "Usage: %s DEV DIR-ID OBJECT-ID [ OFFSET TYPE ]\n", argv[0]);
		return 0xff;
	}

	if (!(dal = file_dal_open(argv[1], DEFAULT_BLOCK_SIZE, O_RDONLY))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
	    	"Couldn't open device %s.", argv[1]);
		goto error;    
	}
	
	if ((dirid = progs_strtol(argv[2], &error)) && error) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
	    	"Invalid directory %s.", argv[2]);
		goto error_free_dal;    
	}
	
	if ((objid = progs_strtol(argv[3], &error)) && error) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid object %s.", argv[3]);
		goto error_free_dal;
	}

	if (argc > 3 && (offset = progs_strtol(argv[4], &error)) && error) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid offset %s.", argv[4]);
		goto error_free_dal;
	}

	if (argc > 4 && (type = progs_strtol(argv[5], &error)) && error) {
		if ((type = lookup_type_from_str(argv[5])) == -1) {
			libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Invalid type %s.", argv[5]);
			goto error_free_dal;
		}
	}

	if (dirid == objid) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Directory identifier equals object identifier.");
		goto error_free_dal;
	}

	if (!(fs = reiserfs_fs_open_fast(dal, dal))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
			"Couldn't open filesystem on %s.", argv[1]);
		goto error_free_dal;
	}
	
	memset(&key, 0, sizeof(key));
	
	reiserfs_key_form(&key, (uint32_t)dirid, (uint32_t)objid, 
		offset, type, reiserfs_fs_format(fs));
	
	path = reiserfs_path_create(MAX_HEIGHT);

	found = 1;	
	if (!(leaf = reiserfs_tree_lookup_leaf(reiserfs_fs_tree(fs), 
		reiserfs_tree_root(reiserfs_fs_tree(fs)),
	    reiserfs_key_comp_four_components, &key, path)))
	{
		leaf = reiserfs_path_last(path);
		leaf->pos--;
		found = 0;
	}
	
	if (found) {
		libreiserfs_exception_throw(EXCEPTION_INFORMATION, EXCEPTION_CANCEL, 
		    "Specified item found inside the leaf %lu at position %lu.", 
	   		reiserfs_block_location(leaf->node), leaf->pos);
	} else {
		libreiserfs_exception_throw(EXCEPTION_INFORMATION, EXCEPTION_CANCEL, 
		    "Item not found, however acceptable place is inside the leaf "
	    	"%lu at position %lu.", reiserfs_block_location(leaf->node), leaf->pos);
	}
	
	reiserfs_path_free(path);
	
	reiserfs_fs_close(fs);
	
	file_dal_close(dal);
	
	return !found;

error_free_dal:    
	file_dal_close(dal);
error:
	return 0xff;    
}

