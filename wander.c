/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Reiser4 Wandering Log */

/*
 * Modified by Edward Shishkin to support Heterogeneous Logical Volumes
 */

/* You should read http://www.namesys.com/txn-doc.html

   That describes how filesystem operations are performed as atomic
   transactions, and how we try to arrange it so that we can write most of the
   data only once while performing the operation atomically.

   For the purposes of this code, it is enough for it to understand that it
   has been told a given block should be written either once, or twice (if
   twice then once to the wandered location and once to the real location).

   This code guarantees that those blocks that are defined to be part of an
   atom either all take effect or none of them take effect.

   The "relocate set" of nodes are submitted to write by the jnode_flush()
   routine, and the "overwrite set" is submitted by reiser4_write_log().
   This is because with the overwrite set we seek to optimize writes, and
   with the relocate set we seek to cause disk order to correlate with the
   "parent first order" (preorder).

   reiser4_write_log() allocates and writes wandered blocks and maintains
   additional on-disk structures of the atom as wander records (each wander
   record occupies one block) for storing of the "wandered map" (a table which
   contains a relation between wandered and real block numbers) and other
   information which might be needed at transaction recovery time.

   The wander records are unidirectionally linked into a circle: each wander
   record contains a block number of the next wander record, the last wander
   record points to the first one.

   One wander record (named "tx head" in this file) has a format which is
   different from the other wander records. The "tx head" has a reference to the
   "tx head" block of the previously committed atom.  Also, "tx head" contains
   fs information (the free blocks counter, and the oid allocator state) which
   is logged in a special way .

   There are two journal control blocks, named journal header and journal
   footer which have fixed on-disk locations.  The journal header has a
   reference to the "tx head" block of the last committed atom.  The journal
   footer points to the "tx head" of the last flushed atom.  The atom is
   "played" when all blocks from its overwrite set are written to disk the
   second time (i.e. written to their real locations).

   NOTE: People who know reiserfs internals and its journal structure might be
   confused with these terms journal footer and journal header. There is a table
   with terms of similar semantics in reiserfs (reiser3) and reiser4:

   REISER3 TERM        |  REISER4 TERM         | DESCRIPTION
   --------------------+-----------------------+----------------------------
   commit record       |  journal header       | atomic write of this record
                       |                       | ends transaction commit
   --------------------+-----------------------+----------------------------
   journal header      |  journal footer       | atomic write of this record
                       |                       | ends post-commit writes.
                       |                       | After successful
                       |                       | writing of this journal
                       |                       | blocks (in reiser3) or
                       |                       | wandered blocks/records are
                       |                       | free for re-use.
   --------------------+-----------------------+----------------------------

   The atom commit process is the following:

   1. The overwrite set is taken from atom's clean list, and its size is
      counted.

   2. The number of necessary wander records (including tx head) is calculated,
      and the wander record blocks are allocated.

   3. Allocate wandered blocks and populate wander records by wandered map.

   4. submit write requests for wander records and wandered blocks.

   5. wait until submitted write requests complete.

   6. update journal header: change the pointer to the block number of just
   written tx head, submit an i/o for modified journal header block and wait
   for i/o completion.

   NOTE: The special logging for bitmap blocks and some reiser4 super block
   fields makes processes of atom commit, flush and recovering a bit more
   complex (see comments in the source code for details).

   The atom playing process is the following:

   1. Write atom's overwrite set in-place.

   2. Wait on i/o.

   3. Update journal footer: change the pointer to block number of tx head
   block of the atom we currently flushing, submit an i/o, wait on i/o
   completion.

   4. Free disk space which was used for wandered blocks and wander records.

   After the freeing of wandered blocks and wander records we have that journal
   footer points to the on-disk structure which might be overwritten soon.
   Neither the log writer nor the journal recovery procedure use that pointer
   for accessing the data.  When the journal recovery procedure finds the oldest
   transaction it compares the journal footer pointer value with the "prev_tx"
   pointer value in tx head, if values are equal the oldest not flushed
   transaction is found.

   NOTE on disk space leakage: the information about of what blocks and how many
   blocks are allocated for wandered blocks, wandered records is not written to
   the disk because of special logging for bitmaps and some super blocks
   counters.  After a system crash we the reiser4 does not remember those
   objects allocation, thus we have no such a kind of disk space leakage.
*/

/* Special logging of reiser4 super block fields. */

/* There are some reiser4 super block fields (free block count and OID allocator
   state (number of files and next free OID) which are logged separately from
   super block to avoid unnecessary atom fusion.

   So, the reiser4 super block can be not captured by a transaction with
   allocates/deallocates disk blocks or create/delete file objects.  Moreover,
   the reiser4 on-disk super block is not touched when such a transaction is
   committed and flushed.  Those "counters logged specially" are logged in "tx
   head" blocks and in the journal footer block.

   A step-by-step description of special logging:

   0. The per-atom information about deleted or created files and allocated or
   freed blocks is collected during the transaction.  The atom's
   ->nr_objects_created and ->nr_objects_deleted are for object
   deletion/creation tracking, the numbers of allocated and freed blocks are
   calculated using atom's delete set and atom's capture list -- all new and
   relocated nodes should be on atom's clean list and should have JNODE_RELOC
   bit set.

   1. The "logged specially" reiser4 super block fields have their "committed"
   versions in the reiser4 in-memory super block.  They get modified only at
   atom commit time.  The atom's commit thread has an exclusive access to those
   "committed" fields because the log writer implementation supports only one
   atom commit a time (there is a per-fs "commit" mutex).  At
   that time "committed" counters are modified using per-atom information
   collected during the transaction. These counters are stored on disk as a
   part of tx head block when atom is committed.

   2. When the atom is flushed the value of the free block counter and the OID
   allocator state get written to the journal footer block.  A special journal
   procedure (journal_recover_sb_data()) takes those values from the journal
   footer and updates the reiser4 in-memory super block.

   NOTE: That means free block count and OID allocator state are logged
   separately from the reiser4 super block regardless of the fact that the
   reiser4 super block has fields to store both the free block counter and the
   OID allocator.

   Writing the whole super block at commit time requires knowing true values of
   all its fields without changes made by not yet committed transactions. It is
   possible by having their "committed" version of the super block like the
   reiser4 bitmap blocks have "committed" and "working" versions.  However,
   another scheme was implemented which stores special logged values in the
   unused free space inside transaction head block.  In my opinion it has an
   advantage of not writing whole super block when only part of it was
   modified. */

#include "debug.h"
#include "dformat.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "page_cache.h"
#include "wander.h"
#include "reiser4.h"
#include "super.h"
#include "vfs_ops.h"
#include "writeout.h"
#include "inode.h"
#include "entd.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/mm.h>		/* for struct page */
#include <linux/pagemap.h>
#include <linux/bio.h>		/* for struct bio */
#include <linux/blkdev.h>

static int write_jnodes_contig(jnode *, int, const reiser4_block_nr *,
			       flush_queue_t *, int, reiser4_subvol *);
/*
 * Per-logical-volume commit_handle.
 * This contains infrastructure needed at atom commit time.
 * See also definition of per-suvbolume commit handle (commit_handle_subvol)
 */
struct commit_handle {
	__u64 nr_files;
	__u64 next_oid;
	__u32 total_tx_size; /* total number of wander records */
	__u32 total_overwrite_set_size;
	reiser4_block_nr total_nr_bitmap;
	txn_atom *atom; /* the atom which is being committed */
	struct super_block *super; /* current super block */
};

