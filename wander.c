/* Copyright 2002 by Hans Reiser */

/* Reiser4 log writer is here */

#include "reiser4.h"

int WRITE_LOG = 1;		/* journal is written by default  */

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

struct io_handle {
	struct semaphore io_sema;
	atomic_t         nr_submitted;
	atomic_t         nr_errors;
};

static int submit_write (jnode*, int, const reiser4_block_nr *, struct io_handle *);


static void format_journal_header (struct super_block *s, capture_list_head * tx)
{
	struct reiser4_super_info_data * private;
	struct journal_header * h;
	jnode * txhead;

	private = get_super_private(s);
	assert ("zam-479", private != NULL);
	assert ("zam-480", private->journal_header != NULL);

	txhead = capture_list_front(tx);

	jload_and_lock (private->journal_header);

	h = (struct journal_header*)jdata(private->journal_header);
	assert ("zam-484", h != NULL);

	cputod64(*jnode_get_block(txhead), &h->last_committed_tx);

	junlock_and_relse (private->journal_header);
}

static void format_journal_footer (struct super_block *s, jnode * txhead)
{
	struct reiser4_super_info_data * private;
	struct journal_footer * h;

	private = get_super_private(s);

	assert ("zam-581", txhead != NULL);
	assert ("zam-493", private != NULL);
	assert ("zam-494", private->journal_header != NULL);

	jload_and_lock (private->journal_footer);

	h = (struct journal_footer*)jdata(private->journal_footer);
	assert ("zam-495", h != NULL);

	cputod64(*jnode_get_block(txhead), &h->last_flushed_tx);
	cputod64(reiser4_free_committed_blocks(s), &h->free_blocks);

	junlock_and_relse (private->journal_footer);
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

	cputod32((__u32)total, & h->total);
	cputod64(get_super_private(super)->last_committed_tx, & h->prev_tx);
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

	format_journal_header (s, tx_list);

	ret = submit_write(jh, 1, jnode_get_block(jh), NULL);

	if (ret) return ret;

	blk_run_queues();

	ret = jwait_io (jh, WRITE);

	return ret;
}
	
/* This function is called after write-back is finished. We update journal
 * footer block and free blocks which were occupied by wandered blocks and
 * transaction log records */
static int update_journal_footer (jnode * txhead)
{
	struct super_block * s = reiser4_get_current_sb();
	reiser4_super_info_data * private = get_super_private(s);

	jnode * jf = private->journal_footer;

	int ret;

	format_journal_footer (s, txhead);

	ret = submit_write (jf, 1, jnode_get_block(jf), NULL);
	if (ret) return ret;

	blk_run_queues();

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

		jdrop (cur);
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
static int get_more_wandered_blocks (int count, reiser4_block_nr * start, int *len)
{
	reiser4_blocknr_hint hint;
	int ret;

	reiser4_block_nr wide_len = count;

	blocknr_hint_init (&hint);
	hint.block_stage = BLOCK_GRABBED;
	
	ret = reiser4_alloc_blocks (&hint, start, &wide_len);

	blocknr_hint_done (&hint);

	*len = (int)wide_len;

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

		if (jnode_is_formatted(cur) && znode_above_root(JZNODE(cur))) {
			trace_on (TRACE_LOG, "fake znode found , WANDER=(%d)\n", JF_ISSET(cur, ZNODE_WANDER));
		}


		if (JF_ISSET(cur, ZNODE_WANDER)) { 
			capture_list_remove_clean (cur);

			if (jnode_is_formatted(cur) && znode_above_root(JZNODE(cur))) {
				/* we replace fake znode by another (real)
				 * znode which is suggested by disk_layout
				 * plugin */

				/* FIXME: it looks like fake znode should be
				 * replaced by jnode supplied by
				 * disk_layout. */

				struct super_block * s = reiser4_get_current_sb();
				reiser4_super_info_data * private = get_current_super_private();

				if (private->lplug->log_super) {
					jnode *sbj = private->lplug->log_super(s);

					assert ("zam-593", sbj != NULL);

					if (IS_ERR(sbj)) return PTR_ERR(sbj);

					spin_lock_jnode (sbj);

					JF_SET(sbj, ZNODE_WANDER);
					capture_list_push_front (overwrite_list, sbj);
					sbj->atom = atom;
					jref(sbj);

					spin_unlock_jnode(sbj);

					set_size ++;
				} else {
					/* the fake znode was removed from
					 * overwrite set and from capture
					 * list, no jnode was added to
					 * transaction -- we decrement atom's
					 * capture count */
					-- atom->capture_count;
				}

				spin_lock_jnode(cur);

				jput(cur);
				cur->atom = NULL;
				JF_CLR(cur, ZNODE_WANDER);

				spin_unlock_jnode(cur);

			} else {
				capture_list_push_front (overwrite_list, cur);
				set_size ++;
			}
		}

		cur = next;
	}
	return set_size;
}

