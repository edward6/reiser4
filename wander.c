/* JOURNALING:




There may be (by default is) a dedicated journaling area.  Its purpose
is to allow a batch of nodes that will not be logged in their optimal
location to all be journaled together efficiently in one seek and one
write (or perhaps to be journalled using NVRAM.)

The "preserve" of a transaction is the set of blocks which may not be
overwritten before the transaction is committed, but may be
overwritten after it is committed.  The "minover" of a transaction is
the set of blocks which must be overwritten after the transaction is
committed.  There is no minover buffer which is a child of another
minover buffer.  The "optover" is the set of blocks which it would be
optimal to overwrite after the transaction is completed because of
buffers in the transaction having the location of a preserve block as
their most optimal location.

Flush plugins interface with journaling by setting the
znode->true_blocknr, and if the true_blocknr is part of the preserve
set then they call set_journal_block to determine the
wandered_blocknr, otherwise the wandered_blocknr is set to 0.

Modified bitmaps in v4.0 are part of the minover.

We need one bitmap that contains the union of all bitmaps of all
uncommitted transcrashes in progress, plus a version of each bitmap
for each transaction in progress, plus a bitmap that represents the
state should all uncommitted transcreashes fail.



Wandering posts have a mapping of blocks that are in the journal to
where they should go when committed.  When the block size is 4k and
the blocknr size is 4 bytes there is one wandering post per megabyte
of disk, it is one 4k block in size with 2040 bytes unused (we can
tune this later), and it is large enough to map every block in the
interval between it and the next wandering post (this is 0.4% space
wasteful but simplifies worst case handling code, we can get more
complex later if we want to).  The mapping is of form (wandered
blocknr, true blocknr).  Commit is performed by writing the next
wandering post which may have null contents.  I wonder if this way of
doing commits is bad design or good design....  All wandering posts
have a sequence number and a number of other posts with the same
sequence number, the most recent one(s) should have all of its (their)
contents discarded, the penultimate one(s) should be replayed, the
others are ignored.  More complex schemes that segment the sequence
number space are possible in the future to allow nested transactions.
When the system is idle and there is no new transaction and replay is
completed and the penultimate wandering post was non-empty of
mappings, the filesystem writes an empty new wandering post which
serves to indicate that replay has been performed already.

At commit time, cease allowing new joinings of the transaction, copy
on write all modifications of buffers in the closed transaction from
this point in time until the time that the sequence number is
committed, and do not allow modification of disk blocks in the
transaction until replay is completed, then update one of the
wandering posts to contain a new sequence number thereby committing
this transaction, then replay the wandering post.

Note that where we gain over a fixed log, is when the wandered
blocknrs equal the true blocknrs, in which case the block only needs
to be written to disk once.  How to optimize the allocation of the
wandered blocks for minimum I/O cost and minimum distortion of true
block I/O is an area I haven't resolved.

If a block has old contents already on disk (that should not be thrown
away without replacing them with something newer) then the block must
be wander-journaled for it to be reallocated.  If it has never been
written it can be reallocated without wander-journaling it.

*/

#include "reiser4.h"

typedef struct _log_record_header log_record_header;
typedef struct _log_record_footer log_record_footer;
typedef struct _log_record_block  log_record_block;
typedef struct _log_region        log_region;

enum log_record_types {

	LOG_HEADER_MAGIC = 0xBCD1BB65U,
	LOG_BLOCK_MAGIC  = 0x42C2DFFEU,
	LOG_FOOTER_MAGIC = 0x9666431BU,
};

struct _log_region {
	/* A single region of the log. */

	reiser4_block_nr base; /* first block */
	__u32             blks; /* number of blocks */
	__u32             generation;
};

/*
 * Diagram of a two-block log record.
 *
 * __________________________________________________
 * |        |            |            |             |
 * | log    | list of    | list of    | delete      |
 * | record | atoms      | do-not-    | set         |
 * | header | reposessed | replay     | (start)     |
 * |        | for        | atoms      |             |
 * |________|____________|____________|_____________|
 * |
 * BLOCK 0
 *
 * __________________________________________________
 * |        |            |                 |        |
 * | log    | delete     | wander          | log    |
 * | record | set        | set             | record |
 * | block  | (cont)     |                 | footer |
 * |        |            |                 |        |
 * |________|____________|_________________|________|
 * |
 * BLOCK 1
 *
 */

