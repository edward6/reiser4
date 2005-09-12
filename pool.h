/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Fast pool allocation */

#ifndef __REISER4_POOL_H__
#define __REISER4_POOL_H__

#include <linux/types.h>

typedef struct reiser4_pool {
	size_t obj_size;
	int objs;
	char *data;
	struct list_head free;
	struct list_head used;
	struct list_head extra;
} reiser4_pool;

typedef struct reiser4_pool_header {
	/* object is either on free or "used" lists */
	struct list_head usage_linkage;
	struct list_head level_linkage;
	struct list_head extra_linkage;
} reiser4_pool_header;

typedef enum {
	POOLO_BEFORE,
	POOLO_AFTER,
	POOLO_LAST,
	POOLO_FIRST
} pool_ordering;

/* pool manipulation functions */

extern void reiser4_init_pool(reiser4_pool * pool, size_t obj_size,
			      int num_of_objs, char *data);
extern void reiser4_done_pool(reiser4_pool * pool);
extern void reiser4_pool_free(reiser4_pool * pool, reiser4_pool_header * h);
reiser4_pool_header *add_obj(reiser4_pool * pool, struct list_head * list,
			     pool_ordering order,
			     reiser4_pool_header * reference);

/* __REISER4_POOL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
