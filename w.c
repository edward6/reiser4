/* temporary file for transaction writer code */

#include "reiser4.h"

/* each log record (one reiser4 block in size) has unified format with log
 * record header followed by an array of log entries */

struct log_record_header {
	d64      id;		/* transaction sequential number */
	d32      total;		/* total number of log records in current
				 * transaction  */
	d32      serial;	/* this block number in transaction */
	d64      prev_block;	/* number of previous block in commit */
};

/* rest of log record is filled by these log entries, unused space filled by
 * zeroes */
struct log_entry {
	d64      original;	/* block original location */
	d64      wandered;	/* block wandered location */
};

/* journal header uses dedicated device block for storing id of last
 * completely flushed transaction */
struct journal_header {
	d64      last_flushed_id;
	d64      first;
};

/* jload/jwrite/junload give a bread/bwrite/brelse functionality for jnodes */
/* jnode ref. counter is missing, it doesn't matter for us because this
 * journal writer uses those jnodes exclusively by only one thread */
 /* FIXME: it should go to other place */
int jload (jnode * node)
{
	reiser4_tree * tree = current_tree;
	int (*read_node) ( reiser4_tree *, jnode *);

	assert ("zam-441", tree->ops);
	assert ("zam-442", tree->ops->read_node != NULL);

	read_node = tree->ops->read_node;

	return read_node (tree, node);
}

int jwrite (jnode * node)
{
	struct page * page;

	assert ("zam-445", node != NULL);
	assert ("zam-446", jnode_page (node) != NULL);

	page = jnode_page (node);

	assert ("zam-450", blocknr_is_fake (jnode_get_block (node)));

	return page_io (page, WRITE, GFP_NOIO);
}

int jwait_io (jnode * node)
{
	struct page * page;

	assert ("zam-447", node != NULL);
	assert ("zam-448", jnode_page (node) != NULL);

	page = jnode_page (node);

	if (!PageUptodate (page)) {
		wait_on_page_locked (page);

		if (!PageUptodate (page)) return -EIO;
	} else {
		unlock_page (page);
	}

	return 0;
}

int junload (jnode * node)
{
	reiser4_tree * tree = current_tree;
	int (*release_node) ( reiser4_tree *, jnode *);

	assert ("zam-443", tree->ops);
	assert ("zam-444", tree->ops->release_node != NULL);

	release_node = tree->ops->release_node;

	return release_node (tree, node);
}


