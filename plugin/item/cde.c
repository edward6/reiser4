/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory entry implementation
 */

/*
 * DESCRIPTION:
 *
 * This is "compound" directory item plugin implementation. This directory
 * item type is compound (as opposed to the "simple directory item" in
 * fs/reiser4/plugin/item/sde.[ch]), because it consists of several directory
 * entries.
 *
 * The reason behind this decision is disk space efficiency: all directory
 * entries inside the same directory have identical fragment in their
 * keys. This, of course, depends on key assignment policy. In our default key
 * assignment policy, all directory entries have the same locality which is
 * equal to the object id of their directory.
 *
 * Composing directory item out of several directory entries for the same
 * directory allows us to store said key fragment only once. That is, this is
 * some ad hoc form of key compression (stem compression) that is implemented
 * here, because general key compression is not supposed to be implemented in
 * v4.0 and disk space efficiency for directory entries is deemed (who are the
 * deemsters?) to be important.
 *
 * Another decision that was made regarding all directory item plugins, is
 * that they will store entry keys unaligned. This is for that sake of disk
 * space efficiency again.
 *
 * In should be noted, that storing keys unaligned increases CPU consumption,
 * at least on some architectures. [FIXME-NIKITA currently key is copied to
 * the properly aligned location regardless, see extract_key_from_id() and
 * extract_key_from_de_id().]
 *
 * Internal on-disk structure of the compound directory item is the following:
 *
 *      HEADER          cde_item_format.        Here number of entries is stored.
 *      ENTRY_HEADER_0  cde_unit_header.        Here part of entry key and 
 *      ENTRY_HEADER_1                          offset of entry body are stored.
 *      ENTRY_HEADER_2
 *      ...
 *      ENTRY_HEADER_N
 *      ENTRY_BODY_0    directory_entry_format. Here part of stat data key and
 *      ENTRY_BODY_1                            NUL-terminated name are stored.
 *      ENTRY_BODY_2
 *      ...
 *      ENTRY_BODY_N
 *
 * When it comes to the balancing, each directory entry in compound directory
 * item is unit, that is, something that can be cut from one item and pasted
 * into another item of the same type. Handling of unit cut and paste is major
 * reason for the complexity of code below.
 *
 */

#include "../../reiser4.h"

/** return body of compound directory item at @coord */
static cde_item_format *formatted_at( const tree_coord *coord )
{
	assert( "nikita-1282", coord != NULL );
	return item_body_by_coord( coord );
}


/** return entry header at @coord */
static cde_unit_header *header_at( const tree_coord *coord /* coord of item */, 
				   int idx /* index of unit */ )
{
	assert( "nikita-1283", coord != NULL );
	return &formatted_at( coord ) -> entry[ idx ];
}

/** return number of units in compound directory item at @coord */
static int units( const tree_coord *coord /* coord of item */ )
{
	return d16tocpu( &formatted_at( coord ) -> num_of_entries );
}

/** return offset of the body of @idx-th entry in @coord */
static unsigned int offset_of( const tree_coord *coord /* coord of item */, 
			       int idx /* index of unit */ )
{
	if( idx < units( coord ) )
		return d16tocpu( &header_at( coord, idx ) -> offset );
	else if( idx == units( coord ) )
		return item_length_by_coord( coord );
	else impossible( "nikita-1308", "Wrong idx" );
	return 0;
}

/** set offset of the body of @idx-th entry in @coord */
static void set_offset( const tree_coord *coord /* coord of item */, 
			int idx /* index of unit */, 
			unsigned int offset /* new offset */)
{
	cputod16( ( __u16 ) offset, &header_at( coord, idx ) -> offset );
}

/** return pointer to @offset-th byte from the beginning of @coord */
static char *address( const tree_coord *coord /* coord of item */, 
		      int offset )
{
	return ( ( char * ) item_body_by_coord( coord ) ) + offset;
}

/** return pointer to the body of @idx-th entry in @coord */
static directory_entry_format *entry_at( const tree_coord *coord /* coord of
								  * item */, 
					 int idx /* index of unit */ )
{
	return ( directory_entry_format * ) address
		( coord, ( int ) offset_of( coord, idx ) );
}

/** return number of unit referencesd by @coord */
static int idx_of( const tree_coord *coord /* coord of item */ )
{
	assert( "nikita-1285", coord != NULL );
	return coord -> unit_pos;
}