struct _log_record_header {
	/* First 32 bytes of a log record */

	d32   magic;        /* Indicates type (LOG_HEADER_MAGIC) */
	d32   atom_id;      /* Committing atom/transaction ID */
	d32   generation;   /* Increment each time the log wraps */
	d32   num_blocks;   /* Number of blocks in this record */

	d32   num_repos;    /* Number of atoms this atom repossesses for */
	d32   num_finish;   /* Number of atoms which now should not be replayed */
	d32   num_delete;   /* Number of members in the delete set */
	d32   num_wander;   /* Number of wandered block pairs */
};

struct _log_record_block {
	/* First 16 bytes of subsequent blocks in a log record */

	d32   magic;        /* Indicates type (LOG_BLOCK_MAGIC)  */
	d32   atom_id;      /* Matches header */
	d32   generation;   /* Matches header */
	d32   sequence;     /* Sequence number (1 ... num_blocks) */
};

struct _log_record_footer {
	/* Last 16 bytes of the last block in a log record */

	d32   magic;        /* Indicates type (LOG_FOOTER_MAGIC)  */
	d32   atom_id;      /* Matches header */
	d32   generation;   /* Matches header */
	d32   checksum;     /* Checksum of ... */
};

int block_is_allocated (const reiser4_block_nr *blocknr UNUSED_ARG)
{
	/* stub function */
	return 0;
}

void mark_in_delete_set (txn_atom *atom UNUSED_ARG, znode *node UNUSED_ARG)
{
}

void mark_in_wander_set (txn_atom *atom UNUSED_ARG, znode *node UNUSED_ARG)
{
}

/* This function allocates a buffer of the appropriate size for formating a block of the
 * log.  It returns one pointer at the head of this buffer and one pointer at the end of
 * this buffer, for determining when the buffer is out of space.
 */
int new_log_buffer (__u32 blksize, char **buf_ptr, char **buf_end_ptr)
{
	(*buf_ptr) = reiser4_kmalloc (blksize, GFP_KERNEL);

	if (*buf_ptr == NULL) {
		return -ENOMEM;
	}

	(*buf_end_ptr) = (*buf_ptr) + blksize;

	return 0;
}

/* This function allocates buffer of the appropriate size for formating a block of the log
 * after the first block (i.e., subsequent).  It formats the log_record_block at the head
 * of this buffer and returns two pointers: one at the head of the remaining space and one
 * at the end of the buffer, for determining when the buffer is out of space.
 */
int sub_log_buffer (__u32 blksize, log_record_header *header, char **buf_ptr, char **buf_end_ptr)
{
	int ret;
	log_record_block *block;

	if ((ret = new_log_buffer (blksize, buf_ptr, buf_end_ptr))) {
		return ret;
	}

	block = (log_record_block*) (*buf_ptr);

	cputod32 (LOG_BLOCK_MAGIC, & block->magic);

	/* FIXME_JMACD compute generation -josh */

	block->atom_id    = header->atom_id;
	block->generation = header->generation;
	block->sequence   = header->num_blocks;

	cputod32 (d32tocpu (& header->num_blocks) + 1, & header->num_blocks);

	(*buf_ptr) += sizeof (log_record_block);

	return 0;
}

extern __u32 sys_lrand (__u32 max);

/* This function is named "random_commit_record" because it doesn't really format a valid
 * commit record, it is only here for me to structure the code that formats a log buffer.
 * It uses random values, but the outline of the code is correct.
 */
