/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

#include "../../defines.h"

/*
  leaf 40 format:
  
  [node header | item 0, item 1, .., item N-1 |  free space | item_head N-1, .. item_head 1, item head 0 ]
   plugin_id (16)                                                key
   free_space (16)                                               pluginid (16)
   free_space_start (16)                                         offset (16)
   level (8)
   num_items (16)
   magic (32)
   flush_time (32)
*/

/** magic number that is stored in ->magic field of node header */
const __u32 reiser4_node_magic = 0x52344653; /* (*(__u32 *)"R4FS"); */


/* header of node of reiser40 format is at the beginning of node */
static node_header_40 *node40_node_header( znode *node )
{
	assert( "nikita-567", node != NULL );
	assert( "nikita-568", znode_is_loaded( node ) );
	assert( "nikita-569", zdata( node ) != NULL );
	return ( node_header_40 * ) zdata( node );
}


/* functions to get/set fields of node_header_40 */
static void nh_40_set_magic (node_header_40 * nh, int value)
{
	cputod16 (value, &nh->magic);
}


static int nh_40_get_magic (node_header_40 * nh)
{
	return d16tocpu (&nh->magic);
}


static void nh_40_set_free_space (node_header_40 * nh, int value)
{
	cputod16 (value, &nh->free_space);
}


static int nh_40_get_free_space (node_header_40 * nh)
{
	return d16tocpu (&nh->free_space);
}



static void nh_40_set_free_space_start (node_header_40 * nh, int value)
{
	cputod16 (value, &nh->free_space_start);
}


static int nh_40_get_free_space_start (node_header_40 * nh)
{
	return d16tocpu (&nh->free_space_start);
}


static void nh_40_set_level (node_header_40 * nh, int value)
{
	cputod8 (value, &nh->level);
}


static int nh_40_get_level (node_header_40 * nh)
{
	return d8tocpu( &nh->level);
}


static void nh_40_set_num_items (node_header_40 * nh, int value)
{
	cputod16 (value, &nh->num_items);
}


static int nh_40_get_num_items (node_header_40 * nh)
{
	return d16tocpu( &nh->num_items);
}

/* plugin field of node header should be read/set by
   get_disk_plugin/save_disk_plugin */



/* array of item headers is at the end of node */
static item_header_40 *node40_ih_at( znode *node, int pos )
{
	return (item_header_40 *)(zdata( node ) + node->buffer->b_size ) - pos - 1;
}


/* functions to get/set fields of item_header_40 */
static void ih_40_set_offset (item_header_40 * ih, int offset)
{
	cputod16 (offset, &ih->offset);
}


static int ih_40_get_offset (item_header_40 * ih)
{
	return d16tocpu (&ih->offset);
}


/* plugin field of item header should be read/set by
   get_disk_plugin/save_disk_plugin */

static int node40_item_length (const tween_coord * coord)
{
	item_header_40 * ih;

	ih = node40_ih_at (coord->node, coord->item_pos);
	if (coord->item_pos == last_item_pos (coord->node)) {
		return nh_40_get_free_space_start( node40_node_header (coord -> node) ) -
			ih_40_get_offset( ih );
	} else {
		return ih_40_get_offset( ih - 1 ) - ih_40_get_offset( ih );
	}
}



/* plugin methods */

/* plugin->u.node.item_overhead
   look for description of this method in plugin/node/node.h
*/
size_t leaf40_item_overhead (znode * node, flow * flow)
{
	return sizeof (item_header_40);
}

/*
  int           ( *move_items )( znode *source, pos_in_node source_pos_in_node, znode *target, pos_in_node target_pos_in_node, int item_count );
  size_t           ( *create_body_space)( znode *node, int byte_count, pos_in_node pos );
  size_t           ( *create )( znode *node, pos_in_node pos, flow *a_flow );
*/

/* plugin->u.node.free_space
   look for description of this method in plugin/node/node.h
*/
size_t leaf40_free_space ( znode *node )
{
	assert( "nikita-577", node != NULL );
	assert( "nikita-578", znode_is_loaded( node ) );
	assert( "nikita-579", zdata( node ) != NULL );
	trace_stamp( trace_nodes );

	return nh_40_get_free_space (node40_node_header (node));
}