/* overwrite set nodes IO completion handler */
static void wander_end_io (struct bio * bio)
{
	int i;
	struct io_handle * io_hdl = bio->bi_private;

	for (i = 0; i < bio->bi_vcnt; i += 1) {
		struct page *pg = bio->bi_io_vec[i].bv_page;

		if (! test_bit (BIO_UPTODATE, & bio->bi_flags)) {
			SetPageError (pg);
			if (io_hdl) atomic_inc(&io_hdl->nr_errors); 
		} else {
			SetPageUptodate(pg);
		}

		end_page_writeback (pg);

		/*if (! TestClearPageWriteback (pg)) {BUG ();}*/

		/* FIXME: JMACD->ZAM: This isn't right, but I don't know how to fix it
		 * either.  Still working on flush_finish/flush_bio_write */
		/* if (clear_dirty) ClearPageDirty (pg); */

		/*unlock_page (pg);*/
		page_cache_release (pg);
	}

	if (io_hdl && atomic_sub_and_test(bio->bi_vcnt, &io_hdl->nr_submitted)) {
		up (&io_hdl->io_sema);
	}

	bio_put (bio);
}


static void init_io_handle (struct io_handle * io)
{
	sema_init(&io->io_sema, 0);

	/* This setting makes the wakeup code in wander_and_io working only
	 * after io->nr_submitted gets decremented in done_io_handle. */
	atomic_set(&io->nr_submitted, 1);

	atomic_set(&io->nr_errors, 0);
}

static int done_io_handle (struct io_handle * io)
{
	/* this 1 was from init_io_handle() */
	if (! atomic_dec_and_test(&io->nr_submitted)) {
		/* sort and pass requests to driver */
		blk_run_queues();
		/* wait all IO to complete */
		down (&io->io_sema);

		assert ("zam-577", atomic_read(&io->nr_submitted) == 0);

	}

	if (atomic_read(&io->nr_errors)) return -EIO;
	return 0;
}


/* create a BIO object for all pages for all j-nodes and submit write
 * request. j-nodes are in a double-linked list (capture_list)*/
/* FIXME: it should be combined with similar code in flush.c */
static int submit_write (jnode * first, int nr, 
			 const reiser4_block_nr  * block,
			 struct io_handle * io_hdl)
{
	struct super_block * super = reiser4_get_current_sb();
	int max_blocks;
	jnode * cur = first;

	assert ("zam-571", first != NULL);
	assert ("zam-572", block != NULL);
	assert ("zam-570", nr > 0);

#if REISER4_USER_LEVEL_SIMULATION
	max_blocks = nr;
#else
	max_blocks = bdev_get_queue (super->s_bdev)->max_sectors >> (super->s_blocksize_bits - 9);
#endif

