/* -*- c -*- */
/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Journal implementation.
 */

/* $Id */

#include "defines.h"

/** return pointer to the outermost transcrash for this process */
reiser4_transaction *reiser4_get_outermost_trans( reiser4_journal *journal )
{
	return NULL;
}

/** Initialise transaction (or transcrash). */
void init_transaction( reiser4_transaction *trans, reiser4_journal *journal )
{
	assert( "nikita-437", trans != NULL );
	assert( "nikita-438", journal != NULL );

	memset( trans, 0, sizeof *trans );
	trans -> journal = journal;
	/* if this is outermost transcrash, link trans -> trans to
	   itself, otherwise, make it to point to the outermost
	   transacrash. */
	trans -> trans = reiser4_get_outermost_trans( journal ) ? : trans;
}

/*
 * $Log$
 * Revision 4.3  2001/10/14 21:48:02  jmacd
 * Delete some stuff at Hans' request.
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 4.2  2001/10/11 09:21:09  god
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
 * Revision 4.1  2001/10/05 08:50:46  god
 * corrections after compilation in user-level with stricter
 * compiler checks.
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
