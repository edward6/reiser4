/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Declaration of lnode (light-weight node).
 */

#ifndef __LNODE_H__
#define __LNODE_H__

typedef enum {
	LNODE_INODE,
	LNODE_PSEUDO,
	LNODE_LW
} lnode_type;

typedef struct lnode_header {
	__u8  type;
	__u8  flags;
} lnode_header;

typedef union lnode lnode;

typedef struct lw_lock_holder lw_lock_holder;

/** declare hash table of lnode_lw's */
TS_HASH_DECLARE( lw, lw_lock_holder );

struct lw_lock_holder {
	struct semaphore lock;
	lw_hash_link     link;
};

typedef struct lnode_lw {
	lnode_header h;
	lw_lock_holder lock;
	reiser4_key    key;
} lnode_lw;

typedef struct lnode_pseudo {
	lnode_header h;
	lw_lock_holder lock;
	lnode         *host;
	/* something to identify pseudo file type, like name or plugin */
} lnode_pseudo;

union lnode {
	lnode_header h;
	lnode_lw     lw;
	lnode_pseudo pseudo;
};

extern struct inode *inode_by_lnode( const lnode *node );
extern reiser4_key *lnode_key( const lnode *node, reiser4_key *result );

/* __LNODE_H__ */
#endif

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
