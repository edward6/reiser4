/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

extern __u64 oid_used          ( void );
extern __u64 oid_next          ( void );

extern int  oid_allocate       ( oid_t *);
extern int  oid_release        ( oid_t  );

extern void oid_count_allocated( void );
extern void oid_count_released ( void );

extern int  oid_init_allocator (const struct super_block *,  __u64, __u64);
extern void oid_print_allocator(const char * prefix, const struct super_block *);

/* identifiers of available objectid managers */
typedef enum {
	/* default for reiser 4.0 oid manager id */
	OID_40_ALLOCATOR_ID,
	LAST_OID_ALLOCATOR_ID
} oid_allocator_id;


/* this object is part of reiser4 private in-core super block */
struct reiser4_oid_allocator {
	union {
		oid_40_allocator oid_40;
	} u;
};

extern reiser4_plugin oid_plugins[ LAST_OID_ALLOCATOR_ID ];