	while (nr > 0) {
		struct bio * bio;
		int nr_blocks = min (nr, max_blocks);
		int i;

		bio = bio_alloc(GFP_NOIO, nr_blocks);
		if (!bio) return -ENOMEM;

		assert ("zam-574", jnode_page (first) != NULL);

		bio->bi_sector = *block * (super->s_blocksize >>9);
		bio->bi_bdev   = super->s_bdev;
		bio->bi_vcnt   = nr_blocks;
		bio->bi_size   = super->s_blocksize * nr_blocks;
		bio->bi_end_io = wander_end_io;

		if (io_hdl) atomic_add(nr_blocks, &io_hdl->nr_submitted);

		bio->bi_private = io_hdl;

		for (i = 0; i < nr_blocks; i++) {
			struct page * pg;

			pg = jnode_page(cur);
			assert ("zam-573", pg != NULL);

			page_cache_get (pg);
			lock_page (pg);

			assert ("zam-605", !PageWriteback(pg));

			SetPageWriteback (pg);
			ClearPageUptodate(pg);
			ClearPageDirty   (pg);

			unlock_page (pg);

			bio->bi_io_vec[i].bv_page   = pg;
			bio->bi_io_vec[i].bv_len    = super->s_blocksize;
			bio->bi_io_vec[i].bv_offset = 0;

			cur = capture_list_next (cur);
		}

		submit_bio(WRITE, bio);

		nr -= nr_blocks;
	}

	return 0;
}

/* This is a procedure which recovers a contiguous sequences of disk block
 * numbers in the given list of j-nodes and submits write requests on this
 * per-sequence basis */
static int submit_batched_write (capture_list_head * head, struct io_handle * io)
{
	int ret;
	jnode * beg = capture_list_front(head);

	while (!capture_list_end(head, beg)) {
		int nr = 1;
		jnode * cur = capture_list_next(beg);

		while (!capture_list_end(head, cur)) {
			if (*jnode_get_block(cur) != *jnode_get_block(beg) + nr) break;
			++ nr;
			cur = capture_list_next(cur);
		}

		ret = submit_write(beg, nr, jnode_get_block(beg), io);
		if (ret) return ret;

		beg = cur;
	}

	return 0;
}

/* allocate given number of nodes over the journal area and link them into a
 * list, return pinter to the first jnode in the list */
static int alloc_tx (int nr, capture_list_head * tx_list, struct io_handle * io_hdl)
{
	reiser4_blocknr_hint  hint;

	reiser4_block_nr allocated = 0;

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

	{ /* format a on-disk linked list of log records */
		jnode * next;

		txhead = capture_list_front(tx_list);
		cur    = capture_list_back(tx_list);

		assert ("zam-467", !capture_list_end(tx_list, cur));

		next = txhead;

		/* iterate over all list members except first one */
		while (cur != txhead) {
			format_log_record (cur, nr, --serial, jnode_get_block(next));
			next = cur;
			cur = capture_list_prev(cur);
		}

		format_tx_head(txhead, nr, jnode_get_block(next));
	}

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

	{ /* relse all jnodes from tx_list*/
		jnode * cur = capture_list_front (tx_list);
		while (!capture_list_end (tx_list, cur)) {
			jrelse (cur);
			cur = capture_list_next (cur);
		}


	}

	ret = submit_batched_write(tx_list, io_hdl);
	if (ret) goto fail;

	return 0;

 fail:
	while (!capture_list_empty (tx_list)) {
		jnode * node = capture_list_pop_back(tx_list);

		jrelse (node);
		jdrop (node);
		jfree (node);
	}

	return ret;
}

/* add given wandered mapping to atom's wandered map */
static int add_region_to_wmap (jnode * cur,
			       int len,
			       const reiser4_block_nr *block_p)
{
	int ret;
	blocknr_set_entry *new_bsep = NULL;
	reiser4_block_nr block;

	txn_atom * atom;

	assert ("zam-568", block_p != NULL);
	block = *block_p;
	assert ("zam-569", len > 0);

	while ((len --) > 0) {  
		do {
			atom = get_current_atom_locked();
			assert ("zam-536", !blocknr_is_fake (jnode_get_block(cur)));
			ret = blocknr_set_add_pair (
				atom, &atom->wandered_map, &new_bsep, jnode_get_block(cur), &block );
		} while (ret == -EAGAIN);

		if (ret) {
			/* deallocate blocks which were not added to wandered
			 * map */
			reiser4_block_nr wide_len = len;

			reiser4_dealloc_blocks(&block, &wide_len, 0, BLOCK_NOT_COUNTED); 
			return ret;
		}

		spin_unlock_atom(atom);

		cur = capture_list_next(cur);
		++ block;
	}

	return 0;
}

