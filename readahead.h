/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __READAHEAD_H__
#define __READAHEAD_H__

 /* readahead whole directory and all its stat datas */
#define RA_READDIR 1

typedef struct read_ahead_info {
	int flags;
	union {
		struct {
			oid_t oid;
		} readdir;
	} u;
} ra_info_t;

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