/* plugin->u.node.lookup
   look for description of this method in plugin/node/node.h
*/
node_search_result leaf40_lookup( znode *node, reiser4_key *key, lookup_bias bias,
				  tween_coord *point )
{
	item_header_40 *left;
	item_header_40 *right;
	int left_i;
	int right_i;
	reiser4_plugin *plugin;

	assert( "nikita-583", node != NULL );
	assert( "nikita-584", key != NULL );
	assert( "nikita-585", point != NULL );
	trace_stamp( trace_nodes );

	/* binary search for item that can contain given key */
	left_i = 0;
	right_i = last_item_pos( node ) - 1;
	left  = node40_ih_at( node, left_i );
	right = node40_ih_at( node, right_i );
	point -> node = node;
	while( left_i != right_i ) {
		int median_i;
		item_header_40 *median;
		
		median_i = ( left_i + right_i ) / 2;
		median = node40_ih_at( node, median_i );
		
		switch( keycmp( key, &median -> key ) ) {
		case less_than:
			right_i = median_i;
			right = median;
			continue;
		default: wrong_return_value( "nikita-586", "keycmp" );
		case greater_than:
			left_i = median_i;
			left = median;
			continue;
		case equal_to:
			left_i = median_i;
			left = median;
			break;
		}
	}
	point -> item_pos = left_i;
	/* key < leftmost key in a mode or node is corrupted and keys
	   are not sorted  */
	if( keycmp( &left -> key, key ) == greater_than ) {
		/* see reiser4.h for description of this */
		if( REISER4_EXACT_DELIMITING_KEY || ( left_i != 0 ) ) {
			/* screw up */
			warning( "nikita-587", 
				 "Key less than %i key in a node", left_i );
			print_key( "key", key );
			print_key( "min", &left -> key );
			print_node( "node", node );
			print_point( "point", point );
			return ns_ioerror;
		} else
			return ns_not_found;
	}
	/* left <= key, ok */
	plugin = get_disk_plugin( node -> tree -> tree,
				  reiser4_item_plugin_id, &left -> plugin_id );
	if( plugin == NULL ) {
		warning( "nikita-588", "Unknown plugin %i", 
			 d16tocpu( &left -> plugin_id ) );
		print_key( "key", key );
		print_node( "node", node );
		print_point( "point", point );
		return ns_ioerror;
	}
	if( plugin -> u.item.b.max_key_inside != NULL ) {
		reiser4_key max_item_key;

		/* key > max_item_key --- outside of an item */
		if( keycmp( key, plugin -> u.item.b.max_key_inside
			    ( point, &max_item_key ) ) == greater_than ) {
			++ point -> item_pos;
			return ns_not_found;
		}
	}
	if( plugin -> u.item.b.lookup != NULL )
		return plugin -> u.item.b.lookup( key, bias, point );
	else
		return ns_found;
}
#if 0
node_search_result leaf40_lookup( znode *node, reiser4_key *key, lookup_bias bias,
				  tween_coord *point )
{
	item_header_40 *left;
	item_header_40 *right;
	int left_i;
	int right_i;
	reiser4_plugin *plugin;

	assert( "nikita-583", node != NULL );
	assert( "nikita-584", key != NULL );
	assert( "nikita-585", point != NULL );
	trace_stamp( trace_nodes );

	/* binary search for item that can contain given key */
	left_i = 0;
	right_i = last_item_pos( node );
	left  = node40_ih_at( node, left_i );
	right = node40_ih_at( node, right_i );
	point -> node = node;
	while( left_i != right_i ) {
		int median_i;
		item_header_40 *median;
		
		median_i = ( left_i + right_i ) / 2;
		median = node40_ih_at( node, median_i );
		
		switch( keycmp( key, &median -> key ) ) {
		case less_than:
			right_i = median_i;
			right = median;
			continue;
		default: wrong_return_value( "nikita-586", "keycmp" );
		case greater_than:
			left_i = median_i;
			left = median;
			continue;
		case equal_to:
			left_i = median_i;
			left = median;
			break;
		}
	}
	point -> item_pos = left_i;
	/* key < leftmost key in a mode or node is corrupted and keys
	   are not sorted  */
	if( keycmp( &left -> key, key ) == greater_than ) {
		/* see reiser4.h for description of this */
		if( REISER4_EXACT_DELIMITING_KEY || ( left_i != 0 ) ) {
			/* screw up */
			warning( "nikita-587", 
				 "Key less than %i key in a node", left_i );
			print_key( "key", key );
			print_key( "min", &left -> key );
			print_node( "node", node );
			print_point( "point", point );
			return ns_ioerror;
		} else
			return ns_not_found;
	}
	/* left <= key, ok */
	plugin = get_disk_plugin( node -> tree -> tree,
				  reiser4_item_plugin_id, &left -> plugin_id );
	if( plugin == NULL ) {
		warning( "nikita-588", "Unknown plugin %i", 
			 d16tocpu( &left -> plugin_id ) );
		print_key( "key", key );
		print_node( "node", node );
		print_point( "point", point );
		return ns_ioerror;
	}
	point -> key = &left -> key;
	point -> plugin = plugin;
	if( plugin -> u.item.b.max_key_inside != NULL ) {
		reiser4_key max_item_key;

		/* key > max_item_key --- outside of an item */
		if( keycmp( key, plugin -> u.item.b.max_key_inside
			    ( point, &max_item_key ) ) == greater_than ) {
			++ point -> coords.in_node;
			point -> plugin = NULL;
			return ns_not_found;
		}
	}
	if( plugin -> u.item.b.lookup != NULL )
		return plugin -> u.item.b.lookup( key, bias, point );
	else
		return ns_found;
}
#endif

/* plugin->u.node.num_of_items
   look for description of this method in plugin/node/node.h
*/
int leaf40_num_of_items( znode *node )
{
	trace_stamp( trace_nodes );
	return nh_40_get_num_items (node40_node_header (node));
}


/* plugin->u.node.item_at
   look for description of this method in plugin/node/node.h
*/
int leaf40_item_at( const tween_coord *coord, reiser4_item_data *item )
{
	assert( "nikita-596", coord != NULL );

	if( coord_of_unit( coord ) ) {
		/* @point is set to existing item */
		item_header_40 *ih;

		ih = node40_ih_at( coord -> node, coord -> item_pos );
		item -> data = zdata( coord->node ) + ih_40_get_offset( ih );
		item -> plugin = get_disk_plugin (coord->node->tree->tree,
						  reiser4_item_plugin_id, &ih->plugin_id);
		/* length has to be calculated */
		item -> length = node40_item_length (coord);
		return 0;
	}
	return -ENOENT;
}


/* plugin->u.node.key_at
   look for description of this method in plugin/node/node.h
*/
reiser4_key *leaf40_key_at( const tween_coord *coord, reiser4_key *key )
{
	if( coord_of_unit( coord ) ) {
		/* @point is set to existing item */
		item_header_40 *ih;

		ih = node40_ih_at( coord -> node, coord -> item_pos );
		memcpy( key, &ih -> key, sizeof (reiser4_key) );
		return key;
	}
	return ERR_PTR( -ENOENT );
}


/* plugin->u.node.estimate
   look for description of this method in plugin/node/node.h
*/
size_t leaf40_estimate( znode *node )
{
	size_t result;

	assert( "nikita-597", node != NULL );
	assert( "nikita-598", node -> state & znode_synched );

	result = node -> free_space - sizeof( item_header_40 );
	return ( result > 0 ) ? result : 0;
}


/* plugin->u.node.check
   look for description of this method in plugin/node/node.h
*/
int leaf40_check( znode *node, char **error )
{
	assert( "nikita-580", node != NULL );
	assert( "nikita-581", error != NULL );
	trace_stamp( trace_nodes );

	if( !znode_is_loaded( node ) )
		return 0;

	assert( "nikita-582", zdata( node ) != NULL );
	if( leaf40_num_of_items( node ) < 0 ) {
		*error = ( char * ) "Negative number of items";
		return -1;
	}
	return 0;
}


/* plugin->u.node.parse
   look for description of this method in plugin/node/node.h
*/
static int leaf40_parse( const znode *node )
{
	node_header_40   *header;
	int               result;

	header = node40_node_header( node );
	result = -EIO;
	if( ( ( __u8 ) node -> level ) != nh_40_get_level( header ) )
		warning( "nikita-494", "Wrong level found in node: %i != %i",
			 node -> level, nh_40_get_level( header ) );
	else if( nh_40_get_magic( header ) != reiser4_node_magic )
		warning( "nikita-495", "Wrong magic in tree node: %x != %x",
			 reiser4_node_magic, nh_40_get_magic( header ) );
	else
		result = 0;
	if( result != 0 )
		print_node( "node", node );
	return result;
}


/* plugin->u.node.init
   look for description of this method in plugin/node/node.h
*/
int leaf40_init( znode *node )
{
	node_header_40 *header;

	assert( "nikita-570", node != NULL );
	assert( "nikita-571", znode_is_loaded( node ) );
	assert( "nikita-572", zdata( node ) != NULL );
	assert( "nikita-573", node_plugin_by_node( node ) != NULL );

	header = node40_node_header( node );
	if( REISER4_ZERO_NEW_NODE )
		memset( zdata( node ), 0, ( unsigned int ) znode_size( node ) );
	else
		memset( header, 0, sizeof (node_header_40) );
	nh_40_set_free_space (header, znode_size( node ) - sizeof (node_header_40));
	nh_40_set_free_space_start (header, sizeof (node_header_40));
	/* sane hypothesis: 0 in CPU format is 0 in disk format */
	/* items: 0 */
	save_plugin_id (node -> node_plugin, &header -> plugin_id);
	nh_40_set_level (header, node -> level);

	/* flags: 0 */
	/*cputod32( reiser4_node_magic, &header -> magic );*/
	return 0;
}