static void init_ch_sub(reiser4_subvol *subv)
{
	struct commit_handle_subvol *ch_sub = &subv->ch;

	assert("edward-xxx", list_empty(&ch_sub->overwrite_set));
	assert("edward-xxx", list_empty(&ch_sub->tx_list));
	assert("edward-xxx", list_empty(&ch_sub->wander_map));

	__init_ch_sub(ch_sub);
	ch_sub->free_blocks = subv->blocks_free_committed;
}

static void init_commit_handle(struct commit_handle *ch, txn_atom *atom,
			       reiser4_subvol *subv)
{
	memset(ch, 0, sizeof(struct commit_handle));
	ch->atom = atom;
	ch->super = reiser4_get_current_sb();
	ch->nr_files = get_current_super_private()->nr_files_committed;
	ch->next_oid = oid_next(ch->super);
	if (subv)
		/*
		 * init ch of specified subvolume
		 */
		init_ch_sub(subv);
	else {
		/*
		 * init ch of all subvolumes
		 */
		u32 orig_id;
		for_each_origin(orig_id)
			init_ch_sub(super_origin(ch->super, orig_id));
	}
}

#if REISER4_DEBUG
static void done_ch_sub(reiser4_subvol *subv)
{
	struct commit_handle_subvol *ch_sub = &subv->ch;

	assert("edward-xxx", list_empty(&ch_sub->overwrite_set));
	assert("edward-xxx", list_empty(&ch_sub->tx_list));
	assert("edward-xxx", list_empty(&ch_sub->wander_map));
}
#endif

static void done_commit_handle(struct commit_handle *ch, reiser4_subvol *subv)
{
#if REISER4_DEBUG
	if (subv)
		done_ch_sub(subv);
	else {
		u32 subv_id;
		for_each_origin(subv_id)
			done_ch_sub(super_origin(ch->super, subv_id));
	}
#endif
}

/* fill journal header block data  */
static void format_journal_header(struct commit_handle *ch,
				  unsigned subv_id)
{
	reiser4_subvol *subv;
	struct journal_header *header;
	jnode *txhead;

	subv = super_origin(ch->super, subv_id);
	assert("zam-480", subv->journal_header != NULL);

	txhead = list_entry(subv->ch.tx_list.next, jnode, capture_link);

	jload(subv->journal_header);

	header = (struct journal_header *)jdata(subv->journal_header);
	assert("zam-484", header != NULL);

	put_unaligned(cpu_to_le64(*jnode_get_block(txhead)),
		      &header->last_committed_tx);

	jrelse(subv->journal_header);
}

/* fill journal footer block data */
static void format_journal_footer(struct commit_handle *ch, unsigned subv_id)
{
	reiser4_subvol *subv;
	struct journal_footer *footer;
	jnode *tx_head;
	struct commit_handle_subvol *ch_sub;

	subv = super_origin(ch->super, subv_id);
	ch_sub = &subv->ch;

	tx_head = list_entry(ch_sub->tx_list.next, jnode, capture_link);

	assert("zam-494", subv->journal_header != NULL);

	check_me("zam-691", jload(subv->journal_footer) == 0);

	footer = (struct journal_footer *)jdata(subv->journal_footer);
	assert("zam-495", footer != NULL);

	put_unaligned(cpu_to_le64(*jnode_get_block(tx_head)),
		      &footer->last_flushed_tx);
	put_unaligned(cpu_to_le64(ch_sub->free_blocks), &footer->free_blocks);

	put_unaligned(cpu_to_le64(ch->nr_files), &footer->nr_files);
	put_unaligned(cpu_to_le64(ch->next_oid), &footer->next_oid);

	jrelse(subv->journal_footer);
}

/* wander record capacity depends on current block size */
static int wander_record_capacity(const struct super_block *super)
{
	return (super->s_blocksize -
		sizeof(struct wander_record_header)) /
	    sizeof(struct wander_entry);
}

/*
 * Fill first wander record (tx head) in accordance with supplied given data
 */
static void format_tx_head(struct commit_handle *ch, unsigned subv_id)
{
	jnode *tx_head;
	jnode *next;
	struct tx_header *header;
	struct commit_handle_subvol *ch_sub;
	reiser4_subvol *subv;

	subv = super_origin(ch->super, subv_id);
	ch_sub = &subv->ch;

	tx_head = list_entry(ch_sub->tx_list.next, jnode, capture_link);
	assert("zam-692", &ch_sub->tx_list != &tx_head->capture_link);

	next = list_entry(tx_head->capture_link.next, jnode, capture_link);
	if (&ch_sub->tx_list == &next->capture_link)
		next = tx_head;

	header = (struct tx_header *)jdata(tx_head);

	assert("zam-460", header != NULL);
	assert("zam-462", ch->super->s_blocksize >= sizeof(struct tx_header));

	memset(jdata(tx_head), 0, (size_t) ch->super->s_blocksize);
	memcpy(jdata(tx_head), TX_HEADER_MAGIC, TX_HEADER_MAGIC_SIZE);

	put_unaligned(cpu_to_le32(ch_sub->tx_size), &header->total);
	put_unaligned(cpu_to_le64(subv->last_committed_tx), &header->prev_tx);
	put_unaligned(cpu_to_le64(*jnode_get_block(next)), &header->next_block);
	put_unaligned(cpu_to_le64(ch_sub->free_blocks), &header->free_blocks);
	put_unaligned(cpu_to_le64(ch->nr_files), &header->nr_files);
	put_unaligned(cpu_to_le64(ch->next_oid), &header->next_oid);
}

/*
 * prepare ordinary wander record block (fill all service fields)
 */
static void format_wander_record(struct commit_handle *ch, unsigned subv_id,
				 jnode *node, __u32 serial)
{
	jnode *next;
	struct wander_record_header *LRH;
	struct commit_handle_subvol *ch_sub;

	assert("zam-464", node != NULL);

	ch_sub = &super_origin(ch->super, subv_id)->ch;

	LRH = (struct wander_record_header *)jdata(node);
	next = list_entry(node->capture_link.next, jnode, capture_link);

	if (&ch_sub->tx_list == &next->capture_link)
		next = list_entry(ch_sub->tx_list.next, jnode, capture_link);

	assert("zam-465", LRH != NULL);
	assert("zam-463",
	       ch->super->s_blocksize > sizeof(struct wander_record_header));

	memset(jdata(node), 0, (size_t) ch->super->s_blocksize);
	memcpy(jdata(node), WANDER_RECORD_MAGIC, WANDER_RECORD_MAGIC_SIZE);

	put_unaligned(cpu_to_le32(ch_sub->tx_size), &LRH->total);
	put_unaligned(cpu_to_le32(serial), &LRH->serial);
	put_unaligned(cpu_to_le64(*jnode_get_block(next)), &LRH->next_block);
}

/**
 * add one wandered map entry to formatted wander record
 */
static void store_entry(jnode *node, int index,
			const reiser4_block_nr *a, const reiser4_block_nr *b)
{
	char *data;
	struct wander_entry *pairs;

	data = jdata(node);
	assert("zam-451", data != NULL);

	pairs =
	    (struct wander_entry *)(data + sizeof(struct wander_record_header));

	put_unaligned(cpu_to_le64(*a), &pairs[index].original);
	put_unaligned(cpu_to_le64(*b), &pairs[index].wandered);
}

/*
 * currently, wander record contains only wandered map,
 * which depends on overwrite set size
 */
static void get_tx_size(struct commit_handle *ch)
{
	u32 subv_id;

	for_each_origin(subv_id) {

		struct commit_handle_subvol *ch_sub;

		ch_sub = &super_origin(ch->super, subv_id)->ch;

		assert("zam-440", ch_sub->overwrite_set_size != 0);
		assert("zam-695", ch_sub->tx_size == 0);
		/*
		 * count all ordinary wander records
		 * (<overwrite_set_size> - 1) / <wander_record_capacity> + 1
		 * and add one for tx head block
		 */
		ch_sub->tx_size =
			(ch_sub->overwrite_set_size - 1)/
			wander_record_capacity(ch->super) + 2;
		ch->total_tx_size += ch_sub->tx_size;
	}
}

