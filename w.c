/* temporary file for transaction writer code */

/* each log record (one reiser4 block in size) has unified format with log
 * record header followed by an array of wandered pairs */

struct log_record_header {
	d64      id;		/* transaction sequential number */
	d32      total;		/* total number of log records in current
				 * transaction  */
	d32      serial;	/* number of blocks in  */
} __attribute__ ((__packed__));

struct wandered_pair {
	d64      oririnal;	/* block original location */
	d64      wandered;	/* block wandered location */
} __attribute__ ((_packed__));

struct journal_header {
	d64      last_flushed_id;
} __attribute__ ((__packed__));

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

static int estimate_space (void)
{
	txn_atom * atom;
	int wmap_size = 0;

	atom = get_current_atom_locked();

	blocknr_set_iterator(atom, &atom->wandered_map, count_wmap_actor, &wmap_size, 0);

	spin_unlock_atom(atom);

	return wmap_size + 1;	/* + 1 for final commit record */
}


static int store_wmap_actor (txn_atom * atom,
			     reiser4_block_nr * a,
			     reiser4_block_nr * b,
			     void * data)
{
	jnode ** cur_jnode = data;

	return 0;
}

/* I assume that at this moment that all captured blocks from RELOCATE SET are
 * written to disk to new locations, all blocks from OVERWRITE SET are written
 * to wandered location, WANDERED MAP is created, DELETED SET exists. */

int commit (txn_atom * atom)
{
	int space_needed;

	reiser4_context * cx = get_current_context();

	jnode * cur = NULL;
	txn_atom * atom;
	txn_handle * txnh; 

	/* This atom should be in COMMIT state which prevents any attempt to
	 * fuse with him. So, we can safely spin unlock the atom. ?  */

	space_needed = estimate_space (super);

	while (space_needed > space_available (super)) { 

		force_write_back_completion ();

		// Is a condition variable needed?
		sleep_on( no_space_in_journala_wq);
	}

	/* allocate all space in journal area that we need, connect jnode into
	 * a list using capture link fields */

	for (;;);

	/* format log blocks from WANDERED MAP & DELETE SET, allocate blocks
	 * over journal area if it is needed. */

	atom = get_current_atom_locked ();

	blocknr_set_iterator (atom, &atom->wandered_map, store_wmap_actor, &cur_jnode, 0);

	spin_unlock_atom (atom);
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
