/*
 *
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 *
 */

/* $Id$ */

/* Interface to the (leaf) level nodes in the reiserfs tree.

   Description: The tree provides the abstraction of flows, which it
   internally fragments into items which it stores in nodes.

   An atom is a piece of data bound to a single key.

   For reasonable space efficiency to be achieved it is often
   necessary to store atoms in the nodes in the form of items, where
   an item is a sequence of atoms of the same or similar type. It is
   more space-efficient, because the item can implement (very)
   efficient compression of atom's bodies using internal knowledge
   about their semantics, and it can often avoid having a key for each
   atom. Each type of item has specific operations implemented by its
   item handler (see balance.c).

   Rationale: the rest of the code (specifically balancing routines)
   accesses leaf level nodes through this interface. This way we can
   implement various block layouts and even combine various layouts
   within the same tree. Balancing/allocating algorithms should not
   care about peculiarities of splitting/merging specific item types,
   but rather should leave that to the item's item handler.

   Items, including those that provide the abstraction of flows, have
   the property that if you move them in part or in whole to another
   node, the balancing code invokes their is_left_mergeable()
   item_operation to determine if they are mergeable with their new
   neighbor in the node you have moved them to.  For some items the
   is_left_mergeable() function always returns null.

   Possible implementations of this interface include:

   (*) layout that doesn't perform any kind of keys/headers
   compression and relies on item handlers to perform appropriate
   item's body compression.  Probably will be used in the first v4
   release.

   (*) stem compression of item keys.

   (*) reiserfs v3.? formats --- this gives us support of old formats.

   (*) general purpose compression of keys and data within node.

   To copy atoms from node to node it's necessary to unpack-pack them
   iteratively which, in general case, is prohibitively
   expensive. When both nodes use the same layout, its possible to do
   fast copying (->fast_copy() method) that is supposed to use
   knowledge about internal structure of layout to speed-up
   operation. In a case of different layouts you are out of luck
   because we are unwilling to code N^2 layout conversion methods
   where N is the number of layouts supported.

   When moving the bodies of items from one node to another:

   * if a partial item is shifted to another node the balancing code invokes
     an item handler method to handle the item splitting.

   * if the balancing code needs to merge with an item in the node it
     is shifting to, it will invoke an item handler method to handle
     the item merging.

   * if it needs to move whole items unchanged, the balancing code uses memcpy()
     adjusting the item headers after the move is done using the node handler.
*/

				/* Nikita, you really didn't listen to
				   what I was saying about item
				   handlers, and what things are their
				   task.  I cut massive sections of
				   code out of this file. */

static char cvsid[] = "@(#)$Id$";
static char debugFileId[] = __FILE__;

#include "key.h"
#include "debug.h"

/** data-type for the node of the tree. Actually this is used for leaf
	nodes only for now. 

	Why? -Hans

	NOTE: It is not clear how this should be different from znode. If
	it turns out that they are basically the same thing, just change
	following typedef to alias node and znode.

	Needs a comment about the whole node not being defined/spanned by this struct.

*/

/* A node's node_layout is determined by a magic number that is the first byte of the node. */



/*
 * $Log$
 * Revision 4.2  2001/09/06 08:50:38  reiser
 * Got tired of asking nikita to move these files to the plugin directory.
 *
 * Revision 4.1  2001/09/03 21:23:15  reiser
 * I would like a lighter weight less complex plugin infrastructure.  Let us start from specific plugins, decide what they need, then generalize the specific into the general, rather than first creating a general mechanism.
 *
 * Revision 4.0.0.1  2001/08/10 18:26:29  reiser
 * Second attempt to create reiser4 repository.
 *
 * Revision 1.1  2001/08/10 17:49:40  reiser
 * *** empty log message ***
 *
 * Revision 1.15  2001/08/10 16:24:29  reiser
 * Creating an initial schema for dividing source code into files for version 4.  Broke balance.c into many files.
 *
 * Revision 1.14  2001/07/27 11:34:41  reiser
 * Another day of making things a little bit more coherent.
 *
 * Revision 1.13  2001/07/15 18:31:38  reiser
 * Long uncommitted but minor I think stuff.
 *
 * Revision 1.12  2001/02/09 17:17:10  reiser
 * added policy variable to guide space optimization of balancing
 *
 * Revision 1.11  2001/02/05 14:19:56  reiser
 * evolved it a little bit further
 *
 * Revision 1.10  2001/01/25 18:11:12  god
 * typos in comments corrected (to some extent).
 *
 * Revision 1.9  2001/01/25 17:49:40  god
 * functions and data-structures commented
 *
 * Revision 1.8  2001/01/25 12:59:43  reiser
 * Maybe things are in sync now.
 *
 * Revision 1.7  2001/01/24 19:16:08  god
 * merged changes to the terminology made by Hans.
 *
 * Revision 1.6  2001/01/24 11:41:10  reiser
 * Nikita, read the comment about how insert_into_node should be driven by balancing, it should not drive balancing.
 *
 * Revision 1.5  2001/01/22 19:18:26  god
 * added `merged item' (aka `virtual item' in v3.?)
 *
 * Revision 1.4  2001/01/22 17:32:39  god
 * Complete re-write based on item_handler_ops
 *
 * Revision 1.3  2001/01/19 17:34:26  god
 * typedefs rearranged to agree with newly established convention
 *
 * Revision 1.2  2001/01/18 14:58:02  god
 * use atom_deck in stead of size_t for estimations of free space during balancing
 *
 * Revision 1.1.1.1  2001/01/17 14:48:18  lim
 * Initial import of reiserfs4 sources
 *
 */