/*
 * A special structure for using in store_wmap_actor()
 * for saving its state between calls
 */
struct store_wmap_params {
	jnode *cur;   /* jnode of current wander record to fill */
	int idx;      /* free element index in wander record  */
	int capacity; /* capacity  */
#if REISER4_DEBUG
	struct list_head *tx_list;
#endif
};

/*
 * an actor for use in blocknr_set_iterator routine
 * which populates the list of pre-formatted wander
 * records by wandered map info
 */
static int store_wmap_actor(txn_atom *atom UNUSED_ARG,
			    const reiser4_block_nr *a,
			    const reiser4_block_nr *b,
			    __u32 subv_id, void *data)
{
	struct store_wmap_params *params = data;

	if (params->idx >= params->capacity) {
		/*
		 * a new wander record should be taken from the tx_list
		 */
		params->cur = list_entry(params->cur->capture_link.next,
					 jnode, capture_link);
		assert("zam-454",
		       params->tx_list != &params->cur->capture_link);

		params->idx = 0;
	}
	store_entry(params->cur, params->idx, a, b);
	params->idx++;

	return 0;
}

/**
 * This function is called after Relocate set gets written to disk, Overwrite
 * set is written to wandered locations and all wander records are written
 * also. Updated journal header blocks contains a pointer (block number) to
 * first wander record of the just written transaction
 */
static int update_journal_header(struct commit_handle *ch, u32 subv_id)
{
	int ret;
	reiser4_subvol *subv = super_origin(ch->super, subv_id);
	jnode *jh = subv->journal_header;
	jnode *head = list_entry(subv->ch.tx_list.next, jnode, capture_link);

	format_journal_header(ch, subv_id);

	ret = write_jnodes_contig(jh, 1, jnode_get_block(jh), NULL,
				  WRITEOUT_FLUSH_FUA, subv);
	if (ret)
		return ret;

	ret = jwait_io(jh, WRITE);
	if (ret)
		return ret;

	subv->last_committed_tx = *jnode_get_block(head);
	return 0;
}

/**
 * This function is called after write-back is finished. We update journal
 * footer block and free blocks which were occupied by wandered blocks and
 * transaction wander records
 */
static int update_journal_footer(struct commit_handle *ch, u32 subv_id)
{
	int ret;
	reiser4_subvol *subv = super_origin(ch->super, subv_id);
	jnode *jf = subv->journal_footer;

	format_journal_footer(ch, subv_id);

	ret = write_jnodes_contig(jf, 1, jnode_get_block(jf), NULL,
				  WRITEOUT_FLUSH_FUA, subv);
	if (ret)
		return ret;

	ret = jwait_io(jf, WRITE);
	if (ret)
		return ret;
	return 0;
}

/*
 * free block numbers of wander records of already written in place transaction
 */
static void dealloc_tx_list(struct commit_handle *ch)
{
	u32 subv_id;

	for_each_origin(subv_id) {
		reiser4_subvol *subv = current_origin(subv_id);
		struct commit_handle_subvol *ch_sub = &subv->ch;

		while (!list_empty(&ch_sub->tx_list)) {
			jnode *cur = list_entry(ch_sub->tx_list.next,
						jnode,
						capture_link);

			list_del(&cur->capture_link);
			ON_DEBUG(INIT_LIST_HEAD(&cur->capture_link));
			reiser4_dealloc_block(jnode_get_block(cur), 0,
					      BA_DEFER | BA_FORMATTED, subv);
			unpin_jnode_data(cur);
			reiser4_drop_io_head(cur);
		}
	}
}

/*
 * An actor for use in block_nr_iterator() routine which frees wandered blocks
 * from atom's overwrite set
 */
static int dealloc_wmap_actor(txn_atom *atom UNUSED_ARG,
			      const reiser4_block_nr *a UNUSED_ARG,
			      const reiser4_block_nr *b, __u32 subv_id,
			      void *data UNUSED_ARG)
{
	assert("zam-499", b != NULL);
	assert("zam-500", *b != 0);
	assert("zam-501", !reiser4_blocknr_is_fake(b));

	reiser4_dealloc_block(b, 0, BA_DEFER | BA_FORMATTED,
			      current_origin(subv_id));
	return 0;
}

/**
 * Free wandered block locations.
 * Pre-condition: Transaction has been played (that is, all blocks
 * from the OVERWRITE set were overwritten successfully.
 */
static void dealloc_wmap(struct commit_handle *ch)
{
	u32 subv_id;

	assert("zam-696", ch->atom != NULL);

	for_each_origin(subv_id) {

		struct commit_handle_subvol *ch_sub;

		ch_sub = &super_origin(ch->super, subv_id)->ch;

		blocknr_set_iterator(ch->atom,
				     &ch_sub->wander_map,
				     dealloc_wmap_actor, NULL, 1,
				     subv_id);
	}
}

static int alloc_wander_blocks(int count, reiser4_block_nr *start, int *len,
			       reiser4_subvol *subv)
{
	int ret;
	reiser4_blocknr_hint hint;
	reiser4_block_nr wide_len = count;

	/* FIXME-ZAM: A special policy needed for allocation of wandered blocks
	   ZAM-FIXME-HANS: yes, what happened to our discussion of using a fixed
	   reserved allocation area so as to get the best qualities of fixed
	   journals? */
	reiser4_blocknr_hint_init(&hint);
	hint.block_stage = BLOCK_GRABBED;

	ret = reiser4_alloc_blocks(&hint, start, &wide_len,
				   BA_FORMATTED | BA_USE_DEFAULT_SEARCH_START,
				   subv);
	*len = (int)wide_len;
	return ret;
}

/*
 * roll back changes made before issuing BIO in the case of IO error.
 */
static void undo_bio(struct bio *bio)
{
	int i;

	for (i = 0; i < bio->bi_vcnt; ++i) {
		struct page *pg;
		jnode *node;

		pg = bio->bi_io_vec[i].bv_page;
		end_page_writeback(pg);
		node = jprivate(pg);
		spin_lock_jnode(node);
		JF_CLR(node, JNODE_WRITEBACK);
		JF_SET(node, JNODE_DIRTY);
		spin_unlock_jnode(node);
	}
	bio_put(bio);
}

/**
 * release resources aquired in get_overwrite_set()
 */
static void put_overwrite_set(struct commit_handle *ch)
{
	jnode *cur;
	u32 subv_id;

	for_each_origin(subv_id) {
		struct commit_handle_subvol *ch_sub;

		ch_sub = &super_origin(ch->super, subv_id)->ch;

		list_for_each_entry(cur, &ch_sub->overwrite_set, capture_link)
			jrelse_tail(cur);
		reiser4_invalidate_list(&ch_sub->overwrite_set);
	}
}

void check_overwrite_set_subv(reiser4_subvol *subv)
{
	jnode *cur;

	list_for_each_entry(cur, &subv->ch.overwrite_set, capture_link)
		assert("edward-xxx", cur->subvol == subv);
}

void check_overwrite_set(void)
{
	u32 subv_id;

	for_each_origin(subv_id)
		check_overwrite_set_subv(current_origin(subv_id));
}

/*
 * Scan atom't overwrite set and do the following:
 * . move every jnode to overwrite set of respective subvolume;
 * . count total number of nodes in all overwrite sets;
 * . grab disk space for wandered blocks allocation;
 * . count bitmap and other not leaf nodes which wandered blocks
 *   allocation we have to grab space for.
 */
