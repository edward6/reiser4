LIBS			= @LIBS@

includedir		= -I$(top_srcdir)/include

lib_LTLIBRARIES		= libreiser4.la

noinst_LTLIBRARIES	= libplugins_header.la libplugins_footer.la

libreiser4_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

if ENABLE_MONOLITHIC
libreiser4_la_LIBADD  	= $(top_builddir)/libaal/libaal.la \
			  $(top_builddir)/libmisc/libmisc.la \
			  libplugins_header.la \
			  $(top_builddir)/plugin/format40/libformat40-static.la \
			  $(top_builddir)/plugin/journal40/libjournal40-static.la \
			  $(top_builddir)/plugin/alloc40/liballoc40-static.la \
			  $(top_builddir)/plugin/oid40/liboid40-static.la \
			  $(top_builddir)/plugin/node40/libnode40-static.la \
			  $(top_builddir)/plugin/internal40/libinternal40-static.la \
			  $(top_builddir)/plugin/direntry40/libdirentry40-static.la \
			  $(top_builddir)/plugin/stat40/libstat40-static.la \
			  libplugins_footer.la
else
libreiser4_la_LIBADD  	= $(top_builddir)/libaal/libaal.la @LIBREISERFS_LIBS@
endif

libreiser4_la_SOURCES  = libreiser4.c filesystem.c format.c journal.c \
			 alloc.c oid.c tools.c plugin.c node.c tree.c \
			 hack.c key.c

libplugins_header_la_SOURCES = plugins_header.c
libplugins_footer_la_SOURCES = plugins_footer.c

INCLUDES = $(includedir)

