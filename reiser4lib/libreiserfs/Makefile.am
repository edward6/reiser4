LIBS			= @LIBS@

includedir		= -I$(top_srcdir)/include

lib_LTLIBRARIES		= libreiserfs.la
libreiserfs_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE) \
			  $(LIBREISERFS_LDFLAGS)

if ENABLE_MONOLITHIC
libreiserfs_la_LIBADD  	= $(top_builddir)/libaal/libaal.la \
			  $(top_builddir)/plugin/format36/libformat36-static.la \
			  $(top_builddir)/plugin/format40/libformat40-static.la \
			  $(top_builddir)/plugin/journal36/libjournal36-static.la \
			  $(top_builddir)/plugin/journal40/libjournal40-static.la \
			  $(top_builddir)/plugin/alloc36/liballoc36-static.la \
			  $(top_builddir)/plugin/alloc40/liballoc40-static.la \
			  $(top_builddir)/plugin/node40/libnode40-static.la
else
libreiserfs_la_LIBADD  	= $(top_builddir)/libaal/libaal.la @LIBREISERFS_LIBS@
endif

libreiserfs_la_SOURCES  = libreiserfs.c filesystem.c super.c journal.c \
			  alloc.c tools.c plugin.c node.c path.c item.c \
			  tree.c hack.c key.c

INCLUDES = $(includedir)