int get_overwrite_set(struct commit_handle *ch)
{
	int ret;
	jnode *cur;
	u32 subv_id;
	struct list_head *overw_set;
#if REISER4_DEBUG
	__u64 flush_reserved = 0;
	__u64 nr_formatted_leaves = 0;
	__u64 nr_unformatted_leaves = 0;
#endif
	overw_set = ATOM_OVRWR_LIST(ch->atom);
	cur = list_entry(overw_set->next, jnode, capture_link);

	while (!list_empty(overw_set)) {
		jnode *next;
		struct reiser4_subvol *subv;
		struct commit_handle_subvol *ch_sub;
		struct list_head *subv_overw_set;

		next = list_entry(cur->capture_link.next, jnode, capture_link);
		subv = cur->subvol;
		ch_sub = &subv->ch;
		subv_overw_set = &ch_sub->overwrite_set;
		/*
		 * Count bitmap blocks for getting correct statistics what
		 * number of blocks were cleared by the transaction commit
		 */
		if (jnode_get_type(cur) == JNODE_BITMAP) {
			ch_sub->nr_bitmap++;
			ch->total_nr_bitmap++;
		}
		assert("zam-939", JF_ISSET(cur, JNODE_OVRWR) ||
		       jnode_get_type(cur) == JNODE_BITMAP);

		if (jnode_is_znode(cur) && znode_above_root(JZNODE(cur))) {
			/*
			 * This is a super-block captured in rare events (like
			 * the final commit at the end of mount session (see
			 * release_format40()->capture_super_block(), also
			 * see comments at reiser4_journal_recover_sb_data()).
			 *
			 * We replace fake znode by another (real) znode which
			 * is suggested by disk_layout plugin
			 */
			struct super_block *s = reiser4_get_current_sb();

			if (subv->df_plug->log_super) {
				jnode *sj;

				sj = subv->df_plug->log_super(s, subv);
				assert("zam-593", sj != NULL);

				if (IS_ERR(sj))
					return PTR_ERR(sj);

				spin_lock_jnode(sj);
				JF_SET(sj, JNODE_OVRWR);
				/*
				 * put the new jnode right to overwrite
				 * set of respective subvolume
				 */
				insert_into_subv_ovrwr_list(subv, sj, ch->atom);
				spin_unlock_jnode(sj);
				jload_gfp(sj, reiser4_ctx_gfp_mask_get(), 0);

				ch_sub->overwrite_set_size++;
				ch->total_overwrite_set_size++;
			}
			spin_lock_jnode(cur);
			reiser4_uncapture_block(cur);
			jput(cur);

		} else {
			int ret;
			ch_sub->overwrite_set_size++;
			ch->total_overwrite_set_size++;
			/*
			 * move jnode to the overwrite list of
			 * respective subvolume
			 */
			list_move(&cur->capture_link, subv_overw_set);
			ret = jload_gfp(cur, reiser4_ctx_gfp_mask_get(), 0);
			if (ret)
				reiser4_panic("zam-783",
					      "cannot load jnode (ret = %d)\n",
					      ret);
		}
		/*
		 * Count not leaves here because we have to grab disk space
		 * for wandered blocks. They were not counted as "flush
		 * reserved". Counting should be done _after_ nodes are pinned
		 * into memory by jload().
		 */
		if (!jnode_is_leaf(cur)) {
			/*
			 * Grab space for writing (wandered blocks)
			 * of not leaves found in overwrite set
			 */
			ret = reiser4_grab_space_force(1, BA_RESERVED,
						       jnode_get_subvol(cur));
			if (ret)
				return ret;
		}
		else {
#if REISER4_DEBUG
			if (jnode_is_znode(cur))
				nr_formatted_leaves++;
			else
				nr_unformatted_leaves++;
#endif
			JF_CLR(cur, JNODE_FLUSH_RESERVED);
		}
		cur = next;
	}
	/*
	 * Disk space for allocation of wandered blocks of leaf nodes already
	 * reserved as "flush reserved", move it to grabbed space counter
	 */
	spin_lock_atom(ch->atom);
	for_each_origin(subv_id) {
#if REISER4_DEBUG
		flush_reserved += ch->atom->flush_reserved[subv_id];
#endif
		flush_reserved2grabbed(ch->atom,
				       ch->atom->flush_reserved[subv_id],
				       current_origin(subv_id));
	}
	assert("zam-940",
	       nr_formatted_leaves + nr_unformatted_leaves <= flush_reserved);
	spin_unlock_atom(ch->atom);

	check_overwrite_set();
	return ch->total_overwrite_set_size;
}

/**
 * write_jnodes_contig - submit write request.
 * @head:
 * @first: first jnode of the list
 * @nr: number of jnodes on the list
 * @block_p:
 * @fq:
 * @flags: used to decide whether page is to get PG_reclaim flag
 *
 * Submits a write request for @nr jnodes beginning from the @first, other
 * jnodes are after the @first on the double-linked "capture" list.  All jnodes
 * will be written to the disk region of @nr blocks starting with @block_p block
 * number.  If @fq is not NULL it means that waiting for i/o completion will be
 * done more efficiently by using flush_queue_t objects.
 * This function is the one which writes list of jnodes in batch mode. It does
 * all low-level things as bio construction and page states manipulation.
 *
 * ZAM-FIXME-HANS: brief me on why this function exists, and why bios are
 * aggregated in this function instead of being left to the layers below
 *
 * FIXME: ZAM->HANS: What layer are you talking about? Can you point me to that?
 * Why that layer needed? Why BIOs cannot be constructed here?
 */
