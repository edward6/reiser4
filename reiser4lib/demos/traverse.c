/*
	traverse.c -- a programm which demonstrates reiserfs tree traversing
	Copyright (C) 2001, 2002 Yury Umanets <torque@ukrpost.net>, see COPYING for 
	licensing and copyright details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <dal/file_dal.h>

#include <reiserfs/reiserfs.h>
#include <reiserfs/libprogs_gauge.h>

static unsigned long leaf_count, internal_count;

static long node_func(reiserfs_block_t *node, void *data) {
	reiserfs_gauge_t *gauge = (reiserfs_gauge_t *)data;

	if (is_internal_node(node))
		internal_count++;
	else if (is_leaf_node(node))
		leaf_count++;
	else {		
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
	    	"Invalid node type has detected.");
		return 0;
	}

	if (gauge)
	    libreiserfs_gauge_touch(gauge);
	
	return 1;
}

int main(int argc, char *argv[]) {
	dal_t *dal;
	reiserfs_fs_t *fs;
	reiserfs_gauge_t *gauge;
	
	int traverse_res;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s DEV\n", argv[0]);
		return 0xff;
	}

	if (!(dal = file_dal_open(argv[1], DEFAULT_BLOCK_SIZE, O_RDONLY))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
	    	"Couldn't open device %s.", argv[1]);
		return 0xfe;    
	}
	
	if (!(fs = reiserfs_fs_open_fast(dal, dal))) {
		libreiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL, 
	    	"Couldn't open filesystem on %s.", argv[1]);
		goto error_free_dal;
	}
	
	leaf_count = 0, internal_count = 0;

	gauge = progs_gauge_create();
	libreiserfs_gauge_undetermined(gauge);
	libreiserfs_gauge_set_name(gauge, "traversing");
	
	traverse_res = reiserfs_tree_simple_traverse(reiserfs_fs_tree(fs), gauge, node_func);
	
	if (gauge) {
		libreiserfs_gauge_done(gauge);
		progs_gauge_free(gauge);
	}	
	
	if (traverse_res)
		fprintf(stderr, "leaves: %lu\ninternals: %lu\n", leaf_count, internal_count);

	reiserfs_fs_close(fs);
	file_dal_close(dal);
	
	return 0;
	
error_free_dal:
	file_dal_close(dal);
error:
	return 0xff;    
}