/** find position where entry with @entry_key would be inserted into @coord */
static int find( const tree_coord *coord /* coord of item */, 
		 const reiser4_key *entry_key /* key to look for */, 
		 cmp_t *last /* result of last comparison */ )
{
	int i;
	int entries;

	assert( "nikita-1295", coord != NULL );
	assert( "nikita-1296", entry_key != NULL );
	assert( "nikita-1297", last != NULL );

	/*
	 * FIXME-NIKITA hidden treasure! sequential search for now
	 */
	entries = units( coord );
	for( i = 0 ; i < entries ; ++ i ) {
		cde_unit_header *header;

		header = header_at( coord, i );
		*last = de_id_key_cmp( &header -> hash, entry_key );
		
		if( *last != LESS_THAN )
			break;
	} 
	if( i < entries )
		return i;
	else
		return -ENOENT;
}

/**
 * expand @coord as to accomodate for insertion of @no new entries starting
 * from @pos, with total bodies size @size.
 */
static int expand_item( const tree_coord *coord /* coord of item */, 
			int pos /* unit position*/, int no /* number of new
							    * units*/, 
			int size /* total size of new units' data*/, 
			unsigned int data_size /* free space already reserved
						* in the item for insertion */ )
{
	int entries;
	cde_unit_header  *header;
	char *dent;
	int   i;

	assert( "nikita-1310", coord != NULL );
	assert( "nikita-1311", pos >= 0 );
	assert( "nikita-1312", no > 0 );
	assert( "nikita-1313", data_size >= no * sizeof( directory_entry_format ) );
	assert( "nikita-1343", item_length_by_coord( coord ) >=
		( int ) ( size + data_size + no * sizeof *header ) );

	entries = units( coord );

	if( pos == entries )
		dent = address( coord, size );
	else
		dent = ( char * ) entry_at( coord, pos );
	/*
	 * place where new header will be in
	 */
	header = header_at( coord, pos );
	/*
	 * free space for new entry headers
	 */
	xmemmove( header + no, header, 
		 ( unsigned )( address( coord, size ) - ( char * ) header ) );
	/*
	 * if adding to the end initialise first new header
	 */
	if( pos == entries ) {
		set_offset( coord, pos, ( unsigned ) size );
	}

	/*
	 * adjust entry pointer and size
	 */
	dent  = dent + sizeof *header;
	size += sizeof *header;
	/*
	 * free space for new entries
	 */
	xmemmove( dent + data_size, dent, 
		 ( unsigned ) ( address( coord, size ) - dent ) );

	/*
	 * increase counter
	 */
	entries += no;
	cputod16( ( __u16 ) entries, &formatted_at( coord ) -> num_of_entries );

	/*
	 * [ 0 ... pos ] entries were shifted by no * ( sizeof *header )
	 * bytes. 
	 */
	for( i = 0 ; i <= pos ; ++ i )
		set_offset( coord, i, 
			    offset_of( coord, i ) + no * sizeof *header );
	/*
	 * [ pos + no ... +\infty ) entries were shifted by ( no *
	 * sizeof *header + data_size ) bytes
	 */
	for( i = pos + no ; i < entries ; ++ i )
		set_offset( coord, i, offset_of( coord, i ) + 
			    no * sizeof *header + data_size );
	return 0;
}

/** insert new @entry into item */
static int expand( const tree_coord *coord /* coord of item */, 
		   cde_entry *entry /* entry to insert */, 
		   int len /* length of @entry data */, 
		   int *pos /* position to insert */, 
		   reiser4_dir_entry_desc *dir_entry /* parameters for new
						      * entry */ )
{
	cmp_t  cmp_res;

	*pos = find( coord, &dir_entry -> key, &cmp_res );
	if( *pos < 0 )
		*pos = units( coord );

	expand_item( coord, *pos, 1, item_length_by_coord( coord ) - len, 
		     sizeof( directory_entry_format ) + entry -> name -> len + 1 );

	return 0;
}