int leaf40_guess( const znode *node )
{
	assert( "nikita-1058", node != NULL );
	return nh_40_get_magic( node40_node_header( node ) ) == 
		reiser4_node_magic;
}
/*
  int ( *guess )( const znode *node );
  void ( *print )( znode *node );
*/

/* plugin->u.node.chage_item_size
   look for description of this method in plugin/node/node.h
*/
void leaf40_change_item_size (tween_coord * coord, int by)
{
	node_header_40 * nh;
	item_header_40 * ih;
	reiser4_item_data item;
	int i;


	/* make sure that @item is coord of existing item */
	assert ("vs-210", coord_of_unit (coord));

	nh = node40_node_header (coord->node);

	leaf40_item_at (coord, &item);

	/* move item bodies */
	ih = node40_ih_at (coord->node, coord->item_pos);
	memmove (item.data + item.length + by, item.data + item.length,
		 nh_40_get_free_space_start (node40_node_header (coord->node)) -
		 (ih_40_get_offset (ih) + item.length));

	/* update offsets of moved items */
	for (i = coord->item_pos + 1; i < nh_40_get_num_items (nh); i ++) {
		ih = node40_ih_at (coord->node, i);
		ih_40_set_offset (ih, ih_40_get_offset (ih) + by);
	}

	/* update node header */
	nh_40_set_free_space (nh, nh_40_get_free_space (nh) - by);
	nh_40_set_free_space_start (nh, nh_40_get_free_space (nh) - by);
}


/* plugin->u.node.create_new_item
   look for description of this method in plugin/node/node.h
*/
int leaf40_create_new_item (tween_coord * target, reiser4_key * key,
			    reiser4_item_data * data, carry_level * todo)
{
	node_header_40 * nh;
	item_header_40 * ih;
	int offset;
	int i;

	nh = node40_node_header (target->node);

	assert ("vs-211", coord_correct (target));
	assert ("vs-212", coord_between_items (target));

	coord_set_to_right (target);

	if (target->item_pos < nh_40_get_num_items (nh)) {
		/* there are items to be moved to prepare space for new
		   item */
		ih = node40_ih_at (target->node, target->item_pos);
		offset = ih_40_get_offset (ih);

		memmove (zdata (target->node) + offset + data->length,
			 zdata (target->node) + offset,
			 nh_40_get_free_space_start (nh) - offset);
		/* update headers of moved items */
		for (i = target->item_pos; i < nh_40_get_num_items (nh); i ++) {
			ih = node40_ih_at (target->node, i);
			ih_40_set_offset (ih, ih_40_get_offset (ih) + data->length);
		}

		/* move item headers */
		memmove (ih - (nh_40_get_num_items (nh) - target->item_pos),
			 ih - (nh_40_get_num_items (nh) - target->item_pos - 1),
			 sizeof (item_header_40) * (nh_40_get_num_items (nh) - target->item_pos));
	} else {
		offset = nh_40_get_free_space_start (nh);
	}

	/* make item header */
	ih = node40_ih_at (target->node, target->item_pos);
	memcpy (&ih->key, key, sizeof (reiser4_key));
	ih_40_set_offset (ih, offset);
	save_plugin_id (data->plugin, &ih->plugin_id);

	/* copy item body */
	memcpy (zdata (target->node) + offset, data->data, data->length);

	/* update node header */
	nh_40_set_free_space (nh, nh_40_get_free_space (nh) - data->length);
	nh_40_set_free_space_start (nh, nh_40_get_free_space_start (nh) + data->length);
	nh_40_set_num_items (nh, nh_40_get_num_items (nh) + 1);

	if (target->item_pos == 0) {
		/* left delimiting key has to be updated */
		carry_node * cn;
		carry_op * op;
		; /* delimiting key updating usually requires to know both
		     delimited nodes. Here we know only one of those */
	}
	return 0;
}


/* plugin->u.node.create_new_item
   look for description of this method in plugin/node/node.h
*/
void leaf40_update_item_key (tween_coord * target, reiser4_key * key, carry_level * todo)
{
	item_header_40 * ih;
	
	ih = node40_ih_at (target->node, target->item_pos);
	memcpy (&ih->key, key, sizeof (reiser4_key));
}


/* how many units are there in @source starting from source->in_item.unit_pos
   but not further than @stop_point */
static int wanted_units (tween_coord * source, tween_coord * stop_point,
			 append_prepend pend)
{
	if (source->item_pos != stop_point->item_pos)
		/* @source item is not stop item, so we want all its units
		   shifted */
		return last_unit_pos (source) + 1;

	if (pend == append) {
		assert ("vs-181", source->in_item.unit_pos == 0);
		return stop_point->in_item.unit_pos + 1;
	} else {
		assert ("vs-182", source->in_item.unit_pos == last_unit_pos (stop_point));
		return source->in_item.unit_pos - stop_point->in_item.unit_pos + 1;
	}
}


/* this is auxiliary function used by both cutting methods - cut and
   cut_and_kill. If it is called by cut_and_kill (@cut == 0) special action
   (kill_hook) will be performed on every unit being removed from tree */
