/* temporary file for transaction writer code */

#include "reiser4.h"

/* each log record (one reiser4 block in size) has unified format with log
 * record header followed by an array of wandered pairs */

struct log_record_header {
	d64      id;		/* transaction sequential number */
	d32      total;		/* total number of log records in current
				 * transaction  */
	d32      serial;	/* this block number in transaction */
	d64      prev_block;	/* number of previous block in commit */
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
	d64      first
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

		if (!PageUptodata (page)) return -EIO;
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

static void format_log_record (jnode * node,
			       int total, 
			       int serial,
			       const reiser4_block_nr * prev_block)
{
	struct super_block * super = reiser4_get_current_sb ();
	struct log_record_header * h = (struct log_record_header*) jdata (node); 

	assert ("zam-438", h != NULL);

	xmemset (jdata(node), 0, super->s_blocksize);

	h -> id     = reiser4_trans_id(super);
	h -> total  = total;
	h -> serial = serial;
	h -> prev   = *prev_block;
}
static void store_wpair (jnode * node, 
			 int index,
			 const reiser4_block_nr * a,
			 const reiser4_block_nr *b)
{
	char * data;
	struct wandered_pair * pairs;

	data = jdata (node);
	assert ("zam-451", data != NULL);


	
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


static int get_space_for_tx (int tx_size)
{
	while (1) {
		reiser4_block_nr not_used;

		ret = reiser4_reserve_blocks 
			(&not_used, (reiser4_block_nr)tx_size, (reiser4_block_nr)tx_size);

		if (ret == 0) break;

		if (ret == -ENOSPC) { 
			force_write_back_completion ();
			wait_for_space_available();
		} else {
			return ret;
		}
	}

	return 0;
}

/* allocate given number of nodes over the journal area and link them into a
 * list, return pinter to the first jnode in the list */
static int alloc_tx (capture_list_head * head, struct super_block * super, int nr)
{
	reiser4_block_nr allocated = 0;
	reiser4_blocknr_hint hint;
	reiser4_block_nr prev;

	int serial = 0;

	int ret;

	while (allocated < nr) {
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
		for (j = 0; j < len; j++) {
			jnode * jal;
			struct log_record_header * h;


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

			format_log_record (jal, nr, serial, prev_block);

			capture_list_push_front (head, jal)l;
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

static int store_wmap_actor (txn_atom * atom,
			     reiser4_block_nr * a,
			     reiser4_block_nr * b,
			     void * data)
{
	struct store_wmap_params * params = data;

	if (params->idx >= params->capacity) {
		/* a new log record should be taken from the tx_list */
		jnode->cur = capture_list_next (jnode->cur);
		assert ("zam-454", !capture_list_end(params->tx_list, jnode->cur));

		param->idx = 0;
	}

	store_wpair (node, params->idx, a, b);
	params->idx ++;

	return 0;
}

static void fill_tx (capture_list_head * tx_list,struct super_block * super)

{
	struct store_wmap_params params;

	assert ("zam-452", !capture_list_empty(tx_list));

	param.cur = capture_list_back ();
	param.idx = 0;
	param.capacity = get_log_record_capacity (super);

	atom = get_current_atom_locked ();
	blocknr_set_iterator (atom, &atom->wandered_map, store_wmap_actor, &params , 0);
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

	return 0;
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

	tx_size = get_tx_size (super);

	get_space_for_tx(tx_size);

	/* allocate all space in journal area that we need, connect jnode into
	 * a list using capture link fields */

	capure_list_init (&tx_list);

	ret = alloc_tx (&tx_list, super, tx_size);

	if (ret) return ret;

	fill_tx (&tx_list, super);

	ret = write_tx (&tx_list);

	/* We keep jnodes in memory until atom flush completes, them update
	 * journal header or super block and free blocks occupied by wandered
	 * set and log records */

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