/** paste body of @entry into item */
static int paste_entry( const tree_coord *coord /* coord of item */, 
			cde_entry *entry /* new entry */, 
			int pos /* position to insert */, 
			reiser4_dir_entry_desc *dir_entry /* parameters for
							   * new entry */ )
{
	cde_unit_header        *header;
	directory_entry_format *dent;

	header = header_at( coord, pos );
	dent   = entry_at( coord, pos );

	build_de_id_by_key( &dir_entry -> key, &header -> hash );
	build_inode_key_id( entry -> obj, &dent -> id );
	strcpy( ( char * ) dent -> name, entry -> name -> name );
	cputod8( 0, &dent -> name[ entry -> name -> len ] );
	return 0;
}

/**
 * estimate how much space is necessary in item to insert/paste set of entries
 * described in @data.
 */
int cde_estimate( const tree_coord *coord /* coord of item */, 
		  const reiser4_item_data *data /* parameters for new item */ )
{
	cde_entry_data *e;
	int             result;
	int             i;

	e = ( cde_entry_data * ) data -> data;

	assert( "nikita-1288", e != NULL );
	assert( "nikita-1289", e -> num_of_entries >= 0 );

	if( coord == NULL )
		/*
		 * insert
		 */
		result = sizeof( cde_item_format );
	else
		/*
		 * paste
		 */
		result = 0;

	result += e -> num_of_entries * 
		( sizeof( cde_unit_header ) + sizeof( directory_entry_format ) );
	for( i = 0 ; i < e -> num_of_entries ; ++i )
		result += strlen( e -> entry[ i ].name -> name ) + 1;
	( ( reiser4_item_data * ) data ) -> length = result;
	return result;
}

/** ->nr_units() method for this item plugin. */
unsigned cde_nr_units( const tree_coord *coord /* coord of item */ )
{
	return units( coord );
}

/**
 * ->unit_key() method for this item plugin.
 */
reiser4_key *cde_unit_key( const tree_coord *coord /* coord of item */, 
			   reiser4_key *key /* resulting key */ )
{
	assert( "nikita-1452", coord != NULL );
	assert( "nikita-1345", idx_of( coord ) < units( coord ) );
	assert( "nikita-1346", key != NULL );

	item_key_by_coord( coord, key );
	extract_key_from_de_id( extract_dir_id_from_key( key ),
				&header_at( coord, idx_of( coord ) ) -> hash,
				key );
	return key;
}

/**
 * cde_mergeable(): implementation of ->mergeable() item method.
 *
 * Two directory items are mergeable iff they are from the same
 * directory. That simple.
 *
 */
int cde_mergeable( const tree_coord *p1 /* coord of first item */, 
		   const tree_coord *p2 /* coord of second item */ )
{
	reiser4_key k1;
	reiser4_key k2;

	assert( "nikita-1339", p1 != NULL );
	assert( "nikita-1340", p2 != NULL );

	return 
		( item_plugin_by_coord( p1 ) == item_plugin_by_coord( p2 ) ) &&
		( extract_dir_id_from_key( item_key_by_coord( p1, &k1 ) ) ==
		  extract_dir_id_from_key( item_key_by_coord( p2, &k2 ) ) );
		
}

/** ->max_key_inside() method for this item plugin. */
reiser4_key *cde_max_key_inside( const tree_coord *coord /* coord of item */, 
				 reiser4_key *result /* resulting key */ )
{
	assert( "nikita-1342", coord != NULL );

	item_key_by_coord( coord, result );
	set_key_objectid( result, get_key_objectid( max_key() ) );
	set_key_offset( result, get_key_offset( max_key() ) );
	return result;
}

/** @data contains data which are to be put into tree */
int cde_can_contain_key( const tree_coord *coord /* coord of item */, 
			 const reiser4_key *key /* key to check */,
			 const reiser4_item_data *data /* parameters of new
							* item/unit being
							* created */ )
{
	reiser4_key item_key;

	assert( "vs-457", data && data -> data );
	assert( "vs-553", data -> user == 0 );
	item_key_by_coord( coord, &item_key);

	return 	( item_plugin_by_coord( coord ) == data -> iplug ) &&
		( extract_dir_id_from_key( &item_key ) ==
		  extract_dir_id_from_key( key ) );
}

