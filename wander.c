/* Copyright 2002 by Hans Reiser */

/* Reiser4 log writer is here */

#include "reiser4.h"

/* journal header and footer are stored in two dedicated device blocks and
 * contain block numbers of first block of last committed transaction and
 * block number of first block of for oldest not flushed transaction. */

/* FIXME: we will see, can those blocks be combined together in one block or
 * can their data be put into reiser4 super block. */

/* Journal header block get updated after all log records of a transaction is
 * beign committed are written  */

/* Note: the fact that not a whole fs block is used may make reiser4 journal
 * more reliable because used part of fs block is less that 512 bytes which
 * write operation can be considered as an atomic operation. */

/* FIXME: I think that `write atomicity' means atomic write of block with
 * sizes which are supported by the hardware */

/* Journal footer gets updated after one transaction is flushed (all blocks
 * are written in place) and _before_ wandered blocks and log records are
 * freed in WORKING bitmap */

struct txn_atom_extra {
	struct semaphore io_sema;
	atomic_t         nr_submitted;
	atomic_t         nr_errors;
};

static int submit_write (jnode*, int, const reiser4_block_nr *, struct txn_atom_extra *);

static const d64 *get_last_committed_tx (struct super_block *s)
{
	struct reiser4_super_info_data * private;
	struct journal_header * h;

	private = get_super_private(s);
	assert ("zam-477", private != NULL);
	assert ("zam-478", private->journal_header != NULL);

	jload_and_lock (private->journal_header);

	h = (struct journal_header*)jdata(private->journal_header);
	assert ("zam-485", h != NULL);

	junlock_and_relse (private->journal_header);

	return &h->last_committed_tx; 
}

static void set_last_committed_tx (struct super_block *s, const d64 * block)
{
	struct reiser4_super_info_data * private;
	struct journal_header * h;

	private = get_super_private(s);
	assert ("zam-479", private != NULL);
	assert ("zam-480", private->journal_header != NULL);

	jload_and_lock (private->journal_header);

	h = (struct journal_header*)jdata(private->journal_header);
	assert ("zam-484", h != NULL);

	junlock_and_relse (private->journal_header);

	h->last_committed_tx = *block;
}

static const d64 *get_last_flushed_tx (struct super_block * s)
{
	struct reiser4_super_info_data * private;
	struct journal_footer * h;

	private = get_super_private(s);
	assert ("zam-481", private != NULL);
	assert ("zam-482", private->journal_footer != NULL);

	jload_and_lock (private->journal_footer);

	h = (struct journal_footer*)jdata(private->journal_footer);
	assert ("zam-483", h != NULL);

	junlock_and_relse (private->journal_footer);

	return &h->last_flushed_tx;
}

static void set_last_flushed_tx (struct super_block *s, const d64 *block)
{
	struct reiser4_super_info_data * private;
	struct journal_footer * h;

	private = get_super_private(s);
	assert ("zam-493", private != NULL);
	assert ("zam-494", private->journal_header != NULL);

	jload_and_lock (private->journal_footer);

	h = (struct journal_footer*)jdata(private->journal_footer);
	assert ("zam-495", h != NULL);

	junlock_and_relse (private->journal_footer);

	h->last_flushed_tx = *block;
}

/* log record capacity depends on current block size */
static int log_record_capacity (const struct super_block * super)
{
	return (super->s_blocksize - sizeof (struct log_record_header)) /
		sizeof (struct log_entry);
}

static void format_tx_head (
	jnode * node,
	int total,
	const reiser4_block_nr * next)
{
	struct super_block * super = reiser4_get_current_sb();
	struct tx_header * h;

	assert ("zam-459", node != NULL);

	h = (struct tx_header*)jdata(node);

	assert ("zam-460", h != NULL);
	assert ("zam-462", super -> s_blocksize >= sizeof (struct tx_header));

	xmemset (jdata(node), 0, (size_t)super->s_blocksize);
	xmemcpy (jdata(node), TX_HEADER_MAGIC, TX_HEADER_MAGIC_SIZE); 

	// cputod64((__u64)reiser4_trans_id(super), &h->id);
	cputod32((__u32)total, & h->total);
	h->prev_tx = *get_last_committed_tx(super);
	cputod64((__u64)(*next), & h->next_block );
	cputod64((__u64)reiser4_free_committed_blocks(super), & h->free_blocks);
}

