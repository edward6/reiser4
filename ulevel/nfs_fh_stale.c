/* -*- C -*- */

/* cc -o nfs -O3 -Wformat -g nfs_fh_stale.c -pthread */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>


extern char *optarg;
extern int optind, opterr, optopt;

typedef enum { ok = 0, 
	       wrong_argc, 
	       pthread_create_error } ret_t;

typedef struct stats
{
	pthread_mutex_t lock;
	unsigned long opens;
	unsigned long lseeks;
	unsigned long reads;
	unsigned long writes;
	unsigned long renames;
	unsigned long mrenames;
	unsigned long unlinks;
	unsigned long munlinks;
	unsigned long truncates;
	unsigned long mtruncates;
	unsigned long fsyncs;
	unsigned long errors;
	unsigned long naps;
	unsigned long total;
	unsigned long done;
	struct timeval start;
	struct timeval end;
} stats_t;

stats_t stats;

static void *worker( void * arg );
static void sync_file( char *fileName );
static void read_file( char *fileName );
static void write_file( char *fileName );
static void rename_file( int files, char *fileName );
static void unlink_file( char *fileName );
static void truncate_file( char *fileName );
static void nap( int secs, int nanos );
static void _nap( int secs, int nanos );

pthread_key_t  buffer_key;

int delta = 1;
int max_sleep = 0;
int max_buf_size = 4096 * 100;
int max_size = 4096 * 100 * 1000;
int verbose = 0;

#define RND( n ) \
 ( ( int ) ( ( ( double ) ( n ) ) * rand() / ( RAND_MAX + 1.0 ) ) )

#define STEX( e ) \
 pthread_mutex_lock( &stats.lock ) ; e ; pthread_mutex_unlock( &stats.lock )

const char optstring[] = "p:f:d:i:s:b:M:B:v";

static double rate( unsigned long events, int secs )
{
	return ( ( double ) events ) / secs;
}

static void usage( char *argv0 )
{
	fprintf( stderr, 
		 "usage: %s %s\n"
		 "\n\tRandomly creates and removes files from multiple threads."
		 "\n\tCan be used to test NFS stale handle detection."
		 "\n\tCompiled from " __FILE__ " at " __DATE__ "\n\n", 
		 argv0, optstring );
}

int benchmark = 0;

int main( int argc , char **argv )
{
	ret_t result;
	int threads = 10;
	int files = 100;
	int i;
	int iterations = 10;
	int opt;

	result = ok;
	do
		{
			opt = getopt( argc, argv, optstring );
			switch( opt )
				{
				case '?':
					usage( argv[ 0 ] );
					return wrong_argc;
				case 'p':
					threads = atoi( optarg );
					break;
				case 'f':
					files = atoi( optarg );
					break;
				case 'd':
					delta = atoi( optarg );
					break;
				case 'i':
					iterations = atoi( optarg );
					break;
				case 's':
					max_sleep = atoi( optarg );
					break;
				case 'b':
					max_buf_size = atoi( optarg );
					if( max_buf_size < 255 )
						{
							fprintf( stderr, "%s: max_buf_size too small\n", argv[ 0 ] );
							return 1;
						}
					break;
				case 'M':
					max_size = atoi( optarg );
					break;
				case 'B':
					benchmark = 1;
					break;
				case 'v':
					++ verbose;
				case -1:
					break;
				}
		}
	while( opt != -1 );
	stats.total = iterations;
	stats.done = 0;
	if( gettimeofday( &stats.start, NULL ) != 0 )
		{
			perror( "gettimeofday" );
			return 1;
		}
	pthread_mutex_init( &stats.lock, NULL );
	pthread_key_create( &buffer_key, free );
	fprintf( stderr, 
		 "%s: %i processes, %i files, delta: %i"
		 "\n\titerations: %i, sleep: %i, buffer: %i, max size: %i\n",
		 argv[ 0 ], threads, files, delta, iterations, 
		 max_sleep, max_buf_size, max_size );
	for( i = 0 ; i < threads ; ++i )
		{
			int rv;
			pthread_t id;

			rv = pthread_create( &id, NULL, worker, ( void * ) files );
			if( rv != 0 )
				{
					fprintf( stderr, "%s: pthread_create fails: %s(%i) while creating %i-%s thread\n",
						 argv[ 0 ], strerror( rv ), rv, i,
						 ( i % 10 == 1 ) ? "st" : ( i % 10 == 2 ) ? "nd" :"th" );
					return 1;
				}
		}
	if( !benchmark )
		{
			for( i = 0 ; i < iterations ; i += delta )
				{
					printf( "\nseconds: %i"
						"\n\topens: %lu [%f]"
						"\n\tlseeks: %lu [%f]"
						"\n\treads: %lu [%f], writes: %lu [%f]"
						"\n\trenames: %lu/%lu [%f]"
						"\n\tunlinks: %lu/%lu [%f]"
						"\n\ttruncate: %lu/%lu [%f]"
						"\n\tfsyncs: %lu [%f]"
						"\n\terrors: %lu, naps: %lu [%f]\n",
						i,
						stats.opens, rate( stats.opens, i ), 
						stats.lseeks, rate( stats.lseeks, i ), 
						stats.reads, rate( stats.reads, i ),
						stats.writes, rate( stats.writes, i ),
						stats.renames, stats.mrenames, rate( stats.renames, i ),
						stats.unlinks, stats.munlinks, rate( stats.unlinks, i ), 
						stats.truncates, stats.mtruncates, rate( stats.truncates, i ),
						stats.fsyncs, rate( stats.fsyncs, i ),
						stats.errors, stats.naps, rate( stats.naps, i ) );
					nap( delta, 0 );
				}
		}
	else
		{
			while( 1 )
				{
					_nap( 1, 0 );
				}
		}
	return result;
}