/** ->print() method for this item plugin. */
void cde_print( const char *prefix /* prefix to print */, 
		tree_coord *coord /* coord of item to print */ )
{
	assert( "nikita-1077", prefix != NULL );
	assert( "nikita-1078", coord != NULL );

	if( item_length_by_coord( coord ) < ( int ) sizeof( cde_item_format ) ) {
		info( "%s: wrong size: %i < %i\n", prefix,
		      item_length_by_coord( coord ), 
		      sizeof( cde_item_format ) );
	} else {
		char        *name;
		char        *end;
		char        *start;
		int          i;
		oid_t        dirid;
		reiser4_key  key;

		start = address( coord, 0 );
		end = address( coord, item_length_by_coord( coord ) );
		item_key_by_coord( coord, &key );
		dirid = extract_dir_id_from_key( &key );

		info( "%s: units: %i\n", prefix, cde_nr_units( coord ) );
		for( i = 0 ; i < units( coord ) ; ++ i ) {
			cde_unit_header *header;

			header = header_at( coord, i );
			indent_znode( coord -> node );
			info( "\theader %i: ", i );
			if( ( char * ) ( header + 1 ) > end ) {
				info( "out of bounds: %p [%p, %p]\n", header,
				      start, end );
			} else {
				extract_key_from_de_id
					( dirid, &header -> hash, &key );
				info( "%i: at %i, offset: %i, ", i,
				      i * sizeof( *header ), 
				      d16tocpu( &header -> offset ) );
				print_key( "key", &key );
			}
		}
		for( i = 0 ; i < units( coord ) ; ++ i ) {
			directory_entry_format *entry;

			entry = entry_at( coord, i );
			indent_znode( coord -> node );
			info( "\tentry: %i: ", i );
			if( ( ( char * ) ( entry + 1 ) > end ) || 
			    ( ( char * ) entry < start ) ) {
				info( "out of bounds: %p [%p, %p]\n", entry,
				      start, end );
			} else {
				coord -> unit_pos = i;
				cde_extract_key( coord, &key );
				name = cde_extract_name( coord );
				info( "at %i, name: %s, ", 
				      ( char * ) entry - start, name );
				print_key( "sdkey", &key );
			}
		}
	}
}

/**
 * cde_check ->check() method for compressed directory items
 *
 * used for debugging, every item should have here the most complete
 * possible check of the consistency of the item that the inventor can
 * construct 
 */
int cde_check( tree_coord *coord /* coord of item to check */, 
	       const char **error /* where to store error message */ )
{
	int   i;
	int   result;
	char *item_start;
	char *item_end;
	
	tree_coord c;
	
	assert( "nikita-1357", coord != NULL );
	assert( "nikita-1358", error != NULL );

	item_start = item_body_by_coord( coord );
	item_end = item_start + item_length_by_coord( coord );

	coord_dup( &c, coord );
	result = 0;
	for( i = 0 ; i < units( coord ) ; ++ i ) {
		directory_entry_format *entry;

		if( ( char * ) ( header_at( coord, i ) + 1 ) >= 
		    item_end - units( coord ) * sizeof *entry ) {
			*error = "CDE header is out of bounds";
			result = -1;
			break;
		}
		entry = entry_at( coord, i );
		if( ( char * ) entry < item_start + sizeof( cde_item_format ) ) {
			*error = "CDE header is too low";
			result = -1;
			break;
		}
		if( ( char * ) ( entry + 1 ) >= item_end ) {
			*error = "CDE header is too high";
			result = -1;
			break;
		}
	}
	return result;
}

/** ->init() method for this item plugin. */
int cde_init( tree_coord *coord /* coord of item */,
	      reiser4_item_data *data /* structure used for insertion */
	      UNUSED_ARG )
{
	cputod16( 0u, &formatted_at( coord ) -> num_of_entries );
	return 0;
}