static int cut_or_kill (tween_coord * from, tween_coord * to,
			carry_level * todo, int cut)
{
	node_header_40 * nh;
	int want;
	int from_free; /* how many bytes were freed cutting item @from */
	int to_free; /* .. from item @to */
	item_header_40 * ih;
	int new_from_end;
	int new_to_start;
	int first_removed; /* position of first item removed entirely */
	int count; /* number of items removed entirely */
	int i;


	assert ("vs-184", from->node == to->node);

	nh = node40_node_header (from->node);

	first_removed = from->item_pos;
	count = to->item_pos - from->item_pos + 1;

	from_free = 0;
	to_free = 0;
	if (from->in_item.unit_pos != 0) {
		/* @from item has to be cut partially */
		want = wanted_units (from, to, append);
		/* remove @want units starting from @from->in_item.unit_pos
		   one of item @from and move freed space to the right */
		if (cut)
			from_free = item_plugin_by_coord (from)->u.item.b.remove_units (from, from->in_item.unit_pos, want, append);
		else
			from_free = item_plugin_by_coord (from)->u.item.b.kill_units (from, from->in_item.unit_pos, want, append);
			
		first_removed ++;
		count --;
	}

	if (to->in_item.unit_pos != last_unit_pos (to)) {
		/* @to item has to be cut partially */
		if (from->item_pos != to->item_pos || from->in_item.unit_pos == 0) {
			/* @to item is cut partially and it is different item
			   than @from. remove units from the beginning of @to
			   and move free space to the left */
			if (cut)
				to_free = item_plugin_by_coord (from)->u.item.b.remove_units (from, 0, to->in_item.unit_pos + 1, prepend);
			else
				to_free = item_plugin_by_coord (from)->u.item.b.kill_units (from, 0, to->in_item.unit_pos + 1, prepend);
			count --;
		} else {
			/* it has been cut already as @from and @to are set to
			   the same item and as @from was cut already */
		}
	}

	if (!cut) {
		/* for every item being removed entirely call special method */
		tween_coord tmp;

		tmp = *from;
		for (i = 0; i < count; i ++) {
			tmp.item_pos = first_removed + i;
			if (item_plugin_by_coord (from)->u.item.b.kill_item_hook)
				item_plugin_by_coord (from)->u.item.b.kill_item_hook (&tmp);
		}
	}


	/* where does @from item end now */
	ih = node40_ih_at (from->node, from->item_pos);
	new_from_end = ih_40_get_offset (ih) + node40_item_length (from) - from_free;

	/* where does @to start now */
	ih = node40_ih_at (from->node, to->item_pos);
	if (to->in_item.unit_pos == last_unit_pos (to)) {
		if (to->item_pos == last_item_pos (to->node))
			new_to_start = nh_40_get_free_space_start (nh);
		else
			new_to_start = ih_40_get_offset (ih - 1);
	} else
		new_to_start = ih_40_get_offset (ih) + to_free;

	/* move remaining data to left */
	memmove (from->node->data + new_from_end,
		 from->node->data + new_to_start,
		 nh_40_get_free_space_start (nh) - new_to_start);

	/* update their item headers */
	i = first_removed + count;
	ih = node40_ih_at (from->node, i);
	for (; i <= last_item_pos (to->node); i ++, ih --) {
		ih_40_set_offset (ih,
				  ih_40_get_offset (ih) - (new_to_start - new_from_end));
	}

	/* cut item headers of removed items */
	ih = node40_ih_at (from->node, first_removed) + 1;
	memmove (ih, ih - count, sizeof (item_header_40) * (last_item_pos (to->node) + 1 -
							    count - first_removed));
	
	/* update node header */
	nh_40_set_num_items (nh, last_item_pos (to->node) + 1 - count);
	nh_40_set_free_space_start (nh, nh_40_get_free_space_start (nh) - (new_to_start - new_from_end));
	nh_40_set_free_space (nh, nh_40_get_free_space (nh) +
			      ((new_to_start - new_from_end) + sizeof (item_header_40) * count));
	
	if (todo) {
		if (from->item_pos == 0 && from->in_item.unit_pos == 0) {
			/* left delimiting key has to be updated */
			carry_node * cn;
			carry_op * op;
			; /* delimiting key updating usually requires to know both
			     delimited nodes. Here we know only one of those */
		}
	}

	return count;
}


/* plugin->u.node.cut_and_kill
   look for description of this method in plugin/node/node.h
*/
int leaf40_cut_and_kill (tween_coord * from, tween_coord * to, carry_level * todo)
{
	return cut_or_kill (from, to, todo, 0);
}


/* plugin->u.node.cut
   look for description of this method in plugin/node/node.h
*/
int leaf40_cut (tween_coord * from, tween_coord * to, carry_level * todo)
{
	return cut_or_kill (from, to, todo, 1);
}


/* this structure is used by shift method of node plugin */
struct shift_params {
	append_prepend pend; /* when @pend == append - we are shifting
				to left, when @pend == prepend - to
				right */
	tween_coord wish_stop; /* when shifting to left this is last unit we
				  want shifted, when shifting to right - this
				  is set to unit we want to start shifting
				  from */
	znode * target;

	/* these are set by estimate_shift */
	tween_coord real_stop; /* this will be set to last unit which will be
				  really shifted */
	int merging_units; /* number of units of first item which have to be
			      merged with last item of target node */
	int merging_bytes; /* number of bytes in those units */

	int entire; /* items shifted in their entirety */
	int entire_bytes; /* number of bytes in those items */

	int part_units; /* number of units of partially copied item */
	int part_bytes; /* number of bytes in those units */

	int shift_bytes; /* total number of bytes in items shifted
			    (item headers not included) */
	
};


static int item_creation_overhead (tween_coord * item)
{
	return node_plugin_by_coord (item) -> u.node.item_overhead (item->node, 0);
}




/* this calculates what can be copied from @shift->wish_stop.node to
   @shift->target */
static void leaf40_estimate_shift (struct shift_params * shift)
{
	int target_free_space, size;
	int stop_item; /* item which estimating should not consider */
	int want; /* number of units of item we want shifted */
	tween_coord source; /* item being estimated */
	reiser4_item_plugin * ip;


	/* shifting to left/right starts from first/last units of
	   @shift->wish_stop.node */
	source.node = shift->wish_stop.node;
	(shift->pend == append) ? coord_first_unit (&source) : 
		coord_last_unit (&source);


	/* free space in target node and number of items in source */
	target_free_space = znode_free_space (shift->target);


	if (!is_empty_node (shift->target)) {
		/* target node is not empty, check for boundary items
		   mergeability */
		tween_coord to;

		/* item we try to merge @source with */
		to.node = shift->target;
		(shift->pend == append) ? coord_last_unit (&to) : 
			coord_first_unit (&to);

		if (are_items_mergeable (&source, &to)) {
			/* how many units of @source do we want to merge to
			   item @to */
			want = wanted_units (&source, &shift->wish_stop, shift->pend);

			/* how many units of @source we can merge to item
			   @to */
			ip = &item_plugin_by_coord (&source)->u.item;
			shift->merging_units = ip->b.can_shift (target_free_space, &source,
								shift->target, shift->pend, &size,
								want);
			shift->merging_bytes = size;
			shift->shift_bytes += size;
			/* update stop point to be set to last unit of @source
			   we can merge to @target */
			shift->real_stop = source;
			shift->real_stop.in_item.unit_pos = (shift->merging_units - source.in_item.unit_pos - 1) * shift->pend;

			if (shift->merging_units != want) {
				/* we could not copy as many as we want, so,
				   there is no reason for estimating any
				   longer */
				return;
			}

			target_free_space -= size;
			source.item_pos += shift->pend;
		}
	}


	/* number of item nothing of which we want to shift */
	stop_item = shift->wish_stop.item_pos + shift->pend;

	/* calculate how many items can be copied into given free
	   space as whole */
	for (; source.item_pos != stop_item; source.item_pos += shift->pend) {
		if (shift->pend == prepend)
			source.in_item.unit_pos = last_unit_pos (&source);

		/* how many units of @source do we want to copy */
		want = wanted_units (&source, &shift->wish_stop, shift->pend);

		if (want == last_unit_pos (&source) + 1) {
			/* we want this item to be copied entirely */
			size = item_length_by_coord (&source) + item_creation_overhead (&source);
			if (size <= target_free_space) {
				/* item fits into target node as whole */
				target_free_space -= size;
				shift->shift_bytes += size;
				shift->entire_bytes = size;
				shift->entire ++;
				
				/* update shift->real_stop point to be set to
				   last unit of @source we can merge to
				   @target */
				(shift->pend == append) ? coord_last_unit (&shift->real_stop) : 
					coord_first_unit (&shift->real_stop);
				continue;
			}
		}

		/* we reach here only for an item which does not fit into
		   target node in its entirety. This item may be either
		   partially shifted, or not shifted at all. We will have to
		   create new item in target node, so decrease amout of free
		   space by an item creation overhead. We can reach here also
		   if stop point is in this item */
		target_free_space -= item_creation_overhead (&source);
		ip = &item_plugin_by_coord (&source)->u.item;
		shift->part_units = ip->b.can_shift (target_free_space, &source,
						     0/*target*/, shift->pend, &size,
						     want);
		shift->part_bytes = size;
		shift->shift_bytes += size;

		/* update stop point to be set to last unit of @source we can
		   merge to @target */
		shift->real_stop = source;
		shift->real_stop.in_item.unit_pos = (shift->part_units - source.in_item.unit_pos - 1) * shift->pend;
		break;
	}
}