static void *worker( void * arg )
{
	int files;

	files = ( int ) arg;
	pthread_setspecific( buffer_key, malloc( max_buf_size ) );
	while( 1 )
		{
			char fileName[ 30 ];

			sprintf( fileName, "%x", RND( files ) );
			if( rand() < RAND_MAX / 6 )
				{
					sync_file( fileName );
				}
			else if( rand() <  RAND_MAX / 5 )
				{
					read_file( fileName );
				}
			else if( rand() < RAND_MAX / 4 )
				{
					write_file( fileName );
				}
			else if( rand() < RAND_MAX / 3 )
				{
					rename_file( files, fileName );
				}
			else if( rand() < RAND_MAX / 2 )
				{
					unlink_file( fileName );
				}
			else
				{
					truncate_file( fileName );
				}
			STEX( ++stats.done );
			if( !benchmark )
				nap( 0, RND( max_sleep ) );
			else if( stats.done >= stats.total )
				{
					pthread_mutex_lock( &stats.lock );
					gettimeofday( &stats.end, NULL );
					printf( "start: %li.%li, end: %li.%li, diff: %li, %li\n",
						stats.start.tv_sec, stats.start.tv_usec,
						stats.end.tv_sec, stats.end.tv_usec,
						stats.end.tv_sec - stats.start.tv_sec,
						stats.end.tv_usec - stats.start.tv_usec );
					exit( 0 );
				}
		}
}