/** ->lookup() method for this item plugin. */
lookup_result cde_lookup( const reiser4_key *key /* key to search for */, 
			  lookup_bias bias /* search bias */, 
			  tree_coord *coord /* coord of item to lookup in */ )
{
	cmp_t last_comp;
	int   pos;

	reiser4_key utmost_key;

	assert( "nikita-1293", coord != NULL );
	assert( "nikita-1294", key != NULL );

	if( keygt( key, cde_max_key_inside( coord, &utmost_key ) ) ) {
		/*
		 * @key is from another directory item
		 */
		coord -> unit_pos = units( coord ) - 1;
		coord -> between = AFTER_UNIT;
		return CBK_COORD_NOTFOUND;
	}
	if( keygt( item_key_by_coord( coord, &utmost_key ), key ) ) {
		coord -> unit_pos = 0;
		coord -> between = BEFORE_UNIT;
		return CBK_COORD_NOTFOUND;
	}
	pos = find( coord, key, &last_comp );
	if( pos >= 0 ) {
		coord -> unit_pos = ( int ) pos;
		switch( last_comp ) {
		case EQUAL_TO:
			coord -> between = AT_UNIT;
			return CBK_COORD_FOUND;
		case GREATER_THAN:
			coord -> between = BEFORE_UNIT;
			return CBK_COORD_NOTFOUND;
		case LESS_THAN:
		default:
			impossible( "nikita-1298", "Broken find" );
			return CBK_IO_ERROR;
		}
	} else {
		coord -> unit_pos = units( coord ) - 1;
		coord -> between = AFTER_UNIT;
		return ( bias == FIND_MAX_NOT_MORE_THAN ) ? CBK_COORD_FOUND : 
			CBK_COORD_NOTFOUND;
	}
}

/** ->paste() method for this item plugin. */
int cde_paste( tree_coord *coord /* coord of item */, 
	       reiser4_item_data *data /* parameters of new unit being
					* inserted */, 
	       carry_level *todo UNUSED_ARG /* todo carry queue */ )
{
	cde_entry_data *e;
	int             result;
	int             i;

	e = ( cde_entry_data * ) data -> data;

	result = 0;
	for( i = 0 ; i < e -> num_of_entries ; ++i ) {
		int pos;
		int phantom_size;

		phantom_size = data -> length;
		if( units( coord ) == 0 )
			phantom_size -= sizeof( cde_item_format );

		result = expand( coord, e -> entry + i, phantom_size, &pos, 
				 data -> arg );
		if( result != 0 )
			break;
		result = paste_entry( coord, e -> entry + i, pos, 
				      data -> arg );
		if( result != 0 )
			break;
	}
	return result;
}

/**
 * amount of space occupied by all entries starting from @idx both headers and
 * bodies.
 */
static unsigned int part_size( const tree_coord *coord /* coord of item */, 
			       int idx /* index of unit */ )
{
	assert( "nikita-1299", coord != NULL );
	assert( "nikita-1300", idx < ( int ) units( coord ) );

	return sizeof( cde_item_format ) + 
		( idx + 1 ) * sizeof( cde_unit_header ) +
		offset_of( coord, idx + 1 ) - offset_of( coord, 0 );
}

/* how many but not more than @want units of @source can be merge with
   item in @target node. If pend == append - we try to append last item
   of @target by first units of @source. If pend == prepend - we try to
   "prepend" first item in @target by last units of @source. @target
   node has @free_space bytes of free space. Total size of those units
   are returned via @size */
int cde_can_shift( unsigned free_space /* free space in item */, 
		   tree_coord *coord /* coord of source item */, 
		   znode *target /* target node */, 
		   shift_direction pend /* shift direction */, 
		   unsigned *size /* resulting number of shifted bytes */, 
		   unsigned want /* maximal number of bytes to shift */ )
{
	int shift;

	if( want == 0 ) {
		*size = 0;
		return 0;
	}

	/*
	 * pend == SHIFT_LEFT <==> shifting to the left
	 */
	if( pend == SHIFT_LEFT ) {
		for( shift = min( ( int ) want - 1, units( coord ) ) ; 
		     shift >= 0 ; -- shift ) {
			*size = part_size( coord, shift );
			if( target != NULL )
				*size -= sizeof( cde_item_format );
			if( *size <= free_space )
				break;
		}
		shift = shift + 1;
	} else {
		int total_size;

		assert( "nikita-1301", pend == SHIFT_RIGHT );
		
		total_size = item_length_by_coord( coord );
		for( shift = units( coord ) - want - 1 ; 
		     shift < units( coord ) - 1 ; ++ shift ) {
			*size = total_size - part_size( coord, shift );
			if( target == NULL )
				*size += sizeof( cde_item_format );
			if( *size <= free_space )
				break;
		}
		shift = units( coord ) - shift - 1;
	}
	if( shift == 0 )
		*size = 0;
	return shift;
}

