/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Slum data-types. See slum_track.c for description.
 */

#if !defined( __SLUM_H__ )
#define __SLUM_H__

typedef enum {
	SLUM_BEING_SQUEEZED       = (1 << 0),
	SLUM_WAS_SQUEEZED         = (1 << 1),
} slum_flags;

/** Slum is set of dirty nodes contiguous in a tree order. Slums are
    tracked dymanically. Locking: slum pointers and lists are protected
    by ->lock spinlock in a znode tree slums are in. */
typedef struct slum {
	/** flags of slum */
	__u32                       flags;
	/** leftmost node in this slum. They are pinned, that is we call
	    zget() on this, if this proves too expensive, there are two
	    possible strategies:
	    
	     - if we never flush early, any node added into slum will
	     stay in memory, at least until slum is squuzed.
	*/
	znode                      *leftmost;
	/** total "estimated" free space. This is not required to be
	    exact. If we err here, super balancing will be sub
	    optimal. */
	unsigned                    free_space;
	unsigned                    num_of_nodes;

	/** maybe this will only be used for assertions, but... */
	txn_atom                   *atom;
} slum;

extern int slums_init( void );
extern int slums_done( void );
extern int balance_slum( slum *hood );
extern int flush_slum ( slum *hood);
extern int add_to_slum( znode *node );
extern int slum_likely_squeezable( slum *hood );
extern void delete_node_from_slum (znode *node);
extern int delete_from_slum( znode *node );
extern int delete_from_slum_locked( znode *node );
extern int is_in_slum( znode *node, slum *hood );
extern void slum_merge_neighbors( znode *node, txn_atom *dying, txn_atom *growing );

/* __SLUM_H__ */
#endif

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */

