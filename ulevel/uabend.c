/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level abnormal termination handling.
 */

#include "ulevel.h"
#include "../reiser4.h"

#if HAS_BFD
#include <bfd.h>
#endif

#define __USE_GNU
#if USE_DLADDR_DLSYM_FOR_BACKTRACE
#include <dlfcn.h>
#endif

#if defined( __GNUC__ )
#define USE_BUILTIN_RETURN_ADDRESS  (1)
#endif


/** resolves address to the symbol name.
    If exactAddress is not NULL, address of the beginning of the
    symbol in question returned there. */
const char *getNameByAddress( void *address, void **exactAddress );

/** resolves symbol name to the address */
void  *getAddressFor( char *name );

static void *getFrame( int n );
static int isValidFrame( void *frame );
static int isLastFrame( void *frame );
static int initBfd( char *image );
static int compare_by_address( const void *symb1, const void *symb2 );
static int stackGrowth( void );
static const char *getBfdNameByAddress( void *address, void **exactAddress );
static const char *getDlNameByAddress( void *address, void **exactAddress );

#define DEBUGGER_COMMAND "gdb %s %li"

#define START_MAGIC 0xacc01adeU
#define END_MAGIC 0x1abe11edU
static struct data_s {
	unsigned long startMagic;
	void *symbolJustBeforeMain;
	void *symbolJustAfterMain;
	void *mainEntryAddress;
	unsigned int dumpCore :1;
	unsigned int attachDebugger :1;
	unsigned int stackGrowsUp :1;
	char  coredumpDir[ 4096 + 1 ];
	char  debuggerCommand[ 1024 ];
	struct {
		int tableLoaded;
		int wasBFDError;
		int tableSize;
		void **addresses;
		char **names;
	} bfd;
	unsigned long endMagic;
} data;

extern void funJustBeforeMain( void );
extern void funJustAfterMain( void );

void abendInit( int argc, char **argv )
{
	dinfo( "Initializing abend\n" );
	data.startMagic = START_MAGIC;
	data.symbolJustBeforeMain = NULL;
	data.symbolJustAfterMain = NULL;
	data.mainEntryAddress = NULL;
	data.stackGrowsUp = stackGrowth();
	data.endMagic = END_MAGIC;
#if USE_BUILTIN_RETURN_ADDRESS
	data.mainEntryAddress = __builtin_return_address( 1 );
#endif
	data.symbolJustBeforeMain = funJustBeforeMain;
	data.symbolJustAfterMain = funJustAfterMain;
	snprintf( data.debuggerCommand, sizeof data.debuggerCommand,
		  DEBUGGER_COMMAND, argv[ 0 ], ( long ) getpid() );
	strcpy( data.coredumpDir, "/tmp" );
	data.dumpCore = ULEVEL_DROP_CORE;
	data.attachDebugger = !ULEVEL_DROP_CORE;
	/* this is to avoid inlining by smart compilers */
	if( ( ( char * ) &data ) + 1 == NULL ) {
		dinfo( "dying early" );
		/* take address */
		argv[ 0 ] = ( char * ) abendInit;
		abendInit( argc - 1, argv );
		/* tail recursive function can be inlined? */
		dinfo( "hehe" );
	}
	data.bfd.tableLoaded = 0;
	data.bfd.wasBFDError = 0;
	initBfd( __prog_name );
}

void printBacktrace()
{
	int   i;
	char *frame;
  
	printk( "Stack backtrace:\n" );
	for( i = 0 ; isValidFrame( frame = getFrame( i ) ) ; ++i ) {
		char *baseAddress;
		const char *name;

		name = getNameByAddress( ( char * ) frame, 
					 ( void ** ) &baseAddress );
		if( name != NULL ) {
			printk( "%i: %p: %s %c 0x%x\n", i, frame, name,
			      ( baseAddress != NULL ) ? '+' : ' ',
			      ( baseAddress != NULL ) ? frame - baseAddress : 0 );
		}
		if( isLastFrame( frame ) ) {
			break;
		}
	}
	printk( "End backtrace.\n" );
}

static void trap_sig(int signum, 
		     siginfo_t *info UNUSED_ARG, 
		     void *datum UNUSED_ARG)
{
	panic( "Signal %i trapped\n", signum );
}

void trap_signal( int signum )
{
	struct sigaction act;

	act.sa_sigaction = trap_sig;
	act.sa_flags = SA_SIGINFO;
	if( sigaction( signum, &act, NULL ) != 0 ) {
		printk( "cannot install signal %i: %s(%i)\n",
		      signum, strerror( errno ), errno );
	}
}

