LIBS 					= @LIBS@

includedir 				= -I$(top_srcdir)/include

lib_LTLIBRARIES	     	= libreiserfs.la
libreiserfs_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
						  -release $(LT_RELEASE)

libreiserfs_la_LIBADD  	= $(top_builddir)/libagl/libagl.la @LIBREISERFS_LIBS@

libreiserfs_la_SOURCES  = libreiserfs.c tools.c debug.c plugin.c list.c

INCLUDES = $(includedir)