/** ->copy_units() method for this item plugin. */
void cde_copy_units( tree_coord *target /* coord of target item */, 
		     tree_coord *source /* coord of source item */,
		     unsigned from /* starting unit */, 
		     unsigned count /* how many units to copy */,
		     shift_direction where_is_free_space /* shift direction */,
		     unsigned free_space /* free space in item */ )
{
	char *header_from;
	char *header_to;
	
	char *entry_from;
	char *entry_to;

	int   pos_in_target;
	int   data_size;
	int   data_delta;
	int   i;
#if REISER4_TRACE
	reiser4_key debug_key;
#endif

	assert( "nikita-1303", target != NULL );
	assert( "nikita-1304", source != NULL );
	assert( "nikita-1305", ( int ) from < units( source ) );
	assert( "nikita-1307", ( int ) ( from + count ) <= units( source ) );

	trace_if( TRACE_DIR | TRACE_NODES, 
		  print_key( "cde_copy source", 
			     item_key_by_coord( source, &debug_key ) ) );
	trace_if( TRACE_DIR | TRACE_NODES, 
		  print_key( "cde_copy target", 
			     item_key_by_coord( target, &debug_key ) ) );
		  
	if( where_is_free_space == SHIFT_LEFT ) {
		assert( "nikita-1453", from == 0 );
		pos_in_target = units( target );
	} else {
		assert( "nikita-1309", 
			( int ) ( from + count ) == units( source ) );
		pos_in_target = 0;
		xmemmove( item_body_by_coord( target ), 
			 ( char * ) item_body_by_coord( target ) + free_space,
			 item_length_by_coord( target ) - free_space );
	}

	/* expand @target */
	data_size = 
		offset_of( source, ( int ) ( from + count ) ) - 
		offset_of( source, ( int ) from );

	if( units( target ) == 0 )
		free_space -= sizeof( cde_item_format );

	expand_item( target, pos_in_target, ( int ) count, 
		     ( int ) ( item_length_by_coord( target ) - free_space ), 
		     ( unsigned ) data_size );

	/* copy first @count units of @source into @target */
	data_delta = offset_of( target, pos_in_target ) - 
		offset_of( source, ( int ) from );

	/* copy entries */
	entry_from  = ( char * ) entry_at( source, ( int ) from );
	entry_to = ( char * ) entry_at( source, ( int ) ( from + count ) );
	xmemmove( entry_at( target, pos_in_target ), 
		 entry_from, ( unsigned ) ( entry_to - entry_from ) );

	/* copy headers */
	header_from = ( char * ) header_at( source, ( int ) from );
	header_to   = ( char * ) header_at( source, ( int ) ( from + count ) );
	xmemmove( header_at( target, pos_in_target ),
		 header_from, ( unsigned ) ( header_to - header_from ) );

	/* update offsets */
	for( i = pos_in_target ; i < ( int ) ( pos_in_target + count ) ; ++ i ) {
		set_offset( target, i, offset_of( target, i ) + data_delta );
	}
}

