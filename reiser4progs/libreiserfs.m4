# This file is a part of libreiserfs
# Copyright (C) 1996 - 2002 Hans Reiser

dnl Usage:
dnl AC_CHECK_LIBREISERFS([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl
dnl Example:
dnl AC_CHECK_LIBREISERFS(0.3.0, , [AC_MSG_ERROR([*** libreiserfs >= 0.3.0 not installed - please install first ***])])
dnl
dnl Adds the required libraries to $REISERFS_LIBS and does an
dnl AC_SUBST(REISERFS_LIBS)

AC_DEFUN(AC_CHECK_LIBREISERFS,
[

dnl save LIBS
saved_LIBS="$LIBS"

dnl Check for headers and library
AC_CHECK_HEADER(reiserfs/reiserfs.h, ,
	[AC_MSG_ERROR([<reiserfs/reiserfs.h> not found; install libreiserfs])] 
$3)

AC_CHECK_LIB(reiserfs,reiserfs_fs_open, ,
	[AC_MSG_ERROR([libreiserfs not found; install libreiserfs available at \
	http://www.namesys.com])]
$3)

AC_MSG_CHECKING(for libreiserfs - version >= $1)

AC_TRY_LINK_FUNC(libreiserfs_get_version,,
	AC_MSG_RESULT(failed)
	AC_MSG_ERROR([*** libreiserfs can't execute test ***]))

dnl Get major, minor, and micro version from arg MINIMUM-VERSION
libreiserfs_config_major_version=`echo $1 | \
    sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
libreiserfs_config_minor_version=`echo $1 | \
    sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
libreiserfs_config_micro_version=`echo $1 | \
    sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`

dnl Compare MINIMUM-VERSION with libreiserfs version
AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <reiserfs/reiserfs.h>

int main(){
	int	major, minor, micro;
	const char *version;    
	
	if ( !(version = libreiserfs_get_version()) )
		exit(1);
		
	if (sscanf(version, "%d.%d.%d", &major, &minor, &micro) != 3) {
		printf("%s, bad version string\n", version);
		exit(1);
	}
	
	if ((major >= $libreiserfs_config_major_version) ||
	   ((major == $libreiserfs_config_major_version) && 
	   (minor >= $libreiserfs_config_minor_version)) ||
	   ((major == $libreiserfs_config_major_version) && 
	   (minor == $libreiserfs_config_minor_version) && 
	   (micro >= $libreiserfs_config_micro_version))) 
	{
		return 0;
	} else {
		printf("\n*** An old version of libreiserfs (%s) was found.\n",
		       version);
		printf("*** You need a version of libreiserfs newer than %d.%d.%d.\n",
			$libreiserfs_config_major_version, 
			$libreiserfs_config_minor_version,
			$libreiserfs_config_micro_version);
		printf("*** You can get it at - http://www.namesys.com\n");
		return 1;
	}
}
], 
	AC_MSG_RESULT(yes),
	AC_MSG_RESULT(no) ; $3,
	[echo $ac_n "cross compiling; assumed OK... $ac_c"])

dnl restore orignial LIBS and set @REISERFS_LIBS@
REISERFS_LIBS="$LIBS"
LIBS="$saved_LIBS"
AC_SUBST(REISERFS_LIBS)

dnl Execute ACTION-IF-FOUND
$2

])