static int write_jnodes_contig(jnode *first, int nr,
			       const reiser4_block_nr *block_p,
			       flush_queue_t *fq, int flags,
			       reiser4_subvol *subv)
{
	struct super_block *super = reiser4_get_current_sb();
	int write_op = (flags & WRITEOUT_FLUSH_FUA) ? WRITE_FLUSH_FUA : WRITE;
	jnode *cur = first;
	reiser4_block_nr block;

	assert("zam-571", first != NULL);
	assert("zam-572", block_p != NULL);
	assert("zam-570", nr > 0);

	if (subv == NULL)
		subv = first->subvol;
	block = *block_p;

	while (nr > 0) {
		struct bio *bio;
		int nr_blocks = min(nr, BIO_MAX_PAGES);
		int i;
		int nr_used;

		bio = bio_alloc(GFP_NOIO, nr_blocks);
		if (!bio)
			return RETERR(-ENOMEM);

		bio->bi_bdev = subv->bdev;
		bio->bi_iter.bi_sector = block * (super->s_blocksize >> 9);
		for (nr_used = 0, i = 0; i < nr_blocks; i++) {
			struct page *pg;

			pg = jnode_page(cur);
			assert("zam-573", pg != NULL);

			get_page(pg);

			lock_and_wait_page_writeback(pg);

			if (!bio_add_page(bio, pg, super->s_blocksize, 0)) {
				/*
				 * underlying device is satiated. Stop adding
				 * pages to the bio.
				 */
				unlock_page(pg);
				put_page(pg);
				break;
			}

			spin_lock_jnode(cur);
			assert("nikita-3166",
			       pg->mapping == jnode_get_mapping(cur));
			assert("zam-912", !JF_ISSET(cur, JNODE_WRITEBACK));
#if REISER4_DEBUG
			spin_lock(&cur->load);
			assert("nikita-3165",
			       ergo(is_origin(subv), !jnode_is_releasable(cur)));
			spin_unlock(&cur->load);
#endif
			JF_SET(cur, JNODE_WRITEBACK);
			JF_CLR(cur, JNODE_DIRTY);
			ON_DEBUG(cur->written++);

			assert("edward-1647",
			       ergo(jnode_is_znode(cur), JF_ISSET(cur, JNODE_PARSED)));
			spin_unlock_jnode(cur);
			/*
			 * update checksum
			 */
			if (jnode_is_znode(cur) && is_origin(subv)) {
				zload(JZNODE(cur));
				if (node_plugin_by_node(JZNODE(cur))->csum)
					node_plugin_by_node(JZNODE(cur))->csum(JZNODE(cur), 0);
				zrelse(JZNODE(cur));
			}
			ClearPageError(pg);
			set_page_writeback(pg);

			if (get_current_context()->entd) {
				/* this is ent thread */
				entd_context *ent = get_entd_context(super);
				struct wbq *rq, *next;

				spin_lock(&ent->guard);

				if (pg == ent->cur_request->page) {
					/*
					 * entd is called for this page. This
					 * request is not in th etodo list
					 */
					ent->cur_request->written = 1;
				} else {
					/*
					 * if we have written a page for which writepage
					 * is called for - move request to another list.
					 */
					list_for_each_entry_safe(rq, next, &ent->todo_list, link) {
						assert("", rq->magic == WBQ_MAGIC);
						if (pg == rq->page) {
							/*
							 * remove request from
							 * entd's queue, but do
							 * not wake up a thread
							 * which put this
							 * request
							 */
							list_del_init(&rq->link);
							ent->nr_todo_reqs --;
							list_add_tail(&rq->link, &ent->done_list);
							ent->nr_done_reqs ++;
							rq->written = 1;
							break;
						}
					}
				}
				spin_unlock(&ent->guard);
			}

			clear_page_dirty_for_io(pg);

			unlock_page(pg);

			cur = list_entry(cur->capture_link.next, jnode, capture_link);
			nr_used++;
		}
		if (nr_used > 0) {
			assert("nikita-3453",
			       bio->bi_iter.bi_size == super->s_blocksize * nr_used);
			assert("nikita-3454", bio->bi_vcnt == nr_used);

			/* Check if we are allowed to write at all */
			if (super->s_flags & MS_RDONLY)
				undo_bio(bio);
			else {
				add_fq_to_bio(fq, bio);
				bio_get(bio);
				reiser4_submit_bio(write_op, bio);
				bio_put(bio);
			}

			block += nr_used - 1;
			if (is_origin(subv))
				update_blocknr_hint_default(super, subv, &block);
			block += 1;
		} else {
			bio_put(bio);
		}
		nr -= nr_used;
	}

	return 0;
}

/**
 * This procedure recovers extents (contiguous sequences of disk block
 * numbers) in a given list of jnodes and submits write requests on this
 * per-extent basis.
 *
 * @head: the list of jnodes to write
 */
int write_jnode_list(struct list_head *head, flush_queue_t *fq,
		     long *nr_submitted, int flags, reiser4_subvol *subv)
{
	int ret;
	struct list_head *beg = head->next;

	while (head != beg) {
		int nr = 1;
		struct list_head *cur = beg->next;

		while (head != cur) {
			assert("edward-xxx",
			       jnode_by_link(beg)->subvol ==
			       jnode_by_link(cur)->subvol);

			if (*jnode_get_block(jnode_by_link(cur)) !=
			    *jnode_get_block(jnode_by_link(beg)) + nr)
				break;
			++nr;
			cur = cur->next;
		}
		ret = write_jnodes_contig(jnode_by_link(beg), nr,
					  jnode_get_block(jnode_by_link(beg)),
					  fq, flags, subv);
		if (ret)
			return ret;
		if (nr_submitted)
			*nr_submitted += nr;
		beg = cur;
	}
	return 0;
}

/*
 * add given wandered mapping to atom's wandered map
 */
static int add_region_to_wmap(jnode *cur, int len,
			      const reiser4_block_nr *block_p,
			      reiser4_subvol *subv)
{
	int ret;
	blocknr_set_entry *new_bsep = NULL;
	reiser4_block_nr block;

	txn_atom *atom;

	assert("zam-568", block_p != NULL);
	block = *block_p;
	assert("zam-569", len > 0);

	while ((len--) > 0) {
		do {
			atom = get_current_atom_locked();
			assert("zam-536",
			       !reiser4_blocknr_is_fake(jnode_get_block(cur)));

			ret = blocknr_set_add_pair(atom,
						   &subv->ch.wander_map,
						   &new_bsep,
						   jnode_get_block(cur),
						   &block, subv->id);
		} while (ret == -E_REPEAT);

		if (ret) {
			/*
			 * deallocate blocks which were not added
			 * to wandered map
			 */
			reiser4_block_nr wide_len = len;

			reiser4_dealloc_blocks(&block, &wide_len,
					       BLOCK_NOT_COUNTED,
					       BA_FORMATTED, /* formatted,
								without defer */
					       subv);
			return ret;
		}
		spin_unlock_atom(atom);

		cur = list_entry(cur->capture_link.next, jnode, capture_link);
		++block;
	}
	return 0;
}

/**
 * Allocate temporal ("wandering") disk addresses for specified OVERWRITE set
 * and immediately submit IOs for them.
 * We assume that current atom is in a stage when any atom fusion is impossible
 * and atom is unlocked and it is safe.
 */
static int alloc_submit_wander_blocks(struct commit_handle *ch,
				      unsigned subv_id, flush_queue_t *fq)
{
	reiser4_block_nr block;
	int rest;
	int len;
	int ret;
	jnode *cur;
	reiser4_subvol *subv = super_origin(ch->super, subv_id);
	struct list_head *overw_set = &subv->ch.overwrite_set;

	rest = subv->ch.overwrite_set_size;

	assert("zam-534", rest > 0);

	cur = list_entry(overw_set->next, jnode, capture_link);

	while (overw_set != &cur->capture_link) {
		assert("zam-567", JF_ISSET(cur, JNODE_OVRWR));

		ret = alloc_wander_blocks(rest, &block, &len, subv);
		if (ret)
			return ret;

		rest -= len;

		ret = add_region_to_wmap(cur, len, &block, subv);
		if (ret)
			return ret;

		ret = write_jnodes_contig(cur, len, &block, fq, 0, subv);
		if (ret)
			return ret;

		while ((len--) > 0) {
			assert("zam-604", overw_set != &cur->capture_link);
			cur = list_entry(cur->capture_link.next,
					 jnode, capture_link);
		}
	}
	return 0;
}

/*
 * Allocate given number of nodes over the journal area and link
 * them into a list; return pointer to the first jnode in the list
 */
