/* Copyright 2002 by Hans Reiser*/

#if !defined (__FS_REISER4_WANDER_H__)
#define __FS_REISER4_WANDER_H__

/*****************************************************************************************
                            REISER4 JOURNAL ON-DISK DATA STRUCTURES  
 *****************************************************************************************/

#define WRITE_LOG 0

#define TX_HEADER_MAGIC  "TxMagic4"
#define LOG_RECORD_MAGIC "LogMagc4"

#define TX_HEADER_MAGIC_SIZE  (8)
#define LOG_RECORD_MAGIC_SIZE (8)

/* journal header block format */
struct journal_header {
	/* last written transaction head location */
	d64      last_committed_tx;
};

/* journal footer block format */
struct journal_footer {
	/* last flushed transaction location.*/
	/* This block number is no more valid after the transaction it points
	 * to gets flushed, this number is used only at journal replaying time
	 * for detection of the end of on-disk list of committed transactions
	 * which were not flushed completely */
	d64      last_flushed_tx;
};

/* The first log record (transaction head) of written transaction has the
 * special format */
struct tx_header {
	/* magic string makes first block in transaction different from other
	 * logged blocks, it should help fsck. */
	char     magic[TX_HEADER_MAGIC_SIZE];

	/* transaction id*/
	d64      id;

	/* total number of records (including this first tx head) in the
	 * transaction */
	d32      total;

	/* align next field to 8-byte boundary; this field always is zero */
	d32      padding;

	/* block number of previous transaction head */
	d64      prev_tx;

	/* next log record location */
	d64      next_block;

	/* committed versions of free blocks counter */
	d64      free_blocks;
};
/* FIXME-ZAM: rest of block is unused currently, I plan to put wandered map there */

/* A transaction gets written to disk as a set of log records (each log record
 * size is fs block) */

/* each log record (except first one) has unified format with log record
 * header followed by an array of log entries */
struct log_record_header {
	/* when there is no predefined location for log records, this magic
	 * string should help reiser4fsck. */
	char     magic[LOG_RECORD_MAGIC_SIZE];

	/* transaction id */
	d64      id;

	/* total number of log records in current transaction  */
	d32      total;

	/* this block number in transaction */			
	d32      serial;

	/* number of previous block in commit */
	d64      next_block;
};

/* rest of log record is filled by these log entries, unused space filled by
 * zeroes */
struct log_entry {
	d64      original;	/* block original location */
	d64      wandered;	/* block wandered location */
};

/*****************************************************************************************
                            REISER4 JOURNAL WRITER FUNCTIONS  
 *****************************************************************************************/

extern int reiser4_write_logs    (void);

#endif /* __FS_REISER4_WANDER_H__ */
