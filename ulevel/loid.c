#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define MAX_LEN   (20)

#define RAT( a, b ) ( ( ( double ) ( a ) ) / ( ( double ) ( b ) ) )
int main( int argc, char **argv )
{
  const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  unsigned long i;
  char name[ MAX_LEN + 1 ];
  int min;
  int base;
  time_t start;
  time_t instant;
  unsigned long prev;
  unsigned long N;
  int ebusy;
  int ch;
  int pad;
  unsigned long cycle;

  N = 0;
  pad = 0;
  cycle = 20000;
  while( ( ch = getopt( argc, argv, "n:p:c:" ) ) != -1 )
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
		default:
		  exit( 0 );
		}
	}

  base = strlen( alphabet );
  memset( name, base + 10, MAX_LEN + 1 );
  ebusy = 0;
  prev = 0;
  instant = start = time( NULL );

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
	  fd = open( fname, O_CREAT, 0777 );
	  if( fd == -1 )
		{
		  if( errno != EBUSY )
			{
			  perror( "open" );
			  printf( "%li files created\n", i );
			  exit( 2 );
			}
		  else
			{
			  ebusy ++;
			}
		}
	  close( fd );
	  if( ( i % cycle ) == 0 )
		{
		  time_t now;

		  now = time( NULL );
		  printf( "%li files: %li (%f/%f), %i: %s\n", i, time( NULL ) - instant,
				  ( now - start ) ? RAT( i, now - start ) : 0.0, 
				  ( now - instant ) ? RAT( i - prev, now - instant ) : 0.0,
				  ebusy, fname );
		  instant = time( NULL );
		  prev = i;
		}
	}
}
