#if !defined (__REISER4_DISKMAP_H__)
#define __REISER4_DISKMAP_H__

#include "dformat.h"

#define REISER4_FIXMAP_MAGIC "R4FiXMaPv1.0"

#define REISER4_FIXMAP_END_LABEL -2
#define REISER4_FIXMAP_NEXT_LABEL -1

/* This is diskmap table, it's entries must be sorted ascending first in label order,
   then in parameter order.
   End of table is marked with label REISER4_FIXMAP_END_LABEL
   label REISER4_FIXMAP_NEXT_LABEL means that value in this row contains
   disk block of next diskmap in diskmaps chain */
struct reiser4_diskmap {
	char magic[16];
	struct {
		d32 label;
		d32 parameter;
		d64 value;
	} table[0];
};

int reiser4_get_diskmap_value( u32, u32, u64 *);


#endif
