/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Data-types and functions for journalling.
 */

/* $Id$ */

#if !defined( __FS_REISER4_JOURNAL_H__ )
#define __FS_REISER4_JOURNAL_H__

struct reiser4_transaction;
typedef struct reiser4_transaction reiser4_transaction;

struct reiser4_transaction {
	/* for outermost transactions this points to itself.
	   For nested transactions, this points to outermost. */
	reiser4_transaction *trans;
	/* journal this transaction is in */
	reiser4_journal     *journal;
	int                  blocks_allocated;
	int                  blocks_used;
#if REISER4_DEBUG
	void                *initiator;
#endif
};

struct reiser4_journal {
	/** start transaction with given number of blocks reserved.
	    In debug mode, this should store __builtin_return_address( 0 )
	    in trans -> initiator. */
	int ( *begin )( reiser4_transaction *trans, int blocks );
	int ( *end )( reiser4_transaction *trans );
	int ( *involve )( reiser4_transaction *trans, struct page *page );
	int ( *release )( reiser4_transaction *trans, struct page *page );
};

/** return pointer to the outermost transacrash for this process */
extern reiser4_transaction *reiser4_get_outermost_trans( reiser4_journal *journal );

/** Initialise transaction (or transcrash). */
extern void init_transaction( reiser4_transaction *trans, 
			      reiser4_journal *journal );

/* __FS_REISER4_JOURNAL_H__ */
#endif

/*
 * $Log$
 * Revision 1.8  2001/10/14 21:48:02  jmacd
 * Delete some stuff at Hans' request.
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.7  2001/10/05 08:50:46  god
 * corrections after compilation in user-level with stricter
 * compiler checks.
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.6  2001/09/28 19:18:10  god
 * Du cote de chez Compilation:
 *   debug.c, tree.c, znode.c, vfs_ops.c, key.c, inode.c, super.c,
 *   plugin/plugin.c, plugin/item/item.c, plugin/item/static_stat.c,
 *   plugin/object.c, plugin/node/node.c, plugin/file/file.c, plugin/hash.c
 *
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.5  2001/09/09 16:53:39  god
 * get rid of unsigned
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.4  2001/09/03 21:23:15  reiser
 * I would like a lighter weight less complex plugin infrastructure.  Let us start from specific plugins, decide what they need, then generalize the specific into the general, rather than first creating a general mechanism.
 *
 * Revision 1.3  2001/08/27 09:43:47  god
 * extra indirection from journal to journal_operations removed
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.2  2001/08/21 14:32:50  god
 * comment about debugging in ->begin()
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.1  2001/08/20 16:04:51  god
 * journal.h: stubs for journal data structures (page based journal)
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
