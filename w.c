/* temporary file for transaction writer code */

#include "reiser4.h"

/* each log record (one reiser4 block in size) has unified format with log
 * record header followed by an array of wandered pairs */

struct log_record_header {
	d64      id;		/* transaction sequential number */
	d32      total;		/* total number of log records in current
				 * transaction  */
	d32      serial;	/* this block number in transaction */
};

/* rest of log record is filled by these wandered pairs, unused space filled
 * by zeroes */
struct wandered_pair {
	d64      oririnal;	/* block original location */
	d64      wandered;	/* block wandered location */
};

/* journal header uses dedicated device block for storing id of last
 * completely flushed transaction */
struct journal_header {
	d64      last_flushed_id;
};

/* log record capacity depends on current block size */
static int log_record_capacity (struct super_block * super)
{
	return (super->s_blocksize - sizeof (struct log_record_header)) /
		sizeof (struct wandered_pair);
}

/* FIXME: It should go to txnmgr.c */
static txn_atom * get_current_atom_locked (void)
{
	reiser4_context * cx;
	txn_atom * atom;
	txn_handle * txnh; 

	cx = get_current_context();
	assert ("zam-437", cx != NULL);

	txnh = cx -> trans;
	assert ("zam-435", txnh != NULL);
	
	atom = atom_get_locked_by_txnh (txnh);
	assert ("zam-436", atom != NULL);

	return atom;
}

static int space_available (struct super_block * super)
{
	
}

static jnode * get_next_journal_node (struct super_block * super)
{
	
}

static void format_log_record (jnode * node, int total, int serial)
{
	struct super_block * super = reiser4_get_current_sb ();
	struct log_record_header * h = (struct log_record_header*) jdata (node); 

	assert ("zam-438", h != NULL);

	xmemset (jdata(node), 0, super->s_blocksize);

	h -> id     = reiser4_trans_id(super);
	h -> total  = total;
	h -> serial = serial;
}

static int count_wmap_size_actor (txn_atom * atom,
				  reiser4_block_nr * a,
				  reiser4_block_nr * b,
				  void * data)
{
	int * count = data;

	(*count) ++;

	return 0;
}

/* currently, log records contains contain only wandered map, and transaction
 * size in log blocks depends only on size of transaction wandered map */
static int get_tx_size (struct super_block * super)
{
	txn_atom * atom;
	int wmap_size = 0;
	int tx_size;

	atom = get_current_atom_locked();

	/* FIXME: this is a bit expensive */
	blocknr_set_iterator(atom, &atom->wandered_map, count_wmap_actor, &wmap_size, 0);

	spin_unlock_atom(atom);

	/* FIXME: seems overwrite set can't be null */
	assert ("zam-440", wmap_size != 0);

	tx_size = (wmap_size - 1) / get_log_record_capacity (super) + 1;

	return tx_size;
}


/* allocate given number of nodes over the journal area and link them into a
 * list, return pinter to the first jnode in the list */
static int alloc_tx (capture_list_head * head, struct super_block * super, int nr)
{
	
}

static int store_wmap_actor (txn_atom * atom,
			     reiser4_block_nr * a,
			     reiser4_block_nr * b,
			     void * data)
{
	jnode ** cur_jnode = data;

	return 0;
}

static void fill_tx (capture_list_head * tx_list,struct super_block * super)

{
	atom = get_current_atom_locked ();
	blocknr_set_iterator (atom, &atom->wandered_map, store_wmap_actor, tx_list /* ??? */, 0);
	spin_unlock_atom (atom);
}

/* I assume that at this moment that all captured blocks from RELOCATE SET are
 * written to disk to new locations, all blocks from OVERWRITE SET are written
 * to wandered location, WANDERED MAP is created, DELETED SET exists. */

int commit_tx (void)
{
	capture_list_head tx_list;

	int               tx_size;
	int               ret;

	struct super_block * super = reiser4_get_current_sb();

	/* This atom should be in COMMIT state which prevents any attempt to
	 * fuse with him. So, we can safely spin unlock the atom. ?  */

	tx_size = get_tx_size (super);

	while (tx_size > space_available (super)) { 

		force_write_back_completion ();

		// Is a condition variable needed?
		sleep_on( no_space_in_journala_wq);
	}

	/* allocate all space in journal area that we need, connect jnode into
	 * a list using capture link fields */

	capure_list_init (&tx_list);

	ret = alloc_tx (&tx_list, super, tx_size);

	if (ret) return ret;

	/* format log blocks from WANDERED MAP & DELETE SET, allocate blocks
	 * over journal area if it is needed. */

	fill_tx (&tx_list, super);

	ret = write_tx (super, &tx_list);

	return ret;
}

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * End:
 */
