/*
 * Copyright 2002 by Hans Reiser
 */


/* This file contains code for atom's DELETED SET support */

#include "reiser4.h"

/* The proposed data structure for storing of atom's DELETED SET is a list of
 * special elements each one of them may contain array of block number or/and
 * array of extent-like items. That element called DSLE (Deleted Set List
 * Element) is used to store block numbers from the beginning and for extents
 * from the end of the data field (char data[...]). The ->nr_blocks and
 * ->nr_extents fields count numbers of blocks and extents.
 *
 * +------------------------ DSLE->data ------------------------+
 * |block1|block2| ... <free space> ... |extent3|extent2|extent1|
 * +------------------------------------------------------------+
 *
 * When current DSLE is full and it can't contain more items of type we are
 * trying to insert we allocate new DSLE and put it at the front of atom's
 * DSLE list.
 */

TS_LIST_DEFINE(dsle, struct txn_dsle, linkage)

#define DSLE_DATA_SIZE 100

/* DSLE -- Deleted Set List Element.  */
struct txn_dsle {
	dsle_list_link linkage;
	int           nr_blocks;
	int           nr_extents;
	char          data[DSLE_DATA_SIZE];
};
typedef struct txn_dsle txn_dsle_t;

struct dsle_extent {
	reiser4_block_nr  start;
	reiser4_block_nr  len;
};

#define DSLE_BLOCKNR_SIZE               sizeof(reiser4_block_nr)
#define DSLE_EXTENT_SIZE                sizeof(struct dsle_extent)

#define DSLE_USED_SPACE(dsle)            ((dsle)->nr_blocks * DSLE_BLOCKNR_SIZE + (dsle)->nr_extents * DSLE_EXTENT_SIZE)
#define DSLE_FREE_SPACE(dsle)            (assert("zam-406", DSLE_DATA_SIZE >= DSLE_USED_SPACE(dsle)), \
                                         DSLE_DATA_SIZE - DSLE_USED_SPACE(dsle))

#define DSLE_HAS_SPACE_FOR_EXTENT(dsle)  (DSLE_FREE_SPACE(dsle) >= DSLE_EXTENT_SIZE)
#define DSLE_HAS_SPACE_FOR_BLOCKNR(dsle) (DSLE_FREE_SPACE(dsle) >= DSLE_BLOCKNR_SIZE)

static void dsle_init(txn_dsle_t * dsle)
{
	dsle->nr_blocks = 0;
	dsle->nr_extents = 0;
	dsle_list_clean(dsle);
}

txn_dsle_t * dsle_alloc (void)
{
	txn_dsle_t * e;

	if ((e = (txn_dsle_t *)kmalloc(sizeof (txn_dsle_t), GFP_KERNEL)) == NULL)
		return ERR_PTR(-NOMEM);

	dsle_init(e);

	return e;
}

void * dsle_free (txn_dsle_t * dsle)
{
	kfree(dsle);
}

/** initialization of atom's DELETED SET list head */
void atom_ds_init (txn_atom * atom)
{
	dsle_list_init(&atom->deleted_list);
}

/** deletion of atom's DELETED SET dsle list */
void atom_ds_done (txn_atom * atom)
{
	while (dsle_list_empty(&atom->deleted_list)) {
		{
			txn_dsle_t * e;

			e = dsle_list_front(&atom->deleted_list);
			dsle_list_remove(e);
			dsle_free(e);
		}
	}
}

/** add one more block number to given DSLE */
static void put_blocknr (txn_dsle_t * e, reiser4_block_nr * block)
{
	reiser4_block_nr * target;

	target = (reiser4_block_nr*)(e->data + (e->nr_blocks));
	*target = *block;
	e->nr_blocks ++;
}

/** add one more extent to given DSLE */
static void put_extent (txn_dsle_t * e, reiser4_block_nr * start, reiser4_block_nr * len)
{
	reiser4_block_nr * target;

	e->nr_extents ++;
	target = (reiser4_block_nr*)(e->data + DSLE_DATA_SIZE - (e->nr_extents));
	target->start = *block;
	target->len   = *len;
}

/** adding a range of blocks to atom's DELETED SET. It should be called under
 * atom's spin lock held */
int add_blocknrs_to_ds (txn_atom * atom, const reiser4_block_nr * start, const reiser4_block_nr * len)
{
	txn_dsle_t * e = NULL;
	int space_needed;
	int err = 0;

	space_needed = (len->blk == 1) ? DSLE_BLOCKNR_SIZE : DSLE_EXTENT_SIZE;

	if (dsle_list_empty(&atom->deleted_list) 
	    || DSLE_FREE_SPACE(dsle_list_front(&atom->deleted_list)) > space_needed)
	{
		tnx_dsle_t * new_dsle;

		/* we release spinlock before doing memory allocation */
		spin_unlock_atom(atom);
		new_dsle = dsle_alloc();
		spin_lock_atom(atom);

		if (IS_ERR(new_dsle)) return PTR_ERR(new_dsle);

		/* check last atom's dsle for free space again, the situation
		 * might be changed while we slept */
		assert("zam-407", !dsle_list_empty(&atom->deleted_list));

		if (!(DSLE_FREE_SPACE(dsle_list_front(&atom->deleted_list) > space_needed)))
		{
			dsle_list_push_front(&atom->deleted_list, new_dsle);
		} else {
			dsle_free(new_dsle);
		}
	}

	if (len->blk == 1) {
		put_blocknr(dsle_list_front(&atom->deleted_list), start);
	} else {
		put_extent (dsle_list_front(&atom->deleted_list), start, len);
	}

	return 0;
}

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
