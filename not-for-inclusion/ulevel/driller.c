#include <unistd.h>
#include <stdio.h>

static void print_percentage( unsigned long reached,
			      unsigned long total, int gap )
{
	int percentage;

	percentage = reached / ( ( ( double ) total ) / 100.0 );
	if( percentage * ( total / 100 ) == reached )
	{
		if( ( percentage / 10 ) * 10 == percentage )
		{
			printf( "%i%%", percentage );
		}
		else if( percentage % 2 == 0 )
		{
			printf( "%c", gap );
		}
		fflush( stdout );
	}
}

int main( int argc, char **argv )
{
	int depth;
	int i;

	depth = atoi( argv[ 1 ] );
	for( i = 0 ; i < depth ; ++ i ) {
		char dname[ 100 ];

		sprintf( dname, "%i", i );
		if( mkdir( dname, 0700 ) != 0 ) {
			perror( "mkdir" );
			return 1;
		} else if( chdir( dname ) != 0 ) {
			perror( "chdir" );
			return 2;
		}
		print_percentage( i, depth, '.' );
	}
	printf( "\nDone.\n" );
	return 0;
}
