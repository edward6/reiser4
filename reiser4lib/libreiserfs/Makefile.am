LIBS			= @LIBS@

includedir		= -I$(top_srcdiq)/include

lib_LTLIBRARIES		= libreiserfs.la
libreiserfs_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

if ENABLE_MONOLITIC
libreiserfs_la_LIBADD  	= $(top_builddir)/libaal/libaal.la \
			  $(top_builddir)/plugin/format40/libformat40.la \
			  $(top_builddir)/plugin/journal40/libjournal40.la \
			  $(top_builddir)/plugin/alloc40/liballoc40.la \
			  $(top_builddir)/plugin/node40/libnode40.la
else
libreiserfs_la_LIBADD  	= $(top_builddir)/libaal/libaal.la @LIBREISERFS_LIBS@
endif

libreiserfs_la_SOURCES  = libreiserfs.c filesystem.c super.c journal.c \
			  alloc.c tree.c tools.c plugin.c node.c

INCLUDES = $(includedir)