/* Allocate wandered blocks for current atom's OVERWRITE SET and immediately
 * submit IO for allocated blocks.  We assume that current atom is in a stage
 * when any atom fusion is impossible and atom is unlocked and it is safe. */
int alloc_wandered_blocks (int set_size, capture_list_head * set, struct io_handle * io_hdl)
{
	reiser4_block_nr block;

	int     rest;
	int     len;
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

		rest -= len;

		ret = add_region_to_wmap(cur, len, &block);
		if (ret) goto free_blocks;

		ret = submit_write (cur, len, &block, io_hdl);
		if (ret) goto free_blocks;

		while ((len --) > 0) {
			assert ("zam-604", !capture_list_end(set, cur));
			cur = capture_list_next(cur);
		}
	}

	return 0;
	
 free_blocks:
	/* free all blocks from wandered map*/
	dealloc_wmap();
	return ret;
	
}

/* We assume that at this moment all captured blocks are marked as RELOC or
 * WANDER (belong to Relocate o Overwrite set), all nodes from Relocate set
 * are submitted to write.*/
int reiser4_write_logs (void)
{
	txn_atom        * atom;

	struct super_block * super = reiser4_get_current_sb();
	reiser4_super_info_data * private = get_super_private(super);

	struct io_handle io_hdl;

	capture_list_head overwrite_set, tx_list;
	int overwrite_set_size, tx_size;

	int ret=0;

	capture_list_init (&overwrite_set);
	capture_list_init (&tx_list);

	/* isolate critical code path which should be executed by only one
 	 * thread using tmgr semaphore */
	down (&private->tmgr.commit_semaphore);

	/* block allocator may add j-nodes to the clean_list */
	pre_commit_hook();

	atom = get_current_atom_locked();
	spin_unlock_atom(atom);

	/* count overwrite set and place it in a separate list */
	overwrite_set_size = get_overwrite_set (atom, &overwrite_set);

	if (overwrite_set_size < 0)
		goto up_and_ret;

	/* count all records needed for storing of the wandered set */
	tx_size = get_tx_size (super, overwrite_set_size);

	if ((ret = reiser4_grab_space1((__u64)(overwrite_set_size + tx_size))))
		goto up_and_ret;

	init_io_handle (&io_hdl);

	if ((ret = alloc_wandered_blocks (overwrite_set_size, &overwrite_set, &io_hdl)))
		goto up_and_ret;

	if ((ret = alloc_tx (tx_size, &tx_list, &io_hdl)))
		goto up_and_ret;

	ret = done_io_handle(&io_hdl);
	if (ret) goto up_and_ret;

	if ((ret = update_journal_header(&tx_list)))
		goto up_and_ret;

	post_commit_hook();

	init_io_handle (&io_hdl);

	/* force j-nodes write back */
	if ((ret = submit_batched_write(&overwrite_set, &io_hdl)))
		goto up_and_ret;

	ret = done_io_handle(&io_hdl);
	if (ret) goto up_and_ret;

	if ((ret = update_journal_footer(capture_list_front(&tx_list))))
		goto up_and_ret;

	post_write_back_hook();

 up_and_ret:
	up(&private->tmgr.commit_semaphore);

	/* free blocks of flushed transaction */

	dealloc_tx_list(&tx_list);
	dealloc_wmap ();

	reiser4_release_all_grabbed_space();

	capture_list_splice (&atom->clean_nodes, &overwrite_set);

	return ret;
}

/* consistency checks for journal data/control blocks: header, footer, log
 * records, transactions head blocks. All functions return zero on success. */

static int check_journal_header (const jnode * node UNUSED_ARG)
{
	/* FIXME: journal header has no magic field yet. */
	return 0;
}