static int alloc_submit_wander_records(struct commit_handle *ch,
				       unsigned subv_id, flush_queue_t *fq)
{
	reiser4_blocknr_hint hint;
	reiser4_block_nr allocated = 0;
	reiser4_block_nr first, len;
	jnode *cur;
	jnode *txhead;
	int ret;
	reiser4_context *ctx;
	reiser4_subvol *subv = super_origin(ch->super, subv_id);
	struct commit_handle_subvol *ch_sub = &subv->ch;
	struct list_head *tx_list = &ch_sub->tx_list;
	int tx_size = ch_sub->tx_size;

	assert("zam-698", tx_size > 0);
	assert("zam-699", list_empty_careful(tx_list));

	ctx = get_current_context();

	while (allocated < (unsigned)tx_size) {
		len = tx_size - allocated;

		reiser4_blocknr_hint_init(&hint);

		hint.block_stage = BLOCK_GRABBED;

		/* FIXME: there should be some block allocation policy for
		   nodes which contain wander records */

		/* We assume that disk space for wandered record blocks can be
		 * taken from reserved area. */
		ret = reiser4_alloc_blocks(&hint, &first, &len,
					   BA_FORMATTED | BA_RESERVED |
					   BA_USE_DEFAULT_SEARCH_START,
					   subv);
		reiser4_blocknr_hint_done(&hint);

		if (ret)
			return ret;

		allocated += len;

		/* create jnodes for all wander records */
		while (len--) {
			cur = reiser4_alloc_io_head(&first, subv);

			if (cur == NULL) {
				ret = RETERR(-ENOMEM);
				goto free_not_assigned;
			}

			ret = jinit_new(cur, reiser4_ctx_gfp_mask_get());

			if (ret != 0) {
				jfree(cur);
				goto free_not_assigned;
			}

			pin_jnode_data(cur);

			list_add_tail(&cur->capture_link, tx_list);

			first++;
		}
	}

	{ /* format a on-disk linked list of wander records */
		int serial = 1;

		txhead = list_entry(tx_list->next, jnode, capture_link);
		format_tx_head(ch, subv_id);

		cur = list_entry(txhead->capture_link.next, jnode, capture_link);
		while (tx_list != &cur->capture_link) {
			format_wander_record(ch, subv_id, cur, serial++);
			cur = list_entry(cur->capture_link.next, jnode, capture_link);
		}
	}

	{ /* Fill wander records with Wandered Set */
		struct store_wmap_params params;
		txn_atom *atom;

		params.cur = list_entry(txhead->capture_link.next, jnode, capture_link);

		params.idx = 0;
		params.capacity =
		    wander_record_capacity(reiser4_get_current_sb());

		atom = get_current_atom_locked();
		blocknr_set_iterator(atom,
				     &ch_sub->wander_map,
				     &store_wmap_actor, &params, 0, subv_id);
		spin_unlock_atom(atom);
	}

	{ /* relse all jnodes from tx_list */
		cur = list_entry(tx_list->next, jnode, capture_link);
		while (tx_list != &cur->capture_link) {
			jrelse(cur);
			cur = list_entry(cur->capture_link.next, jnode, capture_link);
		}
	}
	/*
	 * submit wander records
	 */
	ret = write_jnode_list(tx_list, fq, NULL, 0, subv);

	return ret;

 free_not_assigned:
	/*
	 * We deallocate blocks not yet assigned to jnodes on tx_list.
	 * The caller takes care about invalidating of tx list
	 */
	reiser4_dealloc_blocks(&first, &len,
			       BLOCK_NOT_COUNTED, BA_FORMATTED, subv);
	return ret;
}

static int commit_tx_subv(struct commit_handle *ch, u32 subv_id)
{
	int ret;
	flush_queue_t *fq;
	reiser4_subvol *subv = super_origin(ch->super, subv_id);
	struct commit_handle_subvol *ch_sub = &subv->ch;
	/*
	 * Grab more space for wandered records
	 */
	ret = reiser4_grab_space_force((__u64)(ch_sub->tx_size),
				       BA_RESERVED, subv);
	if (ret)
		return ret;
	fq = get_fq_for_current_atom();
	if (IS_ERR(fq))
		return PTR_ERR(fq);

	spin_unlock_atom(fq->atom);

	ret = alloc_submit_wander_blocks(ch, subv_id, fq);
	if (ret)
		goto exit;
	ret = alloc_submit_wander_records(ch, subv_id, fq);
 exit:
	reiser4_fq_put(fq);
	return ret;
}

