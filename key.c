/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */


/*
 * Key manipulations.
 */

#include "reiser4.h"

/**
 * Minimal possible key: all components are zero. It is presumed that this is
 * independent of key scheme.
 */
static const reiser4_key MINIMAL_KEY = {
	.el = { { 0ull }, { 0ull }, { 0ull } }
};

/**
 * Minimal possible key: all components are ~0. It is presumed that this is
 * independent of key scheme.
 */
static const reiser4_key MAXIMAL_KEY = {
	.el = { { ~0ull }, { ~0ull }, { ~0ull } }
};

/**
 * Initialise key.
 */
void key_init( reiser4_key *key )
{
	assert( "nikita-1169", key != NULL );
	memset( key, 0, sizeof *key );
}

/** minimal possible key in the tree. Return pointer to the static storage. */
const reiser4_key *min_key( void )
{
	return &MINIMAL_KEY;
}

/** maximum possible key in the tree. Return pointer to the static storage. */
const reiser4_key *max_key( void )
{
	return &MAXIMAL_KEY;
}

/** helper macro for keycmp() */
#define DIFF( field )								\
({										\
	typeof ( get_key_ ## field ( k1 ) ) f1;                              	\
	typeof ( get_key_ ## field ( k2 ) ) f2;					\
										\
	f1 = get_key_ ## field ( k1 );						\
	f2 = get_key_ ## field ( k2 );						\
										\
	( f1 < f2 ) ? LESS_THAN : ( ( f1 == f2 ) ? EQUAL_TO : GREATER_THAN );	\
})

/** helper macro for keycmp() */
#define DIFF_EL( off )								\
({										\
	__u64 e1;								\
	__u64 e2;								\
										\
	e1 = get_key_el ( k1, off );						\
	e2 = get_key_el ( k2, off );						\
										\
	( e1 < e2 ) ? LESS_THAN : ( ( e1 == e2 ) ? EQUAL_TO : GREATER_THAN );	\
})

/** compare `k1' and `k2'.  This function is a heart of "key allocation
    policy". All you need to implement new policy is to add yet another
    clause here. */
cmp_t keycmp( const reiser4_key *k1, const reiser4_key *k2 )
{
	cmp_t result;

	assert( "nikita-439", k1 != NULL );
	assert( "nikita-440", k2 != NULL );

	if( REISER4_PLANA_KEY_ALLOCATION ) {
		/* if physical order of fields in a key is identical
		   with logical order, we can implement key comparison
		   as three 64bit comparisons. */
		/* 
		   logical order of fields in plan-a:
		   locality->type->objectid->offset.
		 */
		/* compare locality and type at once */
		result = DIFF_EL( 0 );
		if( result == EQUAL_TO ) {
			/* compare objectid (and band if it's there) */
			result = DIFF_EL( 1 );
			/* compare offset */
			if( result == EQUAL_TO ) {
				result = DIFF_EL( 2 );
			}
		}
	} else if( REISER4_3_5_KEY_ALLOCATION ) {
		result = DIFF( locality );
		if( result == EQUAL_TO ) {
			result = DIFF( objectid );
			if( result == EQUAL_TO ) {
				result = DIFF( type );
				if( result == EQUAL_TO )
					result = DIFF( offset );
			}
		}
	} else impossible( "nikita-441", "Unknown key allocation scheme!" );
	return result;
}

/** debugging aid: print symbolic name of key type */
static const char *type_name( unsigned int key_type )
{
	switch( key_type ) {
	case KEY_FILE_NAME_MINOR:
		return "file name";
	case KEY_SD_MINOR:
		return "stat data";
	case KEY_ATTR_NAME_MINOR:
		return "attr name";
	case KEY_ATTR_BODY_MINOR:
		return "attr body";
	case KEY_BODY_MINOR:
		return "file body";
	default:
		return "unknown";
	}
}

/** debugging aid: print human readable information about key */
void print_key( const char *prefix, const reiser4_key *key )
{
	/* turn bold on */
	/* printf ("\033[1m"); */
	if( key == NULL )
		info( "%s: null key\n", prefix );
	else {
		info( "%s: (%Lx:%x:%Lx:%Lx:%Lx)[%s]\n", prefix,
		      get_key_locality( key ), get_key_type( key ),
		      get_key_band( key ),
		      get_key_objectid( key ), get_key_offset( key ),
		      type_name( get_key_type( key ) ) );
	}
	/* turn bold off */
	/* printf ("\033[m\017"); */
}

char *sprintf_key( char *buffer, const reiser4_key *key )
{
	sprintf( buffer, "(%Lx:%x:%Lx:%Lx:%Lx)",
		 get_key_locality( key ), get_key_type( key ),
		 get_key_band( key ),
		 get_key_objectid( key ), get_key_offset( key ) );
	return buffer;
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
