/* We need a definition of the default node layout here. */


typedef enum { found, notfound, ioerror } search_t;
typedef enum { ok, nospace, ioerror } insert_t;
typedef enum { ok, nofound, nospace, ioerror } copy_t;
typedef enum { ok, fail, ioerror } ret_t;
typedef enum { left_side, right_side } side_t;
typedef enum { left_neigh, right_neigh } find_t;
typedef enum { ok, eon, ioerror } iter_t;

struct node_layout;
typedef struct node_layout node_layout;
/**
   Interface to the layout of node: layout is a way to store items
   inside of the node (which is wrapper around buffer or page).

   Responsibility of node layout is to give access to sequence of
   items within node, shrink or expand (given enough free space) items
   and provide information about overhead (space-wise) of the creation
   of new items. It may prove effective to add parameters to indicate
   exact position within node that operation is to be performed at.
   For example, it may be useful to have a pointer or an item_pos,
   that if not null is used.  This I leave to the coders to decide.  */
struct node_layout
{
  /* calculates the amount of space that will be required to store an
	 item which is in addition to the space consumed by the item body.
	 the space consumed by the item body can be gotten by calling
	 item->estimate */
  size_t           ( *item_overhead )( node_layout *layout, znode *znode,
									   flow *flow );
  size_t           ( *move_items )(node_layout *layout, znode source, znode target);

/* might be a good optimization if we have this return where the next
   insertion should go if there is a next one, and have it optionally
   take a description of where to search.  Perhaps not v4.0 though. As
   written, this suffers from needing to search the node for where to
   create the new item.  */
  size_t           ( *create_item )( node_layout *layout, znode *znode, 
								flow *flow );

  size_t           ( *free_space )( node_layout *layout, znode *znode );

  /* searches within the node for the one item which might contain the
	 key, invoke item->search_within to search within that item to see
	 if it is in there */
  search_t         ( *search_within )( node_layout *layout, znode *znode, 
							  item *result_item, find_t border, item *result );

  /* used for iterating through all items in node */

  iter_t           ( *next_item )( node_layout *layout, znode *znode, u16 *item_pos, item *result );
};

extern node_layout *get_layout( znode *znode );

/** returns length of condensation corresponding to the item, in other
    words, the length of the item's data when uncompressed. */
extern size_t get_condensation_length( item *item );

}



struct item_header 
{
  __u16 data_start;
  __u16 data_length;
  key key;
}

/* for use in version 4.1 */
struct compressed_key_item_header 
{
  __u16 data_start;
  __u16 data_length;
				/* how many bytes to take from the
                                   previous key in this node, and
                                   prepend to this key.  This key
                                   consumes (KEY_LENGTH -
                                   inheritance_length) bytes of space
                                   to store. */
  unsigned char inheritance_length;
}
