# This file is a part of reiser4progs package
# Copyright (C) 1996-2002 Hans Reiser

dnl Usage:
dnl AC_CHECK_LIBREISER4([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl
dnl Example:
dnl AC_CHECK_LIBREISER4(0.3.0, , [AC_MSG_ERROR([libreiser4 >= 0.3.0 not installed - please install first])])
dnl
dnl Adds the required libraries to $REISER4_LIBS and does an
dnl AC_SUBST(REISER4_LIBS)

AC_DEFUN(AC_CHECK_LIBREISER4,
[

dnl save LIBS
saved_LIBS="$LIBS"

dnl Check for headers and library
AC_CHECK_HEADER(reiser4/reiser4.h, ,
    [AC_MSG_ERROR([<reiser4/reiser4.h> not found; install reiser4progs])] 
$3)

AC_CHECK_LIB(reiser4, reiserfs_fs_init, ,
    [AC_MSG_ERROR([libreiser4 not found; install reiser4progs available at \
    http://www.namesys.com/pub/reiser4progs])]
$3)

AC_MSG_CHECKING(for libreiser4 - version >= $1)

AC_TRY_LINK_FUNC(libreiser4_version,,
    AC_MSG_RESULT(failed)
    AC_MSG_ERROR([libreiser4 can't execute test]))

dnl Get major, minor, and micro version from arg MINIMUM-VERSION
libreiser4_config_major_version=`echo $1 | \
    sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
libreiser4_config_minor_version=`echo $1 | \
    sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
libreiser4_config_micro_version=`echo $1 | \
    sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`

dnl Compare MINIMUM-VERSION with libreiser4 version
AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <reiser4/reiser4.h>

int main() {
    int major, minor, micro;
    const char *version;    
	
    if ( !(version = libreiser4_version()) )
	exit(1);
		
    if (sscanf(version, "%d.%d.%d", &major, &minor, &micro) != 3) {
	printf("%s, bad version string\n", version);
	exit(1);
    }
	
    if ((major >= $libreiser4_config_major_version) ||
	((major == $libreiser4_config_major_version) && 
	(minor >= $libreiser4_config_minor_version)) ||
	((major == $libreiser4_config_major_version) && 
	(minor == $libreiser4_config_minor_version) && 
	(micro >= $libreiser4_config_micro_version))) 
    {
	return 0;
    } else {
	printf("\nAn old version of libreiser4 (%s) was found.\n",
	    version);
	printf("You need a version of libreiser4 newer than %d.%d.%d.\n",
	    $libreiser4_config_major_version, 
	    $libreiser4_config_minor_version,
	    $libreiser4_config_micro_version);
	printf("You can get it at - http://www.namesys.com/pub/reiser4progs\n");
	return 1;
    }
}
], 
    AC_MSG_RESULT(yes),
    AC_MSG_RESULT(no) ; $3,
    [echo $ac_n "cross compiling; assumed OK... $ac_c"])

dnl restore orignial LIBS and set @REISER4_LIBS@
REISER4_LIBS="$LIBS"
LIBS="$saved_LIBS"
AC_SUBST(REISER4_LIBS)

dnl Execute ACTION-IF-FOUND
$2

])