void abend()
{
	int endOfEternity;

	if( ( data.startMagic == START_MAGIC ) &&
	    ( data.endMagic == END_MAGIC ) ) {
		printBacktrace();
		if( data.dumpCore ) {
			struct rlimit infiniteCore;

			/* we dont' care about errors from following calls, because
			   what can we do anyway? */
			signal( SIGABRT, SIG_DFL );
			/* this is to lay down ghosts of Linux */
			getrlimit( RLIMIT_CORE, &infiniteCore );
			infiniteCore.rlim_cur = infiniteCore.rlim_max = RLIM_INFINITY;
			setrlimit( RLIMIT_CORE, &infiniteCore );
			chdir( data.coredumpDir );
			kill( getpid(), SIGABRT );
		} else if( data.attachDebugger ) {
			if( !fork() ) {
				system( data.debuggerCommand );
				_exit( 1 );
			}
		}
		else {
			_exit( 1 );
		}
	}
	else {
		/* someone danced fandango on our core */
	}
	endOfEternity = 0;
	while( !endOfEternity )
	{;}
	/* yes, we _can_ get here. */
	abort ();
}

static void *getFrame( int n )
{
#if USE_BUILTIN_RETURN_ADDRESS
#define GET_FRAME( n ) case n: return __builtin_return_address( n )
    
	switch( n ) {
		GET_FRAME( 0 );
		GET_FRAME( 1 );
		GET_FRAME( 2 );
		GET_FRAME( 3 );
		GET_FRAME( 4 );
		GET_FRAME( 5 );
		GET_FRAME( 6 );
		GET_FRAME( 7 );
		GET_FRAME( 8 );
		GET_FRAME( 9 );
		GET_FRAME( 10 );
		GET_FRAME( 11 );
		GET_FRAME( 12 );
		GET_FRAME( 13 );
		GET_FRAME( 14 );
		GET_FRAME( 15 );
		GET_FRAME( 16 );
		GET_FRAME( 17 );
		GET_FRAME( 18 );
		GET_FRAME( 19 );
		GET_FRAME( 20 );
		GET_FRAME( 21 );
		GET_FRAME( 22 );
		GET_FRAME( 23 );
		GET_FRAME( 24 );
		GET_FRAME( 25 );
		GET_FRAME( 26 );
		GET_FRAME( 27 );
		GET_FRAME( 28 );
		GET_FRAME( 29 );
		GET_FRAME( 30 );
		GET_FRAME( 31 );
		GET_FRAME( 32 );
	default:
		return NULL;
	}
#undef GET_FRAME
#else
	void *local;

	return ( &local )[ data.stackGrowsUp ? -n : +n ];
#endif
}

static int isValidFrame( void *frame )
{
#if USE_BUILTIN_RETURN_ADDRESS
	return frame != NULL;
#else
	return 1;
#endif
}

static int isLastFrame( void *frame )
{
#if USE_BUILTIN_RETURN_ADDRESS
	if( frame == NULL ) {
		return 1;
	}
	else if( frame == data.mainEntryAddress ) {
		return 1;
	}
#else
	if( ( data.symbolJustBeforeMain <= frame ) &&
	    ( frame <= data.symbolJustAfterMain ) ) {
		return 1;
	}
#endif
	else {
		return 0;
	}
}

const char *getNameByAddress( void *address, void **exactAddress )
{
	if( address != NULL ) {
		const char *result;

		result = getBfdNameByAddress( address, exactAddress );
		if( result == NULL ) {
			result = getDlNameByAddress( address, exactAddress );
		}
		return result;
	}
	else {
		return "NIL";
	}
}

static const char *getBfdNameByAddress( void *address, void **exactAddress )
{
#if HAS_BFD
	int low;
	int high;
          
	low = 0;
	high = data.bfd.tableSize - 1;

	if( data.bfd.addresses[ high ] <= address ) {
		return NULL;
	}
	if( data.bfd.addresses[ 0 ] > address ) {
		return NULL;
	}
  
	/* binary serach */
	while( high - low > 1 ) {
		int median;
	  
		median = ( high + low ) / 2;
		if( data.bfd.addresses[ median ] > address ) {
			high = median;
		}
		else {
			low = median;
		}
 	}
	if( exactAddress != NULL ) {
		*exactAddress = data.bfd.addresses[ low ];
	}
	return data.bfd.names[ low ];
#else
	return NULL;
#endif
}

static char dlSymbolName[ 1024 ];

static const char *getDlNameByAddress( void *address, void **exactAddress )
{
#if USE_DLADDR_DLSYM_FOR_BACKTRACE
	Dl_info symbolInfo;

	if( dladdr( address, &symbolInfo ) == 0 ) {
		return NULL;
	}
	else if( symbolInfo.dli_sname != NULL ) {
		if( exactAddress != NULL ) {
			*exactAddress = symbolInfo.dli_saddr;
		}
		snprintf( dlSymbolName, sizeof dlSymbolName, "(%s@%p) %s", 
			  ( symbolInfo.dli_fname != NULL ) ? symbolInfo.dli_fname : "", 
			  symbolInfo.dli_fbase,
			  symbolInfo.dli_sname );
		return dlSymbolName;
	}
#endif
	return NULL;
}

