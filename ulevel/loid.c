#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define MAX_LEN   (20)
#define CYCLE     (20000)

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

  if( argc == 2 )
	{
	  N = atol( argv[ 1 ] );
	}
  else
	{
	  N = 0;
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
	  for( j = min ; j < MAX_LEN ; ++ j )
		{
		  fname[ j - min ] = alphabet[ ( int ) name[ j ] ];
		}
	  fname[ MAX_LEN - min ] = 0;
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
	  if( ( i % CYCLE ) == 0 )
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
