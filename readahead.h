/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __READAHEAD_H__
#define __READAHEAD_H__


typedef enum {
	RA_ADJACENT_ONLY = 1,       /* only requests nodes which are adjacent. Default is NO (not only adjacent) */
} ra_global_flags;

/* reiser4 super block has a field of this type. It controls readahead during tree traversals */
typedef struct formatted_read_ahead_params {
	unsigned long max; /* request not more than this amount of nodes. Default is totalram_pages / 4 */
	int flags;
} ra_params_t;


typedef struct {
	reiser4_key key_to_stop;
} ra_info_t;

void formatted_readahead(znode *, ra_info_t *);
void init_ra_info(ra_info_t * rai);

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