static void format_log_record (
	jnode * node,
	int total, 
	int serial,
	const reiser4_block_nr * next_block)
{
	struct super_block * super = reiser4_get_current_sb ();
	struct log_record_header * h;

	assert ("zam-464", node != NULL);

	h = (struct log_record_header*) jdata (node); 

	assert ("zam-465", h != NULL);
	assert ("zam-463", super->s_blocksize > sizeof (struct log_record_header));

	xmemset (jdata(node), 0, (size_t)super->s_blocksize);
	xmemcpy (jdata(node), LOG_RECORD_MAGIC, LOG_RECORD_MAGIC_SIZE);

//	cputod64((__u64)reiser4_trans_id(super), &h->id);
	cputod32((__u32)total,         & h->total     );
	cputod32((__u32)serial,        & h->serial    );
	cputod64((__u64)(*next_block), & h->next_block);
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

/* currently, log records contains contain only wandered map, which depend on
 * Overwrite Set size */
static int get_tx_size (const struct super_block * super, int overwrite_set_size)
{
	int tx_size;

	assert ("zam-440", overwrite_set_size != 0);

	tx_size = (overwrite_set_size - 1) / log_record_capacity (super) + 1;

	tx_size ++;		/* for tx head */

	return tx_size;
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

/* This function is called after Relocate set gets written to disk, Overwrite
 * set is written to wandered locations and all log records are written
 * also. Updated journal header blocks contains a pointer (block number) to
 * first log record of the just written transaction */
static int update_journal_header (capture_list_head * tx_list)
{
	struct super_block * s = reiser4_get_current_sb();
	struct reiser4_super_info_data * private = get_super_private (s);

	jnode * jh = private->journal_header;
	jnode * head = capture_list_back (tx_list);

	d64 block;
	int ret;

	cputod64 (*jnode_get_block(head), &block);

	set_last_committed_tx (s, &block);

	ret = submit_write(jh, 1, jnode_get_block(jh), NULL);

	if (ret) return ret;

	ret = jwait_io (jh, WRITE);

	return ret;
}
	
/* This function is called after write-back is finished. We update journal
 * footer block and free blocks which were occupied by wandered blocks and
 * transaction log records */
static int update_journal_footer (capture_list_head * tx_list)
{
	struct super_block * s = reiser4_get_current_sb();
	reiser4_super_info_data * private = get_super_private(s);

	jnode * jf = private->journal_footer;
	jnode * tx_head = capture_list_front (tx_list);

	int ret;

	assert ("zam-496", !capture_list_end (tx_list, tx_head));
	set_last_flushed_tx (s, (d64*)jnode_get_block(tx_head));

	ret = submit_write (jf, 1, jnode_get_block(jf), NULL);
	if (ret) return ret;

	ret = jwait_io (jf, WRITE);
	if (ret) return ret;

	return 0;
}


/* free block numbers of log records of already written in place transaction */
static void dealloc_tx_list (capture_list_head * tx_list) 
{
	while (!capture_list_empty (tx_list)) {
		jnode * cur = capture_list_pop_front(tx_list);

		reiser4_dealloc_block (jnode_get_block (cur), 0, BLOCK_NOT_COUNTED);

		jfree (cur);
	}
}

static int dealloc_wmap_actor (
	txn_atom               * atom,
	const reiser4_block_nr * a UNUSED_ARG, 
	const reiser4_block_nr * b,
	void                   * data UNUSED_ARG)
{

	assert ("zam-499", b != NULL);
	assert ("zam-500", *b != 0);
	assert ("zam-501", !blocknr_is_fake(b));

	spin_unlock_atom(atom);
	reiser4_dealloc_block (b, 0, BLOCK_NOT_COUNTED);
	spin_lock_atom(atom);

	return 0;
}

/* free wandered block locations of  already written in place transaction */
static void dealloc_wmap (void)
{
	txn_atom * atom = get_current_atom_locked();
	blocknr_set_iterator (atom, &atom->wandered_map, dealloc_wmap_actor, NULL, 1);
	spin_unlock_atom(atom);
}


/* helper function for alloc wandered blocks, which refill set of block
 * numbers needed for wandered blocks  */
static int get_more_wandered_blocks (int count, reiser4_block_nr * start, reiser4_block_nr *len)
{
	reiser4_blocknr_hint hint;
	int ret;

	blocknr_hint_init (&hint);
	hint.block_stage = BLOCK_GRABBED;
	
	*len = count;

	ret = reiser4_alloc_blocks (&hint, start, len);

	blocknr_hint_done (&hint);

	return ret;
}

/* count Overwrite Set size and place Overwrite Set on a separate list  */
static int get_overwrite_set (txn_atom * atom, capture_list_head * overwrite_list)
{
	int set_size = 0;

	capture_list_head * head = &atom->clean_nodes;
	jnode * cur = capture_list_front(head);

	while (!capture_list_end (head, cur)) {
		jnode * next = capture_list_next(cur);

		if (JF_ISSET(cur, ZNODE_WANDER)) { 
			set_size ++;

			capture_list_remove_clean (cur);
			capture_list_push_front (overwrite_list, cur);
		}

		cur = next;
	}
	return set_size;
}

/* overwrite set nodes IO completion handler */
static void wander_end_io (struct bio * bio)
{
	int i;
	struct txn_atom_extra * extra = bio->bi_private;

	for (i = 0; i < bio->bi_vcnt; i += 1) {
		struct page *pg = bio->bi_io_vec[i].bv_page;

		if (! test_bit (BIO_UPTODATE, & bio->bi_flags)) {
			SetPageError (pg);
			if (extra) atomic_inc(&extra->nr_errors); 
		} else {
			SetPageUptodate(pg);
		}

		if (! TestClearPageWriteback (pg)) {
			BUG ();
		}

		ClearPageDirty (pg);

		unlock_page (pg);
		page_cache_release (pg);
	}

	if (extra && !atomic_sub_and_test(bio->bi_vcnt, &extra->nr_submitted)) {
		up (&extra->io_sema);
	}

	bio_put (bio);
}


static void init_extra (struct txn_atom_extra * extra)
{
	sema_init(&extra->io_sema, 0);

	atomic_set(&extra->nr_submitted, 0);
	atomic_set(&extra->nr_errors, 0);
}

static int done_extra (struct txn_atom_extra * extra)
{
	/* sort and pass requests to driver */
	blk_run_queues();

	/* wait all IO to complete */
	down (&extra->io_sema);

	assert ("zam-577", atomic_read(&extra->nr_submitted) == 0);

	if (atomic_read(&extra->nr_errors)) return -EIO;

	return 0;
}


/* create a BIO object for all pages for all j-nodes and submit write
 * request. j-nodes are in a double-linked list (capture_list)*/
/* FIXME: it should be combined with similar code in flush.c */
static int submit_write (jnode * first, int nr, 
			 const reiser4_block_nr  * block,
			 struct txn_atom_extra * extra)
{
	struct super_block * super;
	jnode * cur = first;

	struct bio * bio;
	int i;

	assert ("zam-571", first != NULL);
	assert ("zam-572", block != NULL);
	assert ("zam-570", nr > 0);
	assert ("zam-576", extra != NULL);

	bio = bio_alloc(GFP_NOIO, nr);
	if (!bio) return -ENOMEM;

	assert ("zam-574", jnode_page (first) != NULL);
	super = jnode_page(cur)->mapping->host->i_sb;

	bio->bi_sector = *block * (super->s_blocksize >>9);
	bio->bi_bdev   = super->s_dev;
	bio->bi_vcnt   = nr;
	bio->bi_size   = super->s_blocksize * nr;
	bio->bi_end_io = wander_end_io;

	atomic_add(nr, &extra->nr_submitted);
	bio->bi_private = extra;

	for (i = 0; i < nr; i++) {
		struct page * pg;

		pg = jnode_page(cur);
		assert ("zam-573", pg != NULL);

		page_cache_get (pg);
		lock_page (pg);
		SetPageWriteback (pg);

		ClearPageUptodate(pg);

		bio->bi_io_vec[i].bv_page   = pg;
		bio->bi_io_vec[i].bv_len    = super->s_blocksize;
		bio->bi_io_vec[i].bv_offset = 0;

		cur = capture_list_next (cur);
	}

	submit_bio(WRITE, bio);

	return 0;
}

/* This is a procedure which recovers a contiguous sequences of disk block
 * numbers in the given list of j-nodes and submits write requests on this
 * per-sequence basis */
/* FIXME: it is much simpler now */
static int submit_batched_write (capture_list_head * head, struct txn_atom_extra * extra)
{
	int ret;
	jnode * cur = capture_list_front(head);

	while (!capture_list_end(head, cur)) {
		ret = submit_write(cur, 1, jnode_get_block(cur), extra);

		if (ret) return ret;
	}

	return 0;
}

/* allocate given number of nodes over the journal area and link them into a
 * list, return pinter to the first jnode in the list */
static int alloc_tx (int nr, capture_list_head * tx_list, struct txn_atom_extra * extra)
{
	reiser4_blocknr_hint  hint;

	reiser4_block_nr allocated = 0;
	reiser4_block_nr prev;

	jnode * cur;
	jnode *txhead;

	int serial = nr;

	int ret;

	while (allocated < (unsigned)nr) {
		reiser4_block_nr first, len = (nr - allocated);
		int j;

		blocknr_hint_init (&hint);
		/* FIXME: there should be some block allocation policy for
		 * nodes which contain log records */
		hint.block_stage = BLOCK_GRABBED;

		ret = reiser4_alloc_blocks (&hint, &first, &len);

		blocknr_hint_done (&hint);

		if (ret != 0) goto fail;

		allocated += len;

		/* create jnodes for all log records */
		for (j = 0; (unsigned)j < len; j++) {
			cur = jnew ();

			if (cur == NULL) {
				ret = -ENOMEM;
				goto fail;
			}

			jnode_set_block(cur, &first);

			ret = jload(cur);

			if (ret != 0) {
				jfree (cur);
				goto fail;
			}

			capture_list_push_back (tx_list, cur);

			first ++;
		}
	}

	txhead = capture_list_front(tx_list);
	cur    = capture_list_next(txhead);

	assert ("zam-467", !capture_list_end(tx_list, cur));

	prev = *jnode_get_block(txhead);

	/* iterate over all list members except first one */
	while (cur != txhead) {
		format_log_record (cur, nr, --serial, &prev);
		prev = *jnode_get_block(cur);
		cur = capture_list_next(cur);
	}

	format_tx_head(txhead, nr, &prev);

	{ /* Fill log records with Wandered Set */
		struct store_wmap_params params;
		txn_atom * atom;

		params.cur = capture_list_next (txhead);

		params.idx = 0;
		params.capacity = log_record_capacity (reiser4_get_current_sb());

		atom = get_current_atom_locked ();
		blocknr_set_iterator (atom, &atom->wandered_map, &store_wmap_actor, &params , 0);
		spin_unlock_atom (atom);
	}

	ret = submit_batched_write(tx_list, extra);
	if (ret) goto fail;

	return 0;

 fail:
	while (!capture_list_empty (tx_list)) {
		jnode * node = capture_list_pop_back(tx_list);

		jrelse (node);
		jnode_detach_page (node);
		jfree (node);
	}

	return ret;
}

/* add given wandered mapping to atom's wandered map */
static int add_region_to_wmap (jnode ** cur,
			       const reiser4_block_nr *len_p,
			       const reiser4_block_nr *block_p)
{
	int ret;
	blocknr_set_entry *new_bsep = NULL;
	reiser4_block_nr block;
	reiser4_block_nr len;

	txn_atom * atom;

	assert ("zam-568", block_p != NULL);
	block = *block_p;
	assert ("zam-569", len_p != NULL);
	len = *len_p;

	while ((len --) > 0) {  
		do {
			atom = get_current_atom_locked();
			assert ("zam-536", !blocknr_is_fake (jnode_get_block(*cur)));
			ret = blocknr_set_add_pair (
				atom, &atom->wandered_map, &new_bsep, jnode_get_block(*cur), &block );
		} while (ret == -EAGAIN);

		if (ret) {
			/* deallocate blocks which were not added to wandered
			 * map */
			reiser4_dealloc_blocks(&block, &len, 0, BLOCK_GRABBED); 
			reiser4_release_grabbed_space (len);
			return ret;
		}

		spin_unlock_atom(atom);

		(*cur) = capture_list_next (*cur);
		++ block;
	}

	return 0;
}

/* Allocate wandered blocks for current atom's OVERWRITE SET and immediately
 * submit IO for allocated blocks.  We assume that current atom is in a stage
 * when any atom fusion is impossible and atom is unlocked and it is safe. */
int alloc_wandered_blocks (int set_size, capture_list_head * set, struct txn_atom_extra * extra)
{
	int     rest;

	reiser4_block_nr len = 0;
	reiser4_block_nr block;

	int     ret;

	jnode * cur;

	assert ("zam-534", set_size > 0);
	assert ("zam-578", set != NULL);

	rest = set_size;

	cur = capture_list_front(set);
	while (!capture_list_end(set, cur)) {

		assert ("zam-567", JF_ISSET(cur, ZNODE_WANDER));

		ret = get_more_wandered_blocks (rest, &block, &len); 

		if (ret) goto free_blocks;

		ret = add_region_to_wmap(&cur, &len, &block);

		if (ret) goto free_blocks;

		ret = submit_write (cur, (int)len, &block, extra);

		if (ret) goto free_blocks;
	}

 out:
	return ret;
	
 free_blocks:
	/* free all blocks from wandered map*/
	dealloc_wmap();
	goto out;
	
}

/* submit i/o requests for writing in-place nodes from Overwrite set */
static int force_write_back(struct txn_atom_extra * extra UNUSED_ARG)
{
	return 0;
}

/* We assume that at this moment all captured blocks are marked as RELOC or
 * WANDER (belong to Relocate o Overwrite set), all nodes from Relocate set
 * are submitted to write.*/
int reiser4_write_logs (void)
{
	txn_atom        * atom;

	struct super_block * super = reiser4_get_current_sb();
	reiser4_super_info_data * private = get_super_private(super);

	struct txn_atom_extra extra;

	capture_list_head xw_set, tx_list;
	int xw_set_size, tx_size;

	int ret;

	capture_list_init (&xw_set);
	capture_list_init (&tx_list);

	atom = get_current_atom_locked();
	xw_set_size = get_overwrite_set (atom, &xw_set);
	spin_unlock_atom(atom);

	/* count all records needed for storing of the wandered set */
	tx_size = get_tx_size (super, xw_set_size);

	if ((ret = reiser4_grab_space1((__u64)(xw_set_size + tx_size))))
		return ret;

	init_extra (&extra);

	if ((ret = alloc_wandered_blocks (xw_set_size, &xw_set, &extra)))
		return ret;

	/* isolate critical code path which should be executed by only one
 	 * thread using tmgr semaphore */
	down (&private->tmgr.commit_semaphore);

	pre_commit_hook();

	if ((ret = alloc_tx (tx_size, &tx_list, &extra)))
		goto up_and_ret;

	ret = done_extra(&extra);
	if (ret) goto up_and_ret;

	if ((ret = update_journal_header(&tx_list)))
		goto up_and_ret;

	post_commit_hook();

	init_extra (&extra);

	if ((ret = force_write_back(&extra)))
		goto up_and_ret;

	ret = done_extra(&extra);
	if (ret) goto up_and_ret;

	if ((ret = update_journal_footer(&tx_list)))
		goto up_and_ret;

	post_write_back_hook();

 up_and_ret:
	up(&private->tmgr.commit_semaphore);

	/* free blocks of flushed transaction */

	dealloc_tx_list(&tx_list);
	dealloc_wmap ();

	reiser4_release_all_grabbed_space();

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
