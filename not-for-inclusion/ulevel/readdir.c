#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>

int main( int argc, char **argv )
{
  DIR             *directory;
  struct dirent64 *dentry;
  int              do_stat;

  directory = opendir( argv[ 1 ] );
  do_stat   = atoi( argv[ 2 ] );
  if( directory != NULL )
	{
	  while( ( dentry = readdir64( directory ) ) != NULL )
		{
		  puts( dentry -> d_name );
		  if( do_stat ) {
			struct stat buf;

			if( stat( dentry -> d_name, &buf ) == -1 )
			  {
				perror( "stat" );
				break;
			  }
		  }
		}
	  if( errno != 0 )
		  perror( "readdir" );
	  closedir( directory );
    }
  else
	{
	  perror( "cannot opendir" );
	}
}
