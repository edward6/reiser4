LIBS 					= @LIBS@

includedir 				= -I$(top_srcdir)/include

lib_LTLIBRARIES	     	= libreiserfs.la
libreiserfs_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
						  -release $(LT_RELEASE)

libreiserfs_la_LIBADD  	= $(top_builddir)/libaal/libaal.la @LIBREISERFS_LIBS@

libreiserfs_la_SOURCES  = libreiserfs.c filesystem.c super.c journal.c \
						  alloc.c tree.c tools.c debug.c plugin.c

INCLUDES = $(includedir)