/* copy part of @shift->real_stop.node starting either from its beginning or
   from its end and ending at @shift->real_stop to either the end or the
   beginning of @shift->target */
void leaf40_copy (struct shift_params * shift)
{
	node_header_40 * nh;
	tween_coord from;
	tween_coord to;
	item_header_40 * from_ih, * to_ih;
	int free_space_start;
	int new_items;
	int old_items;
	int old_offset;
	int i;


	nh = node40_node_header (shift->target);
	free_space_start = nh_40_get_free_space_start (nh);
	old_items = nh_40_get_num_items (nh);
	new_items = shift->entire + shift->part_units ? 1 : 0;
	assert ("vs-185",
		shift->shift_bytes ==
		shift->merging_bytes + shift->entire_bytes + shift->part_bytes);

	from = shift->wish_stop;

	to.node = shift->target;
	if (shift->pend == append) {
		/* copying to left */

		from.item_pos = 0;
		from_ih = node40_ih_at (from.node, 0);

		to.item_pos = last_item_pos (to.node);
		if (shift->merging_units) {
			/* appending last item of @target */
			item_plugin_by_coord (&from)->u.item.b.pend_units (&to, &from,
									   0, 
									   shift->merging_units,
									   append, shift->merging_bytes);
			from.item_pos ++;
			from_ih --;
			to.item_pos ++;
		}

		to_ih = node40_ih_at (shift->target, old_items);
		if (shift->entire) {
			/* copy @entire items entirely */

			/* copy item headers */
			memcpy (to_ih - shift->entire + 1,
				from_ih - shift->entire + 1,
				shift->entire * sizeof (item_header_40));
			/* update item header offset */
			old_offset = ih_40_get_offset (from_ih);
			for (i = 0; i < shift->entire; i ++, to_ih --, from_ih --)
				ih_40_set_offset (to_ih,
						  ih_40_get_offset (from_ih) - old_offset + free_space_start);

			/* copy item bodies */
			memcpy (shift->target->data + free_space_start + shift->merging_bytes,
				from.node->data + ih_40_get_offset (from_ih),
				shift->entire_bytes);

			from.item_pos -= shift->entire;
			to.item_pos += shift->entire;
		}

		if (shift->part_units) {
			/* copy heading part (@part units) of @source item as
			   a new item into @target->node */

			/* copy item header of partially copied item */
			memcpy (to_ih, from_ih, sizeof (item_header_40));
			ih_40_set_offset (to_ih, ih_40_get_offset (from_ih) - old_offset + free_space_start);
			item_plugin_by_coord (&to)->u.item.b.pend_units (&to, &from, 0, shift->part_units,
									 append, shift->part_bytes);
		}
	} else {
		/* copying to right */

		from.item_pos = last_item_pos (from.node);
		from_ih = node40_ih_at (from.node, from.item_pos);
		
		/* prepare space for new items */
		memmove (to.node->data + sizeof (node_header_40) + shift->shift_bytes,
			 to.node->data + sizeof (node_header_40),
			 free_space_start - sizeof (node_header_40));
		/* update item headers of moved items */
		to_ih = node40_ih_at (to.node, 0);
		for (i = 0; i < old_items; i ++)
			ih_40_set_offset (to_ih + i,
					  ih_40_get_offset (to_ih + i) + shift->shift_bytes);
		/* first item gets @merging_bytes longer. free space appears
		   at its beginning */
		ih_40_set_offset (to_ih,
				  ih_40_get_offset (to_ih) - shift->merging_bytes);

		/* move item headers to make space for new items */
		memmove (to_ih - old_items + 1 - new_items, to_ih - old_items + 1,
			 sizeof (item_header_40) * old_items);
		to_ih -= (new_items - 1);

		if (shift->merging_units) {
			/* prepend first item of @to */
			item_plugin_by_coord (&to)->u.item.b.pend_units (&to, &from,
									 last_unit_pos (&from) - shift->merging_units + 1,
									 shift->merging_units,
									 prepend, shift->merging_bytes);
			from.item_pos --;
			from_ih ++;
		}

		if (shift->entire) {
			/* copy @entire items entirely */

			/* copy item headers */
			memcpy (to_ih, from_ih, shift->entire * sizeof (item_header_40));

			/* update item header offset */
			old_offset = ih_40_get_offset (from_ih);
			for (i = 0; i < shift->entire; i ++, to_ih ++, from_ih ++)
				ih_40_set_offset (to_ih,
						  ih_40_get_offset (from_ih) - old_offset +
						  sizeof (node_header_40) + shift->part_bytes);
			/* copy item bodies */
			memcpy (to.node->data + sizeof (node_header_40) + shift->part_bytes,
				item_body_by_coord (&from), shift->entire_bytes);

			from.item_pos -= shift->entire;
		}

		if (shift->part_units) {
			/* copy heading part (@part units) of @source item as
			   a new item into @target->node */
			
			/* copy item header of partially copied item */
			memcpy (to_ih, from_ih, sizeof (item_header_40));
			ih_40_set_offset (to_ih, sizeof (node_header_40));
			item_plugin_by_coord (&to)->u.item.b.pend_units (&to, &from,
									 last_unit_pos (&from) - shift->part_units + 1,
									 shift->part_units, 
									 prepend, shift->part_bytes);
		}
	}

	/* update node header */
	nh_40_set_num_items (nh, old_items + new_items);
	nh_40_set_free_space_start (nh, free_space_start + shift->shift_bytes);
	nh_40_set_free_space (nh, nh_40_get_free_space (nh) -
			      (shift->shift_bytes + sizeof (item_header_40) * new_items));
	assert ("vs-170", nh_40_get_free_space (nh) >= 0);
}


/* remove everything either before or after @fact_stop. Number of items
   removed completely is returned */