int random_commit_record (txn_atom *atom, log_region *region, struct super_block *super)
{
#if 0
	int   ret;
	__u32 i;

	char              *buffer;
	char              *buffer_max;
	log_record_header *header;
	log_record_footer *footer;

	__u32 blksize    = super->s_blocksize;
	__u32 num_repos  = sys_lrand (10);
	__u32 num_finish = sys_lrand (10);
	__u32 num_delete = sys_lrand (100);
	__u32 num_wander = sys_lrand (100);

	__u32 checksum   = 0; /* FIXME_JMACD compute checksum -josh */
	
	assert ("jmacd-1110", (sizeof (log_record_header) + sizeof (log_record_footer)) <= (blksize + sizeof (d64)));

	if ((ret = new_log_buffer (blksize, & buffer, & buffer_max))) {
		return ret;
	}

	header = (log_record_header*) buffer;

	cputod32 (LOG_HEADER_MAGIC,   & header->magic);
	cputod32 (atom->atom_id,      & header->atom_id);
	cputod32 (region->generation, & header->generation);
	cputod32 (1,                  & header->num_blocks);

	buffer += sizeof (log_record_header);

#define NEXT_LOG_VALUE(TYPE,VALUE)                                                      \
        do {                                                                            \
		if ((buffer + sizeof (TYPE) > buffer_max) &&                            \
                    (ret = sub_log_buffer (blksize, header, & buffer, & buffer_max))) { \
			return ret;                                                     \
		}                                                                       \
		cputo ## TYPE ((VALUE), (TYPE*) buffer);                                \
		buffer += sizeof (TYPE);                                                \
        } while (0)

	for (i = 0; i < num_repos; i += 1) {
		NEXT_LOG_VALUE (d32, 0);
	}

	for (i = 0; i < num_finish; i += 1) {
		NEXT_LOG_VALUE (d32, 0);
	}

	for (i = 0; i < num_delete; i += 1) {
		NEXT_LOG_VALUE (d64, 0LL);
	}

	for (i = 0; i < num_wander; i += 1) {
		NEXT_LOG_VALUE (d64, 0LL);
		NEXT_LOG_VALUE (d64, 0LL);
	}

	if ((buffer + sizeof (log_record_footer) > buffer_max) &&
	    (ret = sub_log_buffer (blksize, header, & buffer, & buffer_max))) {
		return ret;
	}

	cputod32 (num_repos,  & header->num_repos);
	cputod32 (num_finish, & header->num_finish);
	cputod32 (num_delete, & header->num_delete);
	cputod32 (num_wander, & header->num_wander);

	footer = (log_record_footer*) (buffer_max - sizeof (log_record_footer));

	cputod32 (LOG_FOOTER_MAGIC, & footer->magic);

	footer->atom_id    = header->atom_id;
	footer->generation = header->generation;

	cputod32 (checksum, & footer->checksum);

#endif
	return 0;
}
/* This function will be combined with the logic of "random_commit_record" above, but
 * first there are some other issues to work out.  At that point, random_commit_record
 * will go away and this will be the main entry point for formatting a log record.
 */
int format_commit_record (txn_atom *atom UNUSED_ARG)
{
#if 0
	int level;
	znode *scan;

	for (level = 0; level < REAL_MAX_ZTREE_HEIGHT; level += 1) {

		for (scan = capture_list_front (& atom->capture_level[level]);
		     /**/ ! capture_list_end   (& atom->capture_level[level], scan);
		     scan = capture_list_next  (scan)) {

			if (ZF_ISSET (scan, ZNODE_ALLOC)) {

				/* Newly allocated blocks are not recorded in the commit
				 * record. */
				assert ("jmacd-1101", ! ZF_ISSET (scan, ZNODE_WANDER) && ! znode_is_in_deleteset (scan));
				assert ("jmacd-1102", block_is_allocated (& scan->blocknr));

			} else if (znode_is_in_deleteset (scan)) {

				/* Add it to the delete set */
				assert ("jmacd-1103", ! ZF_ISSET (scan, ZNODE_WANDER));
				assert ("jmacd-1104", block_is_allocated (& scan->blocknr));
				assert ("jmacd-1105", ergo (ZF_ISSET (scan, ZNODE_RELOC), block_is_allocated (& scan->relocnr)));

				/* Form list of scan->blocknrs */
				mark_in_delete_set (atom, scan);

			} else if (ZF_ISSET (scan, ZNODE_WANDER)) {

				/* Not delete set, not alloc implies wandered */
				assert ("jmacd-1107", block_is_allocated (& scan->blocknr));
				assert ("jmacd-1108", block_is_allocated (& scan->relocnr));

				/* Form list of pairs (scan->blocknr, scan->relocnr) */
				mark_in_wander_set (atom, scan);

			} else {

				/* Unmodified, captured node.  Should release at or prior
				 * to this point (atom closing). */
			}
		}
	}
#endif

	return 0;
}

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
