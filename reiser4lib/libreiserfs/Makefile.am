LIBS 					= @LIBS@

includedir 				= -I$(top_srcdir)/include

lib_LTLIBRARIES	     	= libreiserfs.la
libreiserfs_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
						  -release $(LT_RELEASE)

libreiserfs_la_LIBADD  	= $(top_builddir)/libdal/libdal.la @LIBREISERFS_LIBS@

libreiserfs_la_SOURCES  = libreiserfs.c exception.c tools.c debug.c

INCLUDES = $(includedir)

