/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

#include "../key.h"

#define MIN_CLUSTER_SIZE 4096

/* per-node cluster cache */
typedef struct cluster_head cluster_head;
struct cluster_head {
	unsigned char *c_data;
	struct inode *c_inode;   /* for a file plugin, data processing plugins, secret key,
				    sd-key, etc... */ 
	loff_t c_offset;
	unsigned int c_flags;
	cluster_head *c_next;
};
/* position in the ordered linked list */
typedef struct cluster_pos cluster_pos;
struct cluster_pos {
	cluster_head *prev;
	cluster_head *next;
	reiser4_key key;
	reiser4_key next_key;
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