/** ->cut_units() method for this item plugin. */
int cde_cut_units( tree_coord *coord /* coord of item */, 
		   unsigned *from /* start unit pos */, 
		   unsigned *to /* stop unit pos */,
		   const reiser4_key *from_key UNUSED_ARG /* start key */,
		   const reiser4_key *to_key UNUSED_ARG /* stop key */,
		   reiser4_key *smallest_removed /* smallest key actually
						  * removed */ )
{
	char *header_from;
	char *header_to;
	
	char *entry_from;
	char *entry_to;

	int   size;
	int   entry_delta;
	int   header_delta;
	int   i;

	unsigned count;

	count = *to - *from + 1;

	assert( "nikita-1454", coord != NULL );
	assert( "nikita-1455", ( int ) ( *from + count ) <= units( coord ) );

	if (smallest_removed)
		unit_key_by_coord (coord, smallest_removed);

	size = item_length_by_coord( coord );
	if( count == (unsigned)units( coord ) ) {
		return size;
	}

	header_from = ( char * ) header_at( coord, ( int ) *from );
	header_to   = ( char * ) header_at( coord, ( int ) ( *from + count ) );

	entry_from  = ( char * ) entry_at( coord, ( int ) *from );
	entry_to = ( char * ) entry_at( coord, ( int ) ( *from + count ) );

	/* move headers */
	xmemmove( header_from, header_to, 
		 ( unsigned ) ( address( coord, size ) - header_to ) );

	header_delta = header_to - header_from;

	entry_from -= header_delta;
	entry_to   -= header_delta;
	size       -= header_delta;

	/* copy entries */
	xmemmove( entry_from, entry_to, 
		 ( unsigned ) ( address( coord, size ) - entry_to ) );

	entry_delta = entry_to - entry_from;
	size       -= entry_delta;

	/* update offsets */

	for( i = 0 ; i < ( int ) *from ; ++ i ) {
		set_offset( coord, i, offset_of( coord, i ) - header_delta );
	}

	for( i = *from ; i < units( coord ) - (int ) count ; ++ i ) {
		set_offset( coord, i, offset_of( coord, i ) - header_delta - 
			    entry_delta );
	}

	cputod16( ( __u16 ) units( coord ) - count, 
		  &formatted_at( coord ) -> num_of_entries );

	if( *from == 0 ) {
		/* entries from head was removed - move remaining to right */
		xmemmove( ( char * ) item_body_by_coord( coord ) + 
			 header_delta + entry_delta,
			 item_body_by_coord( coord ), ( unsigned ) size );
		if( REISER4_DEBUG )
			xmemset( item_body_by_coord( coord ), 0, 
				( unsigned )header_delta + entry_delta );
	} else {
		/* freed space is already at the end of item */
		if( REISER4_DEBUG )
			xmemset( ( char * )item_body_by_coord( coord ) + size, 0,
				( unsigned )header_delta + entry_delta );
	}

	return header_delta + entry_delta;
}

/** ->s.dir.extract_key() method for this item plugin. */
int cde_extract_key( const tree_coord *coord /* coord of item */, 
		     reiser4_key *key /* resulting key */ )
{
	directory_entry_format *dent;

	assert( "nikita-1155", coord != NULL );
	assert( "nikita-1156", key != NULL );

	dent = entry_at( coord, idx_of( coord ) );
	return extract_key_from_id( &dent -> id, key );
}

/** ->s.dir.extract_name() method for this item plugin. */
char *cde_extract_name( const tree_coord *coord /* coord of item */ )
{
	directory_entry_format *dent;

	assert( "nikita-1157", coord != NULL );

	dent = entry_at( coord, idx_of( coord ) );
	return ( char * ) dent -> name;
}

/** ->s.dir.add_entry() method for this item plugin */
int cde_add_entry( const struct inode *dir /* directory object */, 
		   tree_coord *coord /* coord of item */, 
		   lock_handle *lh /* lock handle for insertion */, 
		   const struct dentry *name /* name to insert */, 
		   reiser4_dir_entry_desc *dir_entry /* parameters of new
						      * directory entry */ )
{
	reiser4_item_data data;
	cde_entry         entry;
	cde_entry_data    edata;
	int               result;

	assert( "nikita-1656", coord -> node == lh -> node );
	assert( "nikita-1657", znode_is_write_locked( coord -> node ) );

	edata.num_of_entries = 1;
	edata.entry = &entry;

	entry.dir  = dir;
	entry.obj  = dir_entry -> obj;
	entry.name = &name -> d_name;

	data.data   = ( char * ) &edata;
	data.user   = 0; /* &edata is not user space */
	data.iplug  = item_plugin_by_id( COMPOUND_DIR_ID );
	data.arg = dir_entry;
	assert( "nikita-1302", data.iplug != NULL );

	if( is_dot_key( &dir_entry -> key ) ) {
		data.length = cde_estimate( NULL, &data );
		result = insert_by_coord( coord, &data, &dir_entry -> key, lh,
					  inter_syscall_ra( dir ), 
					  NO_RAP, 0/*flags*/ );
	} else {
		data.length = cde_estimate( coord, &data );
		result = resize_item( coord, &data, 
				      &dir_entry -> key, lh, 0/*flags*/ );
	}
	return result;
}

/** ->s.dir.rem_entry() == de_rem_entry */

/** ->s.dir.max_name_len() method for this item plugin */
int cde_max_name_len( int block_size /* block size */ )
{
	return block_size - REISER4_NODE_MAX_OVERHEAD - 
		sizeof( directory_entry_format ) - 
		sizeof( cde_item_format ) - 
		sizeof( cde_unit_header ) - 2;
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
