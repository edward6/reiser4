/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */


/* identifiers of available objectdid managers */
typedef enum {
	/* default for reiser 4.0 oid manager id */
	OID_40_ALLOCATOR_ID,
	LAST_OID_ALLOCATOR_ID
} oid_40_allocator_id;


/* this is part of reiser4 private in-core super block */
struct reiser4_oid_allocator {
	/**
	 * spinlock serializing accesses to this structure.
	 */
	spinlock_t oguard;

	union {
		oid_40_allocator oid_40;
	} u;
};

extern reiser4_plugin oid_plugins[ LAST_OID_ALLOCATOR_ID ];