static int check_journal_footer (const jnode * node UNUSED_ARG)
{
	/* FIXME: journal footer has no magic field yet. */
	return 0;
}

static int check_tx_head (const jnode * node)
{
	struct tx_header * TH = (struct tx_header*) jdata(node);

	if (memcmp(&TH->magic, TX_HEADER_MAGIC, TX_HEADER_MAGIC_SIZE) != 0) {
		warning ("zam-627", "tx head at block %llX corrupted\n",
			 (unsigned long long int)(*jnode_get_block(node)));
		return -EIO;
	}

	return 0;
}

static int check_log_record (const jnode * node)
{
	struct log_record_header *RH = (struct log_record_header *)jdata(node);

	if (memcmp(&RH->magic, LOG_RECORD_MAGIC, LOG_RECORD_MAGIC_SIZE) != 0) {
		warning ("zam-628", "log record at block %llX corrupted\n",
			 (unsigned long long int)(*jnode_get_block(node)));
		return -EIO;
	}

	return 0;
}

/** replay one transaction: restore and write overwrite set in place */
static int replay_transaction (const struct super_block * s,
			       const reiser4_block_nr * log_rec_block_p, 
			       const reiser4_block_nr * end_block,
			       unsigned int nr_log_records)
{
	capture_list_head overwrite;
	reiser4_block_nr log_rec_block = *log_rec_block_p;
	jnode * log;
	int ret;

	log = jnew();
	if (log == NULL) return -ENOMEM; 

	capture_list_init(&overwrite);

	while (log_rec_block != *end_block) {
		struct log_record_header * LH;
		struct log_entry * LE;

		int i;

		if (nr_log_records == 0) { 
			warning ("zam-631", "number of log records in the linked list"
				 " greater than number stored in tx head.\n");
			ret =  -EIO;
			goto free_ow_set;
		}

		jnode_set_block(log, &log_rec_block);

		ret = jload(log);
		if (ret < 0) { jfree(log); return ret; }

		ret= check_log_record(log);
		if (ret) { jrelse(log); jdrop(log); jfree(log); return ret; }

		LH = (struct log_record_header *)jdata(log);
		log_rec_block = d64tocpu(&LH->next_block);

		LE = (struct log_entry *)(LH + 1);

		/* restore overwrite set from log record content */
		for (i = 0; i < log_record_capacity(s); i++) {
			reiser4_block_nr block;
			jnode * node;

			node = jnew();
			if (node == NULL) { ret = -ENOMEM; goto free_ow_set; }

			block = d64tocpu(&LE->wandered);
			if (block == 0) break;

			jnode_set_block(node, &block);
			ret = jload(node);

			if (ret < 0) { jfree (node); goto free_ow_set; }

			block = d64tocpu(&LE->original);

			assert ("zam-603", block != 0);

			jnode_set_block(node, &block);

			capture_list_push_back (&overwrite, node);

			++ LE;
		}

		jrelse(log);
		jdrop(log);

		-- nr_log_records;
	}

	if (nr_log_records != 0) {
		warning ("zam-632", "number of log records in the linked list"
			 " less than number stored in tx head.\n");
		ret = -EIO;
		goto free_ow_set;
	} 


	{       /* write wandered set in place */
		struct io_handle io;

		init_io_handle(&io);
		submit_batched_write(&overwrite, &io);
		ret = done_io_handle(&io);

		if (ret) goto free_ow_set;
	}

	ret = -EAGAIN;

 free_ow_set:
	while (!capture_list_empty) {
		jnode * cur = capture_list_pop_front(&overwrite);
		jrelse(cur);
		jdrop(cur);
		jfree(cur);
	}

	return ret;
}


