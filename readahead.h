/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __READAHEAD_H__
#define __READAHEAD_H__

/* reiser4 super block is a field of this type. It controls readahead during tree traversals */
typedef struct formatted_read_ahead_params {
	int max;
	int adjacent_only;
	int leaves_only;
	int one_parent_only;
} ra_params_t;

typedef enum {
	RA_READDIR,
	RA_READFILE
} ra_type;


typedef struct read_ahead_info {
	ra_type type;
	union {
		struct {
			oid_t oid;
		} readdir;
	} u;
} ra_info_t;

/* readahead whole directory and all its stat datas */
void readdir_readahead(znode *node, ra_info_t *info);

/* __READAHEAD_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
