/* -*- c -*- */
/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * IO functions.
 */

#include "defines.h"

/** true if block number makes sense. Used to detect tree corruptions. */
int block_nr_is_correct( reiser4_disk_addr *block, reiser4_io *io )
{
	assert( "nikita-435", block != NULL );
	assert( "nikita-436", io != NULL );

	return ( io -> min_block.blk <= block -> blk ) &&
		( block -> blk < io -> max_block.blk );
}

/*
 * $Log$
 * Revision 1.3  2001/10/14 21:39:05  reiser
 * It really annoys me when you add layers of abstraction without bothering to have a seminar first.
 *
 * Revision 1.2  2001/10/11 09:21:09  god
 * message-ids are added to nikita's assertions, warnings etc.:
 * Summary for user nikita:
 * Found 463 unique error IDs
 * Found 0 duplicate error IDs
 * Found 0 assertions without error IDs
 * Max error ID is 749
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.1  2001/10/05 08:49:21  god
 * file to hold io-related functions
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 */
/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