/* log record capacity depends on current block size */
static int log_record_capacity (struct super_block * super)
{
	return (super->s_blocksize - sizeof (struct log_record_header)) /
		sizeof (struct log_entry);
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

static void format_log_record (jnode * node,
			       int total, 
			       int serial,
			       const reiser4_block_nr * prev_block)
{
	struct super_block * super = reiser4_get_current_sb ();
	struct log_record_header * h = (struct log_record_header*) jdata (node); 

	assert ("zam-438", h != NULL);

	xmemset (jdata(node), 0, super->s_blocksize);

	cputod64((__u64)reiser4_trans_id(super), &h->id);
	cputod32((__u32)total, & h->total);
	cputod32((__u32)serial,& h->serial);
	cputod64((__u64)(*prev_block), & h->prev_block);
}


static void store_entry (jnode * node, 
			 int index,
			 const reiser4_block_nr * a,
			 const reiser4_block_nr *b)
{
	char * data;
	struct log_entry * pairs;

	data = jdata (node);
	assert ("zam-451", data != NULL);

	pairs = (struct log_entry *)(data + sizeof (struct log_record_header));

	cputod64(*a, & pairs[index].original);
	cputod64(*b, & pairs[index].wandered);
}

static int count_wmap_actor (txn_atom * atom      UNUSED_ARG,
			     const reiser4_block_nr * a UNUSED_ARG,
			     const reiser4_block_nr * b UNUSED_ARG,
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

	tx_size = (wmap_size - 1) / log_record_capacity (super) + 1;

	return tx_size;
}


static int get_space_for_tx (int tx_size)
{
	int ret;

	while (1) {
		reiser4_block_nr not_used;

		ret = reiser4_reserve_blocks 
			(&not_used, (reiser4_block_nr)tx_size, (reiser4_block_nr)tx_size);

		if (ret == 0) break;

		if (ret == -ENOSPC) { 
			// force_write_back_completion ();
			// wait_for_space_available();
		} else {
			return ret;
		}
	}

	return 0;
}

/* allocate given number of nodes over the journal area and link them into a
 * list, return pinter to the first jnode in the list */
static int alloc_tx (capture_list_head * head, int nr)
{
	reiser4_block_nr allocated = 0;
	reiser4_blocknr_hint hint;
	reiser4_block_nr prev;

	int serial = 0;

	int ret;

	while (allocated < (unsigned)nr) {
		reiser4_block_nr first, len = nr;
		int j;

		blocknr_hint_init (&hint);
		/* FIXME: there should be some block allocation policy for
		 * nodes which contain log records */
		ret = reiser4_alloc_blocks (&hint, &first, &len);
		blocknr_hint_done (&hint);

		if (ret != 0) goto fail;

		allocated += len;

		/* create jnodes for all log records */
		for (j = 0; (unsigned)j < len; j++) {
			jnode * jal;

			jal = jnew ();

			if (jal == NULL) {
				ret = -ENOMEM;
				goto fail;
			}

			jal->blocknr = first;

			ret = jload(jal);

			if (ret != 0) {
				jfree (jal);
				goto fail;
			}

			format_log_record (jal, nr, serial, &prev);

			capture_list_push_front (head, jal);

			prev = first;

			first ++;
		}
	}

	return 0;

 fail:
	while (!capture_list_empty (head)) {
		jnode * node = capture_list_pop_back(head);

		junload (node);
		jfree (node);
	}

	return ret;
}

struct store_wmap_params {
	jnode * cur;		/* jnode of current log record to fill */
	int     idx;		/* free element index in log record  */
	int     capacity;	/* capacity  */

#if REISER4_DEBUG
	capture_list_head* tx_list;
#endif
};

static int store_wmap_actor (txn_atom * atom UNUSED_ARG,
			     const reiser4_block_nr * a,
			     const reiser4_block_nr * b,
			     void * data)
{
	struct store_wmap_params * params = data;

	if (params->idx >= params->capacity) {
		/* a new log record should be taken from the tx_list */
		params->cur = capture_list_next (params->cur);
		assert ("zam-454", !capture_list_end(params->tx_list, params->cur));

		params->idx = 0;
	}

	store_entry (params->cur, params->idx, a, b);
	params->idx ++;

	return 0;
}

static void fill_tx (capture_list_head * tx_list,struct super_block * super)

{
	struct store_wmap_params params;
	txn_atom * atom;

	assert ("zam-452", !capture_list_empty(tx_list));

	params.cur = capture_list_back (tx_list);
	params.idx = 0;
	params.capacity = log_record_capacity (super);

	atom = get_current_atom_locked ();
	blocknr_set_iterator (atom, &atom->wandered_map, &store_wmap_actor, &params , 0);
	spin_unlock_atom (atom);
}


static int write_tx (capture_list_head * tx_list)
{
	jnode * cur;
	int     ret;

	assert ("zam-456", !capture_list_empty(tx_list));

	cur = capture_list_back (tx_list);

	while (capture_list_end (tx_list, cur)) {
		ret = jwrite (cur);

		if (ret != 0) return ret;
	}

	cur = capture_list_back (tx_list);

	while (capture_list_end (tx_list, cur)) {
		ret = jwait_io (cur);

		junload (cur);	/* free jnode page */

		if (ret != 0) return ret;
	}

	/* Note: We keep jnodes in memory until atom flush completes, them
	 * update journal header or super block and free blocks occupied by
	 * wandered set and log records */

	return 0;
}

/* I assume that at this moment that all captured blocks from RELOCATE SET are
 * written to disk to new locations, all blocks from OVERWRITE SET are written
 * to wandered location, WANDERED MAP is created, DELETED SET exists. */

int reiser4_write_logs (void)
{
	capture_list_head tx_list;

	int               tx_size;
	int               ret;

	struct super_block * super = reiser4_get_current_sb();

	tx_size = get_tx_size (super);

	get_space_for_tx(tx_size);

	/* allocate all space in journal area that we need, connect jnode into
	 * a list using capture link fields */

	capture_list_init (&tx_list);

	ret = alloc_tx (&tx_list, tx_size);

	if (ret) return ret;

	fill_tx (&tx_list, super);

	ret = write_tx (&tx_list);

	if (ret != 0) return ret;

	{
		space_allocator_plugin * splug;

		splug = get_current_super_private()->space_plug;
		assert ("zam-457", splug != NULL);

		if (splug->post_commit_hook != NULL)
			splug->post_commit_hook ();
	}

	return 0;
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