static int leaf40_delete_copied (struct shift_params * shift)
{
	tween_coord from;
	tween_coord to;


	if (shift->pend == append) {
		/* we were shifting to left, remove everything from the
		   beginning of @shift->wish_stop->node upto
		   @shift->wish_stop */
		from.node = shift->real_stop.node;
		from.item_pos = 0;
		from.in_item.unit_pos = 0;
		from.between = at_unit;
		to = shift->real_stop;
	} else {
		/* we were shifting to right, remove everything from
		   @shift->stop_point upto to end of
		   @shift->stop_point->node */
		from = shift->real_stop;
		to.node = from.node;
		to.item_pos = last_item_pos (to.node);
		to.in_item.unit_pos = last_unit_pos (&to);
		to.between = at_unit;
	}

	return leaf40_cut (&from, &to, 0);
}


/* znode has left and right delimiting keys. We moved data between nodes,
   therefore we must update delimiting keys of those znodes */
static void update_znode_dkeys (znode * left, znode * right)
{
	reiser4_key key;
	tween_coord first_item;


	if (!is_empty_node (left) && !is_empty_node (right)) {
		first_item.node = right;
		first_item.item_pos = 0;
		item_key_by_coord (&first_item, &key);
		
		/* update right delimiting key of @left */
		*znode_get_rd_key (left) = key;
		/* update left delimiting key of @right */
		*znode_get_ld_key (right) = key;
		return;
	}

	if (is_empty_node (left)) {
		assert ("vs-186", !is_empty_node (right));

		/* update right delimiting key of @left */
		*znode_get_rd_key (left) = *znode_get_ld_key (left);

		/* update left delimiting key of @right */
		*znode_get_ld_key (right) = *znode_get_ld_key (left);
		return;
	}

	if (is_empty_node (right)) {
		assert ("vs-187", !is_empty_node (left));

		/* update right delimiting key of @left */
		*znode_get_rd_key (left) = *znode_get_rd_key (right);

		/* update left delimiting key of @right */
		*znode_get_ld_key (right) = *znode_get_rd_key (right);
		return;
	}
	impossible ("vs-188", "both nodes can not be empty");
}


static int prepare_for_update (znode * left, znode * right, carry_level * todo)
{
#if 0 /* will change */
	carry_node * cn;
	carry_op * op;


	cn = reiser4_add_carry (&todo->nodes, poolo_last, 0);
	if (IS_ERR (cn))
		return PTR_ERR (cn);

	cn->node = left;
	cn->parent = 1;
	cn->lock = znode_write_lock;

	op = reiser4_add_cop (todo, poolo_last, 0);
	if (IS_ERR (op))
		return PTR_ERR (op);

	op->node = cn;
	op->op = cop_update;
	/*op->u.update.left = left;*/
	op->u.update.right = right;
#endif
	return 0;
}


/* when we have shifted everything from @empty - we have to update
   delimiting key first and then cut the pointer */
static int prepare_for_removal (znode * empty, znode * node,
				append_prepend pend,
				carry_level * todo)
{
#if 0
	carry_node * cn;
	carry_op * op;


	if (pend == prepend) {
		/* we shifted everything to right */

		/* @empty was left neighbor of @node, we are going to delete
		   pointer to it, that will also remove left delimiting key of
		   @empty node, so we have to save that key first by updating
		   delimiting keys between @empty and @node */
		cn = reiser4_add_carry (&todo->nodes, cno_last, 0);
		if (IS_ERR (cn))
			return PTR_ERR (cn);
		cn->node = empty;
		cn->parent = 1;
		cn->lock = znode_write_lock;

		op = reiser4_add_cop (todo, copo_last, 0);
		if (IS_ERR (op))
			return PTR_ERR (op);
		op->op = cop_update;
		op->node = empty;
		/*op->u.update.left = empty;*/
		op->u.update.right = node;
		op->u.update.key = *znode_get_ld_key (empty);
	}

	/* prepare for deletion of pointer to @empty node from its parent */
	cn = reiser4_add_carry (&todo->nodes, cno_last, 0);
	if (IS_ERR (cn))
		return PTR_ERR (cn);
	cn->node = empty;
	cn->parent = 1;
	cn->lock = znode_write_lock;

	op = reiser4_add_cop (todo, copo_last, 0);
	if (IS_ERR (op))
		return PTR_ERR (op);

	op->op = cop_delete;
	op->u.delete.child = empty;
#endif
	return 0;
}


/* something were shifted from @insert_point->node to @shift->target, update
   @insert_coord correspondingly */
static void adjust_coord (tween_coord * insert_point, 
			  struct shift_params * shift,
			  int removed, int including_insert_point)
{
	if (shift->pend == prepend) {
		/* there was shifting to right */
		if (compare_coords (&shift->real_stop, &shift->wish_stop) == same) {
			/* everything wanted was shifted */
			if (including_insert_point) {
				/* @insert_point is set before first unit of
				   @to node */
				insert_point->node = shift->target;
				coord_first_unit (insert_point);
				insert_point->between = before_unit;
			} else {
				/* @insert_point is set after last unit of
				   @insert->node */
				coord_last_unit (insert_point);
				insert_point->between = after_unit;
			}
		}
		return;
	}

	/* there was shifting to left */
	if (compare_coords (&shift->real_stop, &shift->wish_stop) == same) {
		/* everything wanted was shifted */
		if (including_insert_point) {
			/* @insert_point is set after last unit in @to node */
			insert_point->node = shift->target;
			coord_last_unit (insert_point);
			insert_point->between = after_unit;
		} else {
			/* @insert_point is set before first unit in the same
			   node */
			coord_first_unit (insert_point);
			insert_point->between = before_unit;
		}
		return;
	}

	if (!removed) {
		/* no items were shifted entirely */
		assert ("vs-195", shift->merging_units == 0 ||
			shift->part_units == 0);
		if (shift->real_stop.item_pos == insert_point->item_pos) {
			if (shift->merging_units)
				insert_point->in_item.unit_pos -= shift->merging_units;
			else
				insert_point->in_item.unit_pos -= shift->part_units;
		}
		return;
	}
	if (shift->real_stop.item_pos == insert_point->item_pos)
		insert_point->in_item.unit_pos -= shift->part_units;
	insert_point->item_pos -= removed;
}


/* plugin->u.node.shift
   look for description of this method in plugin/node/node.h
 */
