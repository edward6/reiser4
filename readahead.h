/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __READAHEAD_H__
#define __READAHEAD_H__


typedef enum {
	RA_ADJACENT_ONLY = 1,       /* only requests nodes which are adjacent. Default is NO (not only adjacent) */
	RA_ALL_LEVELS = 2,	    /* only request readahead for children of twin nodes. Default is NO (leaves only) */
	RA_CONTINUE_ON_PRESENT = 4, /* when one of nodes to be readahead is in memory already, skip it, but continue
				       readahead submission. Default is NO (stop submission) */
	RA_READ_ON_GRN = 8          /* looking for right neighbor read all necessary nodes. Default is NO (use cache
				       only) */
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