static int commit_tx(struct commit_handle *ch)
{
	int ret;
	u32 subv_id;

	for_each_origin(subv_id) {
		ret = commit_tx_subv(ch, subv_id);
		if (ret)
			return ret;
	}
	ret = current_atom_finish_all_fq();
	if (ret)
		return ret;

	for_each_origin(subv_id) {
		ret = update_journal_header(ch, subv_id);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * Play (checkpoint) transaction on a simplest component of a compound volume.
 * @mirror can be an original subvolume, or a replica.
 */
static int play_tx_mirror(struct commit_handle *ch, reiser4_subvol *mirror)
{
	int ret;
	flush_queue_t *fq;
	struct commit_handle_subvol *ch_sub;
	/*
	 * replicas don't have their own commit handle,
	 * so borrow it form the original subvolume
	 */
	ch_sub = &super_origin(ch->super, mirror->id)->ch;
	fq = get_fq_for_current_atom();
	if (IS_ERR(fq))
		return  PTR_ERR(fq);
	spin_unlock_atom(fq->atom);

	ret = write_jnode_list(&ch_sub->overwrite_set,
			       fq, NULL, WRITEOUT_FOR_PAGE_RECLAIM, mirror);
	reiser4_fq_put(fq);
	return ret;
}

/**
 * Play (checkpoint) transaction on a logical (compound) volume.
 */
static int play_tx(struct commit_handle *ch)
{
	int ret;
	u32 orig_id;

	/*
	 * First of all,
	 * we issue per-component portions of IO requests in parallel.
	 */
	for_each_origin(orig_id) {
		u32 mirr_id;
		for_each_mirror(orig_id, mirr_id) {
			reiser4_subvol *mirror;
			mirror = current_mirror(orig_id, mirr_id);
			ret = play_tx_mirror(ch, mirror);
			if (ret)
				return ret;
		}
	}
	/*
	 * comply with write barriers
	 */
	ret = current_atom_finish_all_fq();
	if (ret)
		return ret;

	for_each_origin(orig_id) {
		ret = update_journal_footer(ch, orig_id);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * We assume that at this moment all captured blocks are marked as RELOC or
 * WANDER (belong to Relocate or Overwrite set), all nodes from Relocate set
 * are submitted to write.
 */
int reiser4_write_logs(long *nr_submitted)
{
	txn_atom *atom;
	struct super_block *super = reiser4_get_current_sb();
	reiser4_super_info_data *sbinfo = get_super_private(super);
	struct commit_handle ch;
	int ret;

	writeout_mode_enable();
	/*
	 * block allocator may add jnodes to the clean_list
	 */
	ret = reiser4_pre_commit_hook();
	if (ret)
		return ret;
	/*
	 * No locks are required if we take atom
	 * whose stage >= ASTAGE_PRE_COMMIT
	 */
	atom = get_current_context()->trans->atom;
	assert("zam-965", atom != NULL);
	/*
	 * relocate set is on the atom->clean_nodes list after
	 * current_atom_complete_writes() finishes. It can be safely
	 * uncaptured after commit_mutex is locked, because any atom that
	 * captures these nodes is guaranteed to commit after current one.
	 *
	 * This can only be done after reiser4_pre_commit_hook(), because
	 * it is where early flushed jnodes with CREATED bit are transferred
	 * to the overwrite list
	 */
	reiser4_invalidate_list(ATOM_CLEAN_LIST(atom));
	spin_lock_atom(atom);
	/* There might be waiters for the relocate nodes which we have
	 * released, wake them up. */
	reiser4_atom_send_event(atom);
	spin_unlock_atom(atom);

	if (REISER4_DEBUG) {
		int level;

		for (level = 0; level < REAL_MAX_ZTREE_HEIGHT + 1; ++level)
			assert("nikita-3352",
			       list_empty_careful(ATOM_DIRTY_LIST(atom,
								  level)));
	}

	sbinfo->nr_files_committed += (unsigned)atom->nr_objects_created;
	sbinfo->nr_files_committed -= (unsigned)atom->nr_objects_deleted;

	init_commit_handle(&ch, atom, NULL);
	/*
	 * count overwrite set and distribute it among subvolumes
	 */
	ret = get_overwrite_set(&ch);

	if (ret <= 0) {
		/*
		 * It is possible that overwrite set is empty here,
		 * which means all captured nodes are clean
		 */
		goto up_and_ret;
	}
	/*
	 * Inform the caller about what number of dirty pages
	 * will be submitted to disk
	 */
	*nr_submitted += ch.total_overwrite_set_size - ch.total_nr_bitmap;
	/*
	 * count all records needed for storing of the wandered set
	 */
	get_tx_size(&ch);

	ret = commit_tx(&ch);
	if (ret)
		goto up_and_ret;

	spin_lock_atom(atom);
	reiser4_atom_set_stage(atom, ASTAGE_POST_COMMIT);
	spin_unlock_atom(atom);
	reiser4_post_commit_hook();

	ret = play_tx(&ch);
 up_and_ret:
	if (ret) {
		/*
		 * there could be fq attached to current atom;
		 * the only way to remove them is:
		 */
		current_atom_finish_all_fq();
	}
	/*
	 * free blocks of flushed transaction
	 */
	dealloc_tx_list(&ch);
	dealloc_wmap(&ch);

	reiser4_post_write_back_hook();

	put_overwrite_set(&ch);

	done_commit_handle(&ch, NULL);

	writeout_mode_disable();

	return ret;
}

/**
 * consistency checks for journal data/control blocks: header, footer, log
 * records, transactions head blocks. All functions return zero on success
 */
static int check_journal_header(const jnode * node UNUSED_ARG)
{
	/* FIXME: journal header has no magic field yet. */
	return 0;
}

/**
 * wait for write completion for all jnodes from given list
 */
static int wait_on_jnode_list(struct list_head *head)
{
	jnode *scan;
	int ret = 0;

	list_for_each_entry(scan, head, capture_link) {
		struct page *pg = jnode_page(scan);

		if (pg) {
			if (PageWriteback(pg))
				wait_on_page_writeback(pg);

			if (PageError(pg))
				ret++;
		}
	}
	return ret;
}

static int check_journal_footer(const jnode * node UNUSED_ARG)
{
	/* FIXME: journal footer has no magic field yet. */
	return 0;
}

static int check_tx_head(const jnode * node)
{
	struct tx_header *header = (struct tx_header *)jdata(node);

	if (memcmp(&header->magic, TX_HEADER_MAGIC, TX_HEADER_MAGIC_SIZE) != 0) {
		warning("zam-627", "tx head at block %s corrupted\n",
			sprint_address(jnode_get_block(node)));
		return RETERR(-EIO);
	}
	return 0;
}

static int check_wander_record(const jnode * node)
{
	struct wander_record_header *RH =
	    (struct wander_record_header *)jdata(node);

	if (memcmp(&RH->magic, WANDER_RECORD_MAGIC, WANDER_RECORD_MAGIC_SIZE) !=
	    0) {
		warning("zam-628", "wander record at block %s corrupted\n",
			sprint_address(jnode_get_block(node)));
		return RETERR(-EIO);
	}
	return 0;
}

/**
 * Fill commit_handler structure by everything what is
 * needed to update journal footer of specified subvolume
 */
static int restore_commit_handle(struct commit_handle *ch,
				 reiser4_subvol *subv, jnode *tx_head)
{
	struct commit_handle_subvol *ch_sub = &subv->ch;
	struct tx_header *TXH;
	int ret;

	ret = jload(tx_head);
	if (ret)
		return ret;

	TXH = (struct tx_header *)jdata(tx_head);

	ch_sub->free_blocks = le64_to_cpu(get_unaligned(&TXH->free_blocks));
	ch->nr_files = le64_to_cpu(get_unaligned(&TXH->nr_files));
	ch->next_oid = le64_to_cpu(get_unaligned(&TXH->next_oid));

	jrelse(tx_head);

	list_add(&tx_head->capture_link, &ch_sub->tx_list);

	return 0;
}

/**
 * Overwrite blocks on permanent location by the wandered set.
 * and synchronize it with all replicas (if any).
 * Pre-condition: all mirrors should be already activated.
 */
static int replay_tx_subv(reiser4_subvol *subv)
{
	int ret;
	u32 mirr_id;
	const u32 orig_id = subv->id;
	struct commit_handle_subvol *ch_sub = &subv->ch;

	assert("edward-xxx", is_origin(subv));
	assert("edward-xxx", subvol_is_set(subv, SUBVOL_ACTIVATED));

	for_each_mirror(orig_id, mirr_id) {
		assert("edward-xxx",
		       subv->super ==
		       super_mirror(subv->super, orig_id, mirr_id)->super);

		subv = super_mirror(subv->super, orig_id, mirr_id);
		assert("edward-xxx", subvol_is_set(subv, SUBVOL_ACTIVATED));

		write_jnode_list(&ch_sub->overwrite_set, NULL, NULL, 0, subv);
		ret = wait_on_jnode_list(&ch_sub->overwrite_set);
		if (ret)
			goto error;
	}
	return 0;
 error:
	warning("edward-xxx",
		"transaction replay failed on %s (%d)", subv->name, ret);
	return RETERR(-EIO);
}

/**
 * This is an "offline" version of play_tx(). Called at mount time.
 * Replay one transaction: restore and write overwrite set in place
 */
static int replay_tx(jnode *tx_head,
		     const reiser4_block_nr *log_rec_block_p,
		     const reiser4_block_nr *end_block,
		     unsigned int nr_wander_records,
		     reiser4_subvol *subv)
{
	int ret;
	jnode *log;
	struct commit_handle ch;
	struct commit_handle_subvol *ch_sub = &subv->ch;
	reiser4_block_nr log_rec_block = *log_rec_block_p;

	assert("edward-xxx", !is_replica(subv));

	init_commit_handle(&ch, NULL, subv);
	restore_commit_handle(&ch, subv, tx_head);

	while (log_rec_block != *end_block) {
		struct wander_record_header *header;
		struct wander_entry *entry;

		int i;

		if (nr_wander_records == 0) {
			warning("zam-631",
				"number of wander records in the linked list"
				" greater than number stored in tx head.\n");
			ret = RETERR(-EIO);
			goto free_ow_set;
		}

		log = reiser4_alloc_io_head(&log_rec_block, subv);
		if (log == NULL)
			return RETERR(-ENOMEM);

		ret = jload(log);
		if (ret < 0) {
			reiser4_drop_io_head(log);
			return ret;
		}

		ret = check_wander_record(log);
		if (ret) {
			jrelse(log);
			reiser4_drop_io_head(log);
			return ret;
		}

		header = (struct wander_record_header *)jdata(log);
		log_rec_block = le64_to_cpu(get_unaligned(&header->next_block));

		entry = (struct wander_entry *)(header + 1);
		/*
		 * restore overwrite set from wander record content
		 */
		for (i = 0; i < wander_record_capacity(subv->super); i++) {
			reiser4_block_nr block;
			jnode *node;

			block = le64_to_cpu(get_unaligned(&entry->wandered));
			if (block == 0)
				break;

			node = reiser4_alloc_io_head(&block, subv);
			if (node == NULL) {
				ret = RETERR(-ENOMEM);
				/*
				 * FIXME-VS:???
				 */
				jrelse(log);
				reiser4_drop_io_head(log);
				goto free_ow_set;
			}

			ret = jload(node);

			if (ret < 0) {
				reiser4_drop_io_head(node);
				/*
				 * FIXME-VS:???
				 */
				jrelse(log);
				reiser4_drop_io_head(log);
				goto free_ow_set;
			}

			block = le64_to_cpu(get_unaligned(&entry->original));

			assert("zam-603", block != 0);

			jnode_set_block(node, &block);

			list_add_tail(&node->capture_link,
				      &ch_sub->overwrite_set);

			++entry;
		}

		jrelse(log);
		reiser4_drop_io_head(log);

		--nr_wander_records;
	}

	if (nr_wander_records != 0) {
		warning("zam-632",
			"number of wander records in the linked list "
			"is less than number stored in tx head.\n");
		ret = RETERR(-EIO);
		goto free_ow_set;
	}
	ret = replay_tx_subv(subv);
	ret = update_journal_footer(&ch, subv->id);

 free_ow_set:

	while (!list_empty(&ch_sub->overwrite_set)) {
		jnode *cur = list_entry(ch_sub->overwrite_set.next,
					jnode, capture_link);
		list_del_init(&cur->capture_link);
		jrelse(cur);
		reiser4_drop_io_head(cur);
	}

	list_del_init(&tx_head->capture_link);

	done_commit_handle(&ch, subv);

	return ret;
}

/**
 * Find oldest committed and not played transaction and play it. The transaction
 * was committed and journal header block was updated but the blocks from the
 * process of writing the atom's overwrite set in-place and updating of journal
 * footer block were not completed. This function completes the process by
 * recovering the atom's overwrite set from their wandered locations and writes
 * them in-place and updating the journal footer.
 */
static int replay_oldest_transaction(reiser4_subvol *subv)
{
	jnode *jf = subv->journal_footer;
	unsigned int total;
	struct journal_footer *F;
	struct tx_header *T;

	reiser4_block_nr prev_tx;
	reiser4_block_nr last_flushed_tx;
	reiser4_block_nr log_rec_block = 0;

	jnode *tx_head;

	int ret;

	if ((ret = jload(jf)) < 0)
		return ret;

	F = (struct journal_footer *)jdata(jf);

	last_flushed_tx = le64_to_cpu(get_unaligned(&F->last_flushed_tx));

	jrelse(jf);

	if (subv->last_committed_tx == last_flushed_tx) {
		/* all transactions are replayed */
		return 0;
	}

	prev_tx = subv->last_committed_tx;
	/*
	 * searching for oldest not flushed transaction
	 */
	while (1) {
		tx_head = reiser4_alloc_io_head(&prev_tx, subv);
		if (!tx_head)
			return RETERR(-ENOMEM);

		ret = jload(tx_head);
		if (ret < 0) {
			reiser4_drop_io_head(tx_head);
			return ret;
		}

		ret = check_tx_head(tx_head);
		if (ret) {
			jrelse(tx_head);
			reiser4_drop_io_head(tx_head);
			return ret;
		}

		T = (struct tx_header *)jdata(tx_head);

		prev_tx = le64_to_cpu(get_unaligned(&T->prev_tx));

		if (prev_tx == last_flushed_tx)
			break;

		jrelse(tx_head);
		reiser4_drop_io_head(tx_head);
	}

	total = le32_to_cpu(get_unaligned(&T->total));
	log_rec_block = le64_to_cpu(get_unaligned(&T->next_block));

	pin_jnode_data(tx_head);
	jrelse(tx_head);

	ret = replay_tx(tx_head, &log_rec_block,
			jnode_get_block(tx_head), total - 1, subv);

	unpin_jnode_data(tx_head);
	reiser4_drop_io_head(tx_head);

	if (ret)
		return ret;
	return -E_REPEAT;
}

/**
 * The reiser4 journal current implementation was optimized to not to capture
 * super block if certain super blocks fields are modified. Currently, the set
 * is (<free block count>, <OID allocator>). These fields are logged by
 * special way which includes storing them in each transaction head block at
 * atom commit time and writing that information to journal footer block at
 * atom flush time. For getting the info from journal footer block to the
 * in-memory super block there is a special function
 * reiser4_journal_recover_sb_data() which should be called after disk format
 * plugin re-reads super block after journal replaying.
 *
 * Get the information from journal footer to in-memory super block
 */
int reiser4_journal_recover_sb_data(struct super_block *s, reiser4_subvol *subv)
{
	struct journal_footer *jf;
	int ret;

	assert("zam-673", subv->journal_footer != NULL);

	ret = jload(subv->journal_footer);
	if (ret != 0)
		return ret;

	ret = check_journal_footer(subv->journal_footer);
	if (ret != 0)
		goto out;

	jf = (struct journal_footer *)jdata(subv->journal_footer);
	/*
	 * was there at least one flushed transaction?
	 */
	if (jf->last_flushed_tx) {
		/*
		 * restore free block counter logged in this transaction
		 */
		reiser4_subvol_set_free_blocks(subv,
				  le64_to_cpu(get_unaligned(&jf->free_blocks)));
		/*
		 * restore oid allocator state
		 */
		oid_init_allocator(s,
				   le64_to_cpu(get_unaligned(&jf->nr_files)),
				   le64_to_cpu(get_unaligned(&jf->next_oid)));
	}
 out:
	jrelse(subv->journal_footer);
	return ret;
}

/**
 * reiser4 replay journal procedure
 */
int reiser4_journal_replay(reiser4_subvol *subv)
{
	jnode *jh, *jf;
	struct journal_header *header;
	int nr_tx_replayed = 0;
	int ret;

	assert("edward-xxx", subv != NULL);

	jh = subv->journal_header;
	jf = subv->journal_footer;

	if (!jh || !jf) {
		/*
		 * It is possible that disk layout does not
		 * support journal structures, we just warn about this
		 */
		warning("zam-583",
			"Journal control blocks were not loaded on %s. "
			"Journal replay is not possible.\n", subv->name);
		return 0;
	}
	/*
	 * Take free block count from journal footer block. The free block
	 * counter value corresponds the last flushed transaction state
	 */
	ret = jload(jf);
	if (ret < 0)
		return ret;

	ret = check_journal_footer(jf);
	if (ret) {
		jrelse(jf);
		return ret;
	}
	jrelse(jf);
	/*
	 * store last committed transaction info in
	 * reiser4 in-memory superblock
	 */
	ret = jload(jh);
	if (ret < 0)
		return ret;

	ret = check_journal_header(jh);
	if (ret) {
		jrelse(jh);
		return ret;
	}
	header = (struct journal_header *)jdata(jh);
	subv->last_committed_tx =
		le64_to_cpu(get_unaligned(&header->last_committed_tx));

	jrelse(jh);

	/* replay committed transactions */
	while ((ret = replay_oldest_transaction(subv)) == -E_REPEAT)
		nr_tx_replayed++;

	return ret;
}

/**
 * Load journal control block (either journal header or journal footer block)
 */
static int load_journal_control_block(jnode **node,
				      const reiser4_block_nr *block,
				      reiser4_subvol *subv)
{
	int ret;

	*node = reiser4_alloc_io_head(block, subv);
	if (!(*node))
		return RETERR(-ENOMEM);

	ret = jload(*node);

	if (ret) {
		reiser4_drop_io_head(*node);
		*node = NULL;
		return ret;
	}

	pin_jnode_data(*node);
	jrelse(*node);

	return 0;
}

/**
 * Unload journal header or footer and free jnode
 */
static void unload_journal_control_block(jnode ** node)
{
	if (*node) {
		unpin_jnode_data(*node);
		reiser4_drop_io_head(*node);
		*node = NULL;
	}
}

/**
 * Release journal control blocks
 */
void reiser4_done_journal_info(reiser4_subvol *subv)
{
	unload_journal_control_block(&subv->journal_header);
	unload_journal_control_block(&subv->journal_footer);
	rcu_barrier();
}

/**
 * Load journal control blocks.
 * Pre-condition: @subv contains valid journal location
 */
int reiser4_init_journal_info(reiser4_subvol *subv)
{
	int ret;
	journal_location *loc = &subv->jloc;

	assert("zam-652", loc->header != 0);
	assert("zam-653", loc->footer != 0);

	ret = load_journal_control_block(&subv->journal_header,
					 &loc->header, subv);
	if (ret)
		return ret;

	ret = load_journal_control_block(&subv->journal_footer,
					 &loc->footer, subv);
	if (ret)
		unload_journal_control_block(&subv->journal_header);
	return ret;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