int leaf40_shift (tween_coord * from, znode * to, carry_level * todo,
		  append_prepend pend,
		  int delete_child, /* if @from->node becomes empty - it will
				       be deleted from the tree if this is set
				       to 1 */
		  int including_stop_point /* */)
{
	struct shift_params shift;
	int result;


	memset (&shift, 0, sizeof (shift));
	shift.pend = pend;
	shift.wish_stop = *from;
	shift.target = to;


	/* set @shift.wish_stop to rightmost/leftmost unit among units we want
	   shifted */
	result = ((pend == append) ? coord_set_to_left (&shift.wish_stop) :
		  coord_set_to_right (&shift.wish_stop));
	if (result)
		/* there is nothing to shift */
		return 0;


	/* when first node plugin with item body compression is implemented,
	   this must be changed to call node specific plugin */

	/* shift->stop_point is updated to last unit which really will be
	   shifted */
	leaf40_estimate_shift (&shift);

	leaf40_copy (&shift);

	result = leaf40_delete_copied (&shift);

	/* adjust @from pointer in accordance with @including_stop_point flag
	   and amount of data which was really shifted */
	adjust_coord (from, &shift, result, including_stop_point);

	/* update delimiting key of znodes */
	update_znode_dkeys (pend == append ? to : from->node,
			    pend == append ? from->node : to);

	/* provide info for tree_carry to continue */
	if (is_empty_node (from->node) && delete_child) {
		/* all contents of @from->node is moved to @to and @from->node
		   has to be removed from the tree, so, on higher level we
		   will be removing the pointer to node @to */
		result = prepare_for_removal (from->node, to, pend, todo);
	} else {
		/* put data into @todo for delimiting key updating */
		result = prepare_for_update (pend == append ? to : from->node,
					     pend == append ? from->node : to,
					     todo);
	}
	printf ("SHIFT: merging %d, entire %d, part %d, size %d\n",
		shift.merging_units, shift.entire, shift.part_units, shift.shift_bytes);
	return result ? result : shift.shift_bytes;
}


/* plugin->u.node.modify
   look for description of this method in plugin/node/node.h
*/
int modify (znode * parent, znode * child, __u32 mod_flag, carry_level * todo)
{
	return 0;
}



#if 0
/* Shifting has three stages.

Shift item closest to target that needs merging.  Shift all items that
can be shifted in their entirety without alteration in one memory
copy.  Partially shift last item that needs splitting.

Calculating shifting must be done by the item plugin for partial and
merging shifts, and is done by the node plugin for whole item shifts.

*/


shift_leaf40(int shift_left,	/*  */
	     int include_insertion_point, /*  */
	     insert_point insert_point /*  */
	     )
{
	/* proper node locking is needed */
	int items_want_shift = 0;

	if (shift_left)
		{
		  shiftee = 0;
		  lzn = get_left_neighbor(czn);
		  l_free_space = leaf40_free_space(lzn);
		  if(!l_free_space)
		    return;

		  num_items_merge_shifted = merge and shift first item if it is mergeable;

		  /* calculate how many items we want to shift,
		     remember that the first item has
		     pos_in_node of 0 */
		  items_want_shift = insert_point.coords.pos_in_node;
		  if (include_insertion_point)
		    items_want_shift++;
			
		  /* calculate space cost to shift each
		     item starting from the leftmost,
		     and continuing until we exceed the
		     free space available in the left
		     node */
		  space_left_in_left = l_free_space;
		  num_items_can_wholly_shift = 0;
		  while ( (space_left_in_left > 0) && (num_items_can_wholly_shift < items_want_shift))
		    {
		      space_left_in_left -= LEAF40_ITEM_HEAD_SIZE + size_by_pos_in_node(node, num_items_can_wholly_shift + num_items_merge_shifted);
		      num_items_can_wholly_shift++;
		    }
		  num_items_shifted = num_items_merge_shifted + num_items_can_wholly_shift;
		  if (num_items_can_wholly_shift)
		    {
		      shift_leaf40_headers(czn, lzn, to_left, num_items_merge_shifted, /* merge_shifted item's header if any doesn't need shifting  */
					   num_items_can_wholly_shift);
		      shift_leaf40_bodies(czn, lzn, to_left, num_items_merge_shifted, /* merge_shifted body if any doesn't need shifting  */
					  num_items_can_wholly_shift);
		    }
		  /* partially shift last item */
		  /* when we write the directory code, modify
		     this to worry about case where insertion
		     point is in the middle of an item */
		  if(l_free_space)
		    {
		      last_item_header = leaf40_item_header_by_pos(zn, num_items_shifted );
		      num_items_shifted += plugin_by_plugin_id(last_item_header->plugin_id)->item_body_shift(zn, lzn, num_items_shifted, left, 
											max_num_units_to_shift /* if 0 then no maximimum 
														  exists, shift as much 
														  as we can */
											target_body_offset, 
											source_body_offset, 
											source_body_length, 
											initial_source_item_key, final_source_item_key
											);
		      properly update keys;
		    }
		}
	else			/* right shifting, this code highly unfinished */
		{	
			num_items_merge_shifted = 0;
		  
			shiftee = num_items(czn);
			rzn = get_right_neighbor(czn);
			r_free_space = leaf40_free_space(rzn);
			if(!r_free_space)
				return;

			/* calculate how many items we want to shift,
			   remember that the first item has
			   pos_in_node of 0 */
			items_want_shift = num_items(zn) - insert_point.coords.pos_in_node;
			if (include_insertion_point) /* never happens? */
				items_want_shift++;
			shift_amount = 0;
			/* estimate first item if mergeable */
			if(is_mergeable())
				{

					/* we must estimate before we shift if we are to avoid shifting three times */
					shift_amount = estimate_merge_items(source_item_offset, 
									    target_item_offset, 
									    current_target_end, 

									    /* the amount we shift is the minimum of max_num_units_to_shift and target_space_available */

									    target_space_available, /* remember to subtract any new item header from the target node's free_space */
									    max_num_units_to_shift /* if 0 then no maximimum exists, shift as much as we can */

									    left, /* 1 if shifting to left, 0 if to right */
									    node_offset source_body_end, 
									    key * initial_source_item_key );
					shiftee--;
					num_items_merge_shiftable++;	
			
				}
			/* estimate unmerging unsplitting items */
			/* calculate space cost to shift each
			   item starting from the leftmost,
			   and continuing until we exceed the
			   free space available in the left
			   node */
			space_left_in_right = r_free_space;
			num_items_can_wholly_shift = 0;
			/* instruction saving is possible in loop below? */
			while ( (space_left_in_right > 0) && ((num_items_can_wholly_shift + num_items_merge_shiftable) < items_want_shift))
				{
					/* logic is wrong/incomplete finish it */
					space_
					space_left_in_right -= LEAF40_ITEM_HEAD_SIZE + size_by_pos_in_node(node,shiftee);
					shiftee--;
					num_items_can_wholly_shift++;
				}
	    	
			/* estimate last item if it was not shifted as a whole */
			/* we must estimate before we shift if we are to avoid shifting three times */
			shift_amount = estimate_merge_items(source_item_offset, 
							    target_item_offset, 
							    current_target_end, 

							    /* the amount we shift is the minimum of max_num_units_to_shift and target_space_available */

							    target_space_available, /* remember to subtract any new item header from the target node's free_space */
							    max_num_units_to_shift /* if 0 then no maximimum exists, shift as much as we can */

							    left, /* 1 if shifting to left, 0 if to right */
							    node_offset source_body_end, 
							    key * initial_source_item_key );
		}
	shift remaining_source_items;
	shift_remaining_source_item_headers;
	increment number of items in target;
	decrement number of items in source;
	update delimiting key;

}


static int leaf40_is_empty (znode * node)
{
	return leaf40_num_of_items (node) == 0;
}


