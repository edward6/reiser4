/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined (__FS_REISER4_WANDER_H__)
#define __FS_REISER4_WANDER_H__

#include "dformat.h"

#include <linux/fs.h>		/* for struct super_block  */

/* REISER4 JOURNAL ON-DISK DATA STRUCTURES   */

#define TX_HEADER_MAGIC  "TxMagic4"
#define LOG_RECORD_MAGIC "LogMagc4"

#define TX_HEADER_MAGIC_SIZE  (8)
#define LOG_RECORD_MAGIC_SIZE (8)

/* journal header block format */
struct journal_header {
	/* last written transaction head location */
	d64 last_committed_tx;
};

/* ZAM-FIXME-HANS: a journal footer is a what? */
/* journal footer block format */
struct journal_footer {
	/* last flushed transaction location. */
	/* This block number is no more valid after the transaction it points
	   to gets flushed, this number is used only at journal replaying time
	   for detection of the end of on-disk list of committed transactions
	   which were not flushed completely */
	d64 last_flushed_tx;

	/* free block counter is written in journal footer at transaction
	   flushing , not in super block because free blocks counter is logged
	   by another way than super block fields (root pointer, for
	   example). */
	d64 free_blocks;

	/* number of used OIDs and maximal used OID are logged separately from
	   super block */
	d64 nr_files;
	d64 next_oid;
};

/* ZAM-FIXME-HANS: a log_record_header is a what? a record is a what?  a log is a what? */
/* each log record (except first one) has unified format with log record
   header followed by an array of log entries */
struct log_record_header {
	/* when there is no predefined location for log records, this magic
	   string should help reiser4fsck. */
	char magic[LOG_RECORD_MAGIC_SIZE];

	/* transaction id */
	d64 id;

	/* total number of log records in current transaction  */
	d32 total;

	/* this block number in transaction */
	d32 serial;

	/* number of previous block in commit */
	d64 next_block;
};

/* The first log record (transaction head) of written transaction has the
   special format */
struct tx_header {
	/* magic string makes first block in transaction different from other
	   logged blocks, it should help fsck. */
	char magic[TX_HEADER_MAGIC_SIZE];

	/* transaction id */
	d64 id;

	/* total number of records (including this first tx head) in the
	   transaction */
	d32 total;

	/* align next field to 8-byte boundary; this field always is zero */
	d32 padding;

	/* block number of previous transaction head */
	d64 prev_tx;

	/* next log record location */
	d64 next_block;

	/* committed versions of free blocks counter */
	d64 free_blocks;

	/* number of used OIDs (nr_files) and maximal used OID are logged separately from
	   super block */
	d64 nr_files;
	d64 next_oid;
};

/* A transaction gets written to disk as a set of log records (each log record
   size is fs block) */

/* ZAM-FIXME-HANS: "rest" implies that you have already defined a part. You have not. */
/* rest of log record is filled by these log entries, unused space filled by
   zeroes */
struct log_entry {
	d64 original;		/* block original location */
	d64 wandered;		/* block wandered location */
};

/* REISER4 JOURNAL WRITER FUNCTIONS   */

extern int reiser4_write_logs(long *);
extern int reiser4_journal_replay(struct super_block *);
extern int reiser4_journal_recover_sb_data(struct super_block *);

extern int init_journal_info(struct super_block *, const reiser4_block_nr *, const reiser4_block_nr *);
extern void done_journal_info(struct super_block *);

extern int write_jnode_list (capture_list_head*, flush_queue_t *);
#endif				/* __FS_REISER4_WANDER_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