/* find oldest not flushed transaction and flush it */
static int replay_oldest_transaction(struct super_block * s)
{
	reiser4_super_info_data * private = get_super_private(s);
	jnode *jf = private->journal_footer;
	unsigned int total;
	struct journal_footer * F;
	struct tx_header * T;

	reiser4_block_nr prev_tx;
	reiser4_block_nr last_flushed_tx;
	reiser4_block_nr log_rec_block = 0;

	jnode * tx_head;

	int ret;

	if ((ret = jload(jf)) < 0) return ret;

	F = (struct journal_footer*)jdata(jf);

	last_flushed_tx = d64tocpu(&F->last_flushed_tx);

	jrelse(jf);

	if(private->last_committed_tx == last_flushed_tx) {
		/* all transactions are replayed */
		return 0;
	}

	/* FIXME: journal replaying is not read yet */
	trace_on(TRACE_REPLAY, "not flushed transactions found.");
	return 0;

	if ((tx_head = jnew()) == NULL) return -ENOMEM;

	prev_tx = private->last_committed_tx;

	/* searching for oldest not flushed transaction */
	while (1) {
		jnode_set_block(tx_head, &prev_tx);

		ret = jload(tx_head);
		if (ret < 0 ) { jfree (tx_head); return ret;}

		ret = check_tx_head(tx_head);
		if (ret) { jrelse(tx_head); jdrop(tx_head); jfree(tx_head); return ret; }

		T = (struct tx_header*)jdata(tx_head);

		prev_tx = d64tocpu(&T->prev_tx);

		if (prev_tx == last_flushed_tx) {
			/* get free block count from committed transaction
			 * head and set per-fs free block counter, it will be
			 * used in journal footer update also */
			reiser4_set_free_blocks(s, d64tocpu(&T->free_blocks));
			break;
		}

		jrelse(tx_head);
		jdrop(tx_head);
	}

	total = d32tocpu(&T->total);
	log_rec_block = d64tocpu(&T->next_block);

	trace_on(TRACE_REPLAY, "not flushed transaction found (head block %llX, %u log records)\n",
		 (unsigned long long)(*jnode_get_block(tx_head)), total);

	jref(tx_head);
	jrelse(tx_head);

	ret = replay_transaction(s, &log_rec_block, jnode_get_block(tx_head), total - 1);
	if (ret) goto out;

	ret = update_journal_footer(tx_head);
 out:
	jput(tx_head);
	jdrop(tx_head);
	jfree(tx_head);

	return ret;
}

/* reiser4 replay journal procedure */
int reiser4_replay_journal (struct super_block * s)
{
	/* FIXME: it is a limited version of journal replaying which just
	 * takes saved free block counter from journal footer, searches for
	 * not flushed transaction and prints a warning, if those transactions
	 * found.*/

	reiser4_super_info_data * private = get_super_private(s);
	jnode *jh, *jf;

	struct journal_header * H;
	struct journal_footer * F;

	int nr_tx_replayed = 0;

	int ret;

	assert ("zam-582", private != NULL);

	jh = private->journal_header;
	jf = private->journal_footer;

	if (!jh || !jf) {
		/* it is possible that disk layout does not support journal
		 * structures, we just warn about this */
		warning ("zam-583", "journal control blocks were not loaded by disk layout plugin.  "
			 "journal replaying is not possible.\n");
		return 0;
	}

	/* Take free block count from journal footer block. The free block
	 * counter value corresponds the last flushed transaction state */
	ret = jload(jf);
	if (ret < 0) return ret;

	ret = check_journal_footer(jf);
	if (ret) { jrelse(jf); return ret; }

	F = (struct journal_footer *)jdata(jf);
	if (d64tocpu(&F->free_blocks)) {
		reiser4_set_free_blocks (s, d64tocpu(&F->free_blocks));
	}

	jrelse(jf);

	/* store last committed transaction info in reiser4 in-memory super
	 * block */
	ret = jload(jh);
	if (ret < 0) return ret;

	ret = check_journal_header(jh);
	if (ret) { jrelse(jh); return ret; }

	H = (struct journal_header *)jdata(jh);
	private->last_committed_tx = d64tocpu(&H->last_committed_tx);

	jrelse(jh);

	/* replay committed transactions */
	while ((ret = replay_oldest_transaction(s)) == -EAGAIN) 
		nr_tx_replayed ++;

	trace_on(TRACE_REPLAY, "%d transactions replayed ret = %d", nr_tx_replayed, ret);

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
