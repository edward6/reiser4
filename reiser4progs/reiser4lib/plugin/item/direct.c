/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

direct_item_handler {
				/* put these in when the list of item operations becomes stable */
};


direct_left_merge(item_header item_header, node)
{
  if (first_item_in_node(item_header, node))
      return CANNOT_LEFT_MERGE;
  if (item_type(left_neighboring_item_in_node(item_header)))
      ;
}




#define pos_in_body_item(coord) ((coord)->in_item.pos_in_item.generic)



/** 
 * lookup method for item of body type.
 *
 * Return value is either pkb_coord_found or pkb_coord_notfound. Coord's
 * pos_in_item gets initialised in accordance with bias
 *
 */
lookup_result body_lookup (const reiser4_key * key /* key to look for inside
						    * item */, 
			   reiser4_tree_coord * coord /* znode and pos_in_node
						       * set an item to look
						       * through */,
			   lookup_bias bias /* specifies lookup
					     * behaviour. Either FIND_EXACT
					     * or */ )
{
	loff_t offset_in_item;


	assert ("vs", keys_of_one_file (key, coord->key));
	assert ("vs", get_key_offset (coord->key) >= get_item_key_offset (coord));

	offset_in_item = get_key_offset (coord->key) - get_item_key_offset (coord);
	if (offset < get_item_len (coord)) {
		pos_in_body_item (coord) = offset_in_item;
		return pkb_coord_found;
	}
		
	switch (bias) {
	case FIND_EXACT:
		coord->pos_in_node ++;
		pos_in_body_item (coord) = 0;
		break;

	case FIND_MAX_NOT_MORE_THAN:
		/* set position to last byte in the item */
		pos_in_body_item (coord) = get_item_len (coord) - 1;
		break;

	default:
		impossible ("vs", "unexpected lookup bias found");
		return pkb_IO_ERROR;
	}
		
	return pkb_coord_notfound;
}


convert_tail_to_extent ()
{
	/* what do we do with pages? */
}


/** 
 *    overwrite method for item of body type
 *
 *    this bh does not have mapping. If we do not overwrite whole block 
 */
static int body_overwrite (reiser4_transaction * trans /* current transaction */,
			   reiser4_tree_coord * coord /* is set to position
						       * where to start
						       * overwrite from */,
			   struct buffer_head * bh, int offset,
			   int count /* number of bytes to be overwritten */, 
			   int * error)
{
	assert ("vs", count <= blocksize);

	if (count == blocksize) {
		/* return number of bytes which can be written into this item */
		return ;
	}
	if ()
	
}


/** this has to read all 
 *
 *  Return value is number of bytes written or error code
 *
 */
static ssize_t body_append ( reiser4_tree_coord * coord, flow * flow /* user
								      * data
								      * to be
								      * written
								      * to a
								      * file */ )
{
}


/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
