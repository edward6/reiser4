/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Directory entry implementation
 */

#include "../../reiser4.h"

void de_print( const char *prefix, tree_coord *coord )
{
	assert( "nikita-1456", prefix != NULL );
	assert( "nikita-1457", coord != NULL );

	if( item_length_by_coord( coord ) < 
	    ( int ) sizeof( directory_entry_format ) ) {
		info( "%s: wrong size: %i < %i\n", prefix,
		      item_length_by_coord( coord ), 
		      sizeof( directory_entry_format ) );
	} else {
		reiser4_key  sdkey;
		char        *name;

		de_extract_key( coord, &sdkey );
		name = de_extract_name( coord );
		info( "%s: name: %s\n", prefix, name );
		print_key( "\tsdkey", &sdkey );
	}
}

int de_extract_key( const tree_coord *coord, reiser4_key *key )
{
	directory_entry_format *dent;

	assert( "nikita-1458", coord != NULL );
	assert( "nikita-1459", key != NULL );

	dent = ( directory_entry_format * ) item_body_by_coord( coord );
	assert( "nikita-1158", item_length_by_coord( coord ) >=
		( int ) sizeof *dent );
	return extract_key_from_id( &dent -> id, key );
}

char *de_extract_name( const tree_coord *coord )
{
	directory_entry_format *dent;

	assert( "nikita-1460", coord != NULL );

	dent = ( directory_entry_format * ) item_body_by_coord( coord );
	assert( "nikita-1160", item_length_by_coord( coord ) >= 
		( int ) sizeof *dent );
	return ( char * ) dent -> name;
}

int de_add_entry( const struct inode *dir, tree_coord *coord, 
		  reiser4_lock_handle *lh, const struct dentry *name, 
		  reiser4_entry *entry )
{
	reiser4_item_data       data;
	directory_entry_format *dent;
	int                     result;

	data.length = sizeof *dent + name -> d_name.len + 1;
	data.data   = NULL;
	data.plugin = plugin_by_id( REISER4_ITEM_PLUGIN_ID, SIMPLE_DIR_ITEM_ID );
	
	result = insert_by_coord( coord, &data, &entry -> key, lh,
				  inter_syscall_ra( dir ), NO_RA );
	dent = ( directory_entry_format * ) item_body_by_coord( coord );
	build_inode_key_id( entry -> obj, &dent -> id );
	assert( "nikita-1163", 
		strlen( name -> d_name.name ) == name -> d_name.len );
	strcpy( ( char * ) dent -> name, name -> d_name.name );
	cputod8( 0, &dent -> name[ name -> d_name.len ] );
	return 0;
}

int de_rem_entry( const struct inode *dir UNUSED_ARG, tree_coord *coord, 
		  reiser4_lock_handle *lh UNUSED_ARG , 
		  reiser4_entry *entry UNUSED_ARG )
{
	int         result;
	tree_coord coord_shadow;

	/*
	 * cut_node() is supposed to take pointers to _different_
	 * coords, because it will modify them without respect to
	 * possible aliasing. To work around this, create temporary copy
	 * of @coord.
	 */
	dup_coord( &coord_shadow, coord );
	result = cut_node( coord, coord, NULL, NULL, NULL );
	done_coord( &coord_shadow );
	return result;
}

int de_max_name_len( int block_size )
{
	return block_size - REISER4_NODE_MAX_OVERHEAD - 
		sizeof( directory_entry_format ) - 2;
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