/* shift an item into another node, determining if we should merge with an item already there */

/* this copies source item (or its part) to target_node. It
   does not remove source_item or its parts. If source_item and target_item
   are mergeable with each other - units of source_item get appended to
   target_item. If not - new item gets created in target_node and units of
   source_item get appended to it. Number of units is as many as possible.
   No return value */

/* ??? should this work when target is empty
   yes
   ??? do we have item_header or (key, plugin, offset)
   no
   ??? can number and types of parameters of this and involved functions be changed 
   yes
*/


/* node must contain enough free sapce for new item if this length */
insert_new_item (length, insert_coord * point)
{
}


/* why do I think that this is not the best way to go? 

   Because this does a lot of job which is not specific to leaf nodes: it
   estimates, creates new items, etc.  Instead we could have a
   leaf40_create_item, leaf40_append_item, etc and have them called from
   shift_items from upper level. Otherwise we will end up with having similar
   functions for every level

   CHANGES: I changed it to check whether it is possible to copy anything into target before creating new item in it.
*/

void copy_merge_leaf40_item (insert_coord * source, insert_coord * target,
			     int units_to_copy, /* if 0 - copy as many as possible */
			     int left /* shift left = 1, shift right = 0 */
			     )
{
	char * source_item_offset; /* offset or item ? */
	char * target_item_offset;
	node_offset current_target_end = 0;


	/* the amount we shift is the minimum of max_num_units_to_shift and target_space_available */

	size_t target_space_available; /* remember to subtract any new item header from the target node's free_space */
	int max_num_units_to_shift; /* if 0 then no maximimum exists, shift as much as we can */
	node_offset source_body_length;
	key * initial_source_item_key;
	key * final_source_item_key;
	reiser4_item_plugin * source_item_handler;
	int can_copy; /* how many bytes of source item do fit into target node */
	int nr_items; /* in target node */
	int ih_offset; /* offset within a node from node beginning to key of item */


	source_item_handler = get_item_plugin (source);

	target_space_available = leaf40_free_space (target->coord.node);
	is_mergeable = source_item_handler->is_mergeable (source, target);
	if (!is_mergeable)
		/*  new item will be created */
		target_space_available -= LEAF40_ITEM_HEADER_SIZE;

	/* estimate how many units of source item can be copied to target node */
	can_copy = source_item_handler->b.estimate_shift (source, target_space_available,
							  units_to_copy, left);
	if (!can_copy)
		return;

	nr_items = leaf40_num_of_items (target->node);

	/* if we are going to create a new item */
	if (!is_mergeable && left) {
		if (!leaf40_is_empty (target->node)) {
			assert ("vs", target->in_node == nr_items - 1);
			target->in_node ++;
		}
		target_space_available -= LEAF40_ITEM_HEADER_SIZE;
		assert ("vs", target_space_available);
		current_target_size = 0;
		leaf40_set_num_of_items (target->node, nr_items + 1);
		/*target_node_header->item_num++;*/
			
		/* create leaf40 item header (could be an inline function?) */

		/* copy key */
		ih_offset = item_header_byte_offset_by_pos_in_node_leaf40 (target->in_node);
		copy_key (target->coord.node->data + ih_offset, key_by_coord (source));
		/* copy plugin */
		ih_offset += sizeof(reiser4_key);
		cputod16 (plugin_id_by_coord (source), (d16 *)(target->coord.node->data + ih_offset));
		/* offset */
		ih_offset += sizeof(d16);
		/* item body offset within node is set to start of free space */
		cputod16 (leaf40_free_space_start (target->node),
			  (d16 *)(target->coord.node->data + ih_offset));
		/* node_header's free_space and free_space_start is not updated */
	}
	/* either is_mergeable or copying to right */
	else
		if (!is_mergeable && right) {
			/* left shifting is cheaper than right shifting
			   because free space is on the right of items, and a
			   memcpy is avoided */
			/* creating new item header before target->in_node */
			
			/* shift item headers to left */
			ih_offset = item_header_byte_offset_by_pos_in_node_leaf40 (nr_items - 1);
			memmove (target->node + ih_offset + LEAF40_ITEM_HEAD_SIZE, target->node + ih_offset,
				 nr_items * LEAF40_ITEM_HEAD_SIZE);
			/* shift items */
			memmove (target->node + sizeof (node_header_40) + can_copy, 
				 target->node + sizeof (node_header_40), 
				 leaf40_free_space_start (target->node) - sizeof (node_header_40));
			/* update offsets of moved items */
			for (i = 0; i < nr_items; i ++) {
				
			}
			/* node_header's free_space and free_space_start is not updated */
		}
		else if (is_mergeable && left) {
			current_target_end = free_space_start(source_node);
		}
		else if (is_mergeable && right) {
			/* we must estimate before we shift if we are to avoid shifting three times */
		}

	if (left) {
		/* ?? */;
	}
	else {
		/* ?? */
	}
	source_item_handler->merge_items(char * source_item_offset, char * target_item_offset, node_offset current_target_end, 

					 /* the amount we shift is the minimum of max_num_units_to_shift and target_space_available */

					 target_space_available, /* remember to subtract any new item header from the target node's free_space */
					 max_num_units_to_shift /* if 0 then no maximimum exists, shift as much as we can */

					 left, /* 1 if shifting to left, 0 if to right */
					 node_offset source_body_end, key * initial_source_item_key, key * final_source_item_key, key * final_target_item_key
												     )	
}


shift_leaf40_headers(source_zn, target_zn, 
		     int to_left,	/* shifting to left = 1, to right = 0 */
		     pos_in_node pos_last_item_to_shift)
{
	char * source_node_start = source_zn->data;
	char * target_node_start = target_zn->data;

	if (to_left)
		{
			/* headers grow backwards in leaf40 nodes, so copy starts with last one */
			source = node_start + item_header_byte_offset_by_pos_in_node_leaf40(pos_last_item_to_shift);
			count = node_start + LEAF40_NODE_SIZE - source;
			
			dest = target_node_start + item_header_byte_offset_by_pos_in_node_leaf40(num_items(target_zn) + pos_last_item_to_shift);
		}
				memcpy(source, dest, count);
	else
		{		/* left for vs to code after he checks the above logic */
		}
}


/* returns offset within node, so you must add the node address to it
   to use it */
inline int item_header_byte_offset_by_pos_in_node_leaf40 (pos_in_node pos_in_node)
{
	return LEAF40_NODE_SIZE - (pos_in_node + 1) * (sizeof(reiser4_key) + sizeof(pluginid) + sizeof(node_offset_40));
}


/* pluginid of an item the coord is set to */
int plugin_id_by_pos_in_node (insert_coord coord)
{
	d16 * pluginid;

	pluginid = (d16 *)(coord->coord.node->data + (item_header_byte_offset_by_pos_in_node_leaf40 (coord->in_node) +
						      sizeof(reiser4_key)));
	return d16tocpu (pluginid);
}

#endif

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 78
 * scroll-step: 1
 * End:
 */
