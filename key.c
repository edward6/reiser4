/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
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
 * Maximal possible key: all components are ~0. It is presumed that this is
 * independent of key scheme.
 */
static const reiser4_key MAXIMAL_KEY = {
	.el = { { ~0ull }, { ~0ull }, { ~0ull } }
};

/** Initialise key. */
void key_init( reiser4_key *key /* key to init */ )
{
	assert( "nikita-1169", key != NULL );
	xmemset( key, 0, sizeof *key );
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
cmp_t keycmp( const reiser4_key *k1 /* first key to compare */, 
	      const reiser4_key *k2 /* second key to compare */ )
{
	cmp_t result;

	assert( "nikita-439", k1 != NULL );
	assert( "nikita-440", k2 != NULL );

	if( REISER4_PLANA_KEY_ALLOCATION ) {
		reiser4_stat_key_add( eq0 );
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
			reiser4_stat_key_add( eq1 );
			/* compare objectid (and band if it's there) */
			result = DIFF_EL( 1 );
			/* compare offset */
			if( result == EQUAL_TO ) {
				reiser4_stat_key_add( eq2 );
				result = DIFF_EL( 2 );
				ON_STATS({ 
					if( result == EQUAL_TO ) 
						reiser4_stat_key_add( eq3 ); });
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

/** true if @k1 equals @k2 */
int keyeq( const reiser4_key *k1 /* first key to compare */, 
	   const reiser4_key *k2 /* second key to compare */ )
{
	assert( "nikita-1879", k1 != NULL );
	assert( "nikita-1880", k2 != NULL );
	return !memcmp( k1, k2, sizeof *k1 );
}

/** true if @k1 is less than @k2 */
int keylt( const reiser4_key *k1 /* first key to compare */, 
	   const reiser4_key *k2 /* second key to compare */ )
{
	assert( "nikita-1952", k1 != NULL );
	assert( "nikita-1953", k2 != NULL );
	return keycmp( k1, k2 ) == LESS_THAN;
}

/** true if @k1 is less than or equal to @k2 */
int keyle( const reiser4_key *k1 /* first key to compare */, 
	   const reiser4_key *k2 /* second key to compare */ )
{
	assert( "nikita-1954", k1 != NULL );
	assert( "nikita-1955", k2 != NULL );
	return keycmp( k1, k2 ) != GREATER_THAN;
}

/** true if @k1 is greater than @k2 */
int keygt( const reiser4_key *k1 /* first key to compare */, 
	   const reiser4_key *k2 /* second key to compare */ )
{
	assert( "nikita-1959", k1 != NULL );
	assert( "nikita-1960", k2 != NULL );
	return keycmp( k1, k2 ) == GREATER_THAN;
}

/** true if @k1 is greater than or equal to @k2 */
int keyge( const reiser4_key *k1 /* first key to compare */, 
	   const reiser4_key *k2 /* second key to compare */ )
{
	assert( "nikita-1956", k1 != NULL );
	assert( "nikita-1957", k2 != NULL ); /* October  4: sputnik launched
					      * November 3: Laika */
	return keycmp( k1, k2 ) != LESS_THAN;
}

#if REISER4_DEBUG_OUTPUT
/** debugging aid: print symbolic name of key type */
static const char *type_name( unsigned int key_type /* key type */ )
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
void print_key( const char *prefix /* prefix to print */, 
		const reiser4_key *key /* key to print */ )
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

#endif

int sprintf_key( char *buffer /* buffer to print key into */, 
		 const reiser4_key *key /* key to print */ )
{
	return sprintf( buffer, "(%Lx:%x:%Lx:%Lx:%Lx)",
			get_key_locality( key ), get_key_type( key ),
			get_key_band( key ), get_key_objectid( key ), 
			get_key_offset( key ) );
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
