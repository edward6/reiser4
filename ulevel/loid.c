#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#define MAX_LEN   (2000)

static double RAT( unsigned long long a, unsigned long long b )
{
  if( b == 0 )
	return 0.0;
  else
	return ( ( double ) a ) / b;
}

static unsigned long long tdiff(struct timeval *t1, struct timeval *t2)
{
  return (t1->tv_sec - t2->tv_sec) * 1000000 + (t1->tv_usec - t2->tv_usec);
}

int main( int argc, char **argv )
{
  const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  unsigned long long i;
  char name[ MAX_LEN + 1 ];
  int min;
  int base;
  struct timeval start;
  struct timeval instant;
  unsigned long prev;
  unsigned long N;
  int ch;
  int pad;
  unsigned long cycle;
  int dodirs;
  int writelen;
  int reverse;
  char *buf;

  N = 0;
  pad = 0;
  dodirs = 0;
  cycle = 20000;
  writelen = 0;
  buf = 0;
  while( ( ch = getopt( argc, argv, "dn:p:c:w:r" ) ) != -1 )
	{
	  switch( ch )
		{
		case 'n':
		  N = atol( optarg );
		  break;
		case 'p':
		  pad = atoi( optarg );
		  break;
		case 'c':
		  cycle = atol( optarg );
		  break;
		case 'd':
		  dodirs = 1;
		  break;
		case 'w':
		  writelen = atoi( optarg );
		  break;
		case 'r':
		  reverse = 1;
		  break;
		default:
		  exit( 0 );
		}
	}

  base = strlen( alphabet );
  memset( name, base + 10, MAX_LEN + 1 );
  prev = 0;
  gettimeofday( &instant, 0 );
  start = instant;

  if( writelen > 0 )
	{
	  if( dodirs )
		{
		  fprintf( stderr, "Cannot write into directories\n" );
		  exit( 1 );
		}
	  buf = ( char * )malloc( writelen );
	  if( buf == NULL )
		{
		  perror( "malloc" );
		  exit( 1 );
		}
	}

  for( i = 0 ; N && i < N ; ++ i )
	{
	  int j;
	  int c;
	  int fd;
	  int shift;
	  char fname[ MAX_LEN + 1 ];

	  for( j = MAX_LEN - 1, c = 1 ; ( j >= 0 ) && c ; -- j )
		{
		  c = 0;
		  if( name[ j ] == base + 10 )
			{
			  name[ j ] = 1;
			  min = j;
			}
		  else if( name[ j ] == base - 1 )
			{
			  c = 1;
			  name[ j ] = 0;
			}
		  else
			{
			  name[ j ] ++;
			}
		}
	  if( c == 1 )
		{
		  exit( 1 );
		}
	  if( pad )
		{
		  shift = pad - ( MAX_LEN - min );
		  if( shift < 0 )
			{
			  shift = 0;
			}
		  for( j = 0 ; j < shift ; ++ j )
			{
			  fname[ j ] = '#';
			}
		}
	  else
		{
		  shift = 0;
		}
	  for( j = min ; j < MAX_LEN ; ++ j )
		{
		  fname[ j - min + shift ] = alphabet[ ( int ) name[ j ] ];
		}
	  fname[ MAX_LEN - min + shift ] = 0;
	  if( reverse ) 
		{
		  int len;

		  len = MAX_LEN - min + shift;
		  for( j = 0 ; j < len / 2 + 1 ; ++ j )
			{
			  char swap;

			  swap = fname[ j ];
			  fname[ j ] = fname[ len - j - 1 ];
			  fname[ len - j - 1 ] = swap;
			}
		}
	  if( dodirs )
		{
		  fd = mkdir( fname, 0744 );
		}
	  else
		{
		  fd = open( fname, O_CREAT | O_WRONLY, 0444 );
		}
	  if( fd == -1 )
		{
		  perror( "open" );
		  printf( "%li files created\n", i );
		  exit( 2 );
		}
	  if( !dodirs )
		{
		  if( writelen > 0 )
			{
			  int result;

			  if( write( fd, buf, writelen ) != writelen )
				{
				  perror( "write" );
				  exit( 3 );
				}
			}
		  close( fd );
		}
	  if( ( i % cycle ) == 0 )
		{
		  struct timeval now;

		  gettimeofday( &now, 0 );
		  printf( "%lli\t files: %lli (%f/%f), %s\n", i, 
				  tdiff( &now, &instant ),
				  RAT( i * 1000000, tdiff( &now, &start ) ), 
				  RAT( ( i - prev ) * 1000000, tdiff( &now, &instant ) ),
				  fname );
		  gettimeofday( &instant, 0 );
		  prev = i;
		}
	}
  if( buf != NULL )
	free( buf );
}