static void sync_file( char *fileName )
{
	int fd;

	fd = open( fileName, O_WRONLY );
	if( fd == -1 )
		{
			if( errno != ENOENT )
				{
					fprintf( stderr, "%s open: %s(%i)\n", fileName, strerror( errno ), errno );
					STEX( ++stats.errors );
				}
			return;
		}
	if( fsync( fd ) )
		{
			fprintf( stderr, "%s sync: %s(%i)\n", fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			return;
		}
	STEX( ++stats.fsyncs );
	if( verbose )
		{
			printf( "[%li] SYNC: %s\n", pthread_self(), fileName );
		}
	close( fd );
}

static void read_file( char *fileName )
{
	int fd;
	char *buf;
	int bufSize;
	int offset;

	fd = open( fileName, O_CREAT | O_APPEND | O_RDWR, 0700 );
	if( fd == -1 )
		{
			fprintf( stderr, "%s open: %s(%i)\n", fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			return;
		}
	STEX( ++stats.opens );
	nap( 0, RND( max_sleep ) );
	if( lseek( fd, RND( max_size ), SEEK_SET ) == -1 )
		{
			fprintf( stderr, "%s lseek: %s(%i)\n", 
				 fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			close( fd );
			return;
		}
	STEX( ++stats.lseeks );
	nap( 0, RND( max_sleep ) );
	bufSize = RND( max_buf_size / 3 ) + 30;
	offset = RND( max_buf_size / 3 );
	buf = pthread_getspecific( buffer_key );
	if( read( fd, buf, bufSize + offset ) == -1 )
		{
			fprintf( stderr, "%s read: %s(%i)\n", fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			close( fd );
			return;
		}
	STEX( ++stats.reads );
	if( verbose )
		{
			printf( "[%li] R: %s\n", pthread_self(), fileName );
		}
	close( fd );
}

static void write_file( char *fileName )
{
	int fd;
	char *buf;
	int bufSize;
	int offset;

	fd = open( fileName, O_CREAT | O_APPEND | O_RDWR, 0700 );
	if( fd == -1 )
		{
			fprintf( stderr, "%s open: %s(%i)\n", fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			return;
		}
	STEX( ++stats.opens );
	nap( 0, RND( max_sleep ) );
	if( lseek( fd, RND( max_size ), SEEK_SET ) == -1 )
		{
			fprintf( stderr, "%s lseek: %s(%i)\n", 
				 fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			close( fd );
			return;
		}
	STEX( ++stats.lseeks );
	nap( 0, RND( max_sleep ) );
	bufSize = RND( max_buf_size / 3 ) + 30;
	offset = RND( max_buf_size / 3 );
	buf = pthread_getspecific( buffer_key );
	memset( buf, 0xfe + stats.writes, max_buf_size );
	sprintf( buf + offset, "---%lx+++", time( NULL ) );
	if( write( fd, buf, bufSize + offset ) == -1 )
		{
			fprintf( stderr, "%s write: %s(%i)\n", 
				 fileName, strerror( errno ), errno );
			STEX( ++stats.errors );
			close( fd );
			return;
		}
	STEX( ++stats.writes );
	if( verbose )
		{
			printf( "[%li] W: %s\n", pthread_self(), fileName );
		}
	close( fd );
}

static void rename_file( int files, char *fileName )
{
	char target[ 30 ];

	sprintf( target, "%x", RND( files ) );
	if( rename( fileName, target ) == -1 )
		{
			switch( errno )
				{
				case ENOENT:
					STEX( ++stats.mrenames );
					break;
				default:
					{
						fprintf( stderr, "rename( %s, %s ): %s(%i)\n", 
							 fileName, target, strerror( errno ), errno );
						STEX( ++stats.errors );
					}
				}
		}
	else
		{
			if( verbose )
				{
					printf( "[%li] %s -> %s\n", pthread_self(), fileName, target );
				}
			STEX( ++stats.renames );
		}
}

static void unlink_file( char *fileName )
{
	if( unlink( fileName ) == -1 )
		{
			switch( errno )
				{
				case ENOENT:
					STEX( ++stats.munlinks );
					break;
				default:
					{
						fprintf( stderr, "%s unlink: %s(%i)\n", 
							 fileName, strerror( errno ), errno );
						STEX( ++stats.errors );
					}
				}
		}
	else
		{
			if( verbose )
				{
					printf( "[%li] U: %s\n", pthread_self(), fileName );
				}
			STEX( ++stats.unlinks );
		}
}

static void truncate_file( char *fileName )
{
	if( truncate( fileName, RND( max_size ) ) == -1 )
		{
			switch( errno )
				{
				case ENOENT:
					STEX( ++stats.mtruncates );
					break;
				default:
					{
						fprintf( stderr, "%s truncate: %s(%i)\n", 
							 fileName, strerror( errno ), errno );
						STEX( ++stats.errors );
					}
				}
		}
	else
		{
			if( verbose )
				{
					printf( "[%li] T: %s\n", pthread_self(), fileName );
				}
			STEX( ++stats.truncates );
		}
}

static void nap( int secs, int nanos )
{
	if( !benchmark )
		_nap( secs, nanos );
}

static void _nap( int secs, int nanos )
{
	if( ( secs > 0 ) || ( nanos > 0 ) )
		{
			struct timespec delay;
	  
			delay.tv_sec = secs;
			delay.tv_nsec = nanos;

			if( nanosleep( &delay, NULL ) == -1 )
				{
					fprintf( stderr, "nanosleep: %s(%i)\n", strerror( errno ), errno );
				}
			STEX( ++stats.naps );
		}
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 79
  End:
*/