void  *getAddressFor( char *name )
{
	void *result;

	result = NULL;
#if USE_DLADDR_DLSYM_FOR_BACKTRACE

	result = dlsym( RTLD_DEFAULT, name );
#endif
#if HAS_BFD
	if( result == NULL ) {
		int i;

		for( i = 0 ; i < data.bfd.tableSize ; ++i ) {
			if( !strcmp( name, data.bfd.names[ i ] ) ) {
				result = data.bfd.addresses[ i ];
				break;
			}
		}
	}
#endif
	return result;
}

static int initBfd( char *image )
{
#if HAS_BFD
#define BFD_ERROR( text ) bfd_perror( text ) ; printk( text )

	bfd *abfd;
	int storage_needed = 0;
	asymbol **symbol_table = ( asymbol ** ) NULL;
	int number_of_symbols;
	int i;
	int j;
    
	if( data.bfd.tableLoaded ) {
		return 1;
	}
	if( data.bfd.wasBFDError ) {
		return 0;
	}
    
	bfd_init();
	if( bfd_get_error() != bfd_error_no_error ) {
		BFD_ERROR( "bfd_init()" );
		data.bfd.wasBFDError = 1;
		return 0;
	}

	abfd = bfd_openr( image, "default" );
	if( bfd_get_error() != bfd_error_no_error ) {
		BFD_ERROR( "bfd_fdopenr()" );
		data.bfd.wasBFDError = 1;
		return 0;
	}
	if( ! bfd_check_format( abfd, bfd_object ) ) {
		BFD_ERROR( "bfd_check_format()" );
		data.bfd.wasBFDError = 1;
		return 0;
	}

	storage_needed = bfd_get_symtab_upper_bound( abfd );
     
	if( storage_needed < 0 ) {
		BFD_ERROR( "bfd_get_symtab_upper_bound(): storage_needed < 0" );
		data.bfd.wasBFDError = 1;
		return 0;
	}
	if( storage_needed == 0 ) {
		dinfo( "no symbols" );
		data.bfd.wasBFDError = 1;
		return 0;
	}
	symbol_table = malloc( sizeof( asymbol ) *storage_needed );

	number_of_symbols = bfd_canonicalize_symtab( abfd, symbol_table );
	if( number_of_symbols < 0 ) {
		BFD_ERROR( "bfd_canonicalize_symtab()" );
		free( symbol_table );
		data.bfd.wasBFDError = 1;
		return 0;
	}
     
	qsort( symbol_table, ( size_t ) number_of_symbols, 
	       sizeof symbol_table[ 0 ], compare_by_address );

	data.bfd.tableSize = 0;
	for( i = 0 ; i < number_of_symbols ; ++i ) {
		if( bfd_asymbol_bfd( symbol_table[ i ] ) == ( bfd * ) NULL ) {
			continue;
		}
		/* unnamed sections and other garbage */
		if( symbol_table[ i ] -> name[ 0 ] == ( char ) 0 ) {
			continue;
		}
		++data.bfd.tableSize;
	}
  
	data.bfd.addresses = malloc( data.bfd.tableSize * sizeof( void * ) );
	data.bfd.names = malloc( data.bfd.tableSize * sizeof( char * ) );

	dinfo( "bfd tables with %i entries allocated\n", data.bfd.tableSize );

	j = 0;
	for( i = 0 ; i < number_of_symbols ; ++i ) {
		if( bfd_asymbol_bfd( symbol_table[ i ] ) == NULL ) {
			continue;
		}
		if( symbol_table[ i ] -> name[ 0 ] == ( char ) 0 ) {
			continue;
		}
		data.bfd.addresses[ j ] =  
			( void * ) bfd_asymbol_value( symbol_table[ i ] );
		data.bfd.names[ j ] = strdup( symbol_table[ i ] -> name );
		++j;
	}
  
	free( symbol_table );
	if( ! bfd_close( abfd ) ) {
		BFD_ERROR( "bfd_close()" );
		data.bfd.wasBFDError = 1;
		/* return 1 anyway */
	}
	data.bfd.tableLoaded = 1;
#undef BFD_ERROR
#endif
	return 1;
}

static int compare_by_address( const void *symb1, const void *symb2 )
{
#if HAS_BFD
	return( bfd_asymbol_value( *( asymbol ** ) symb1 ) - 
		bfd_asymbol_value( *( asymbol ** ) symb2 ) );
#else
	return 1;
#endif
}

static int stackGrowthCalled( int *localOfTheCaller )
{
	int bar;

	return &bar > localOfTheCaller;
}

static int stackGrowth()
{
	int foo;
	return stackGrowthCalled( &foo );
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
