includedir		     = -I$(top_srcdir)/include

libreiser4_sources	     = libreiser4.c filesystem.c format.c journal.c \
			       alloc.c oid.c factory.c node.c tree.c key.c \
			       object.c coord.c cache.c dir.c master.c item.c

lib_LTLIBRARIES		     = libreiser4.la

noinst_LTLIBRARIES	     = libreiser4-static.la 

libreiser4_la_LDFLAGS	     = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

if ENABLE_MONOLITHIC
libreiser4_la_LIBADD	     = $(top_builddir)/libaal/libaal.la \
			       $(top_builddir)/libcomm/libcomm-static.la \
			       $(top_builddir)/plugin/libreiser4-plugin.la
else
libreiser4_la_LIBADD	     = $(top_builddir)/libaal/libaal.la @LIBREISER4_LIBS@
endif

libreiser4_la_SOURCES	     = $(libreiser4_sources)

libreiser4_static_la_LIBADD  = $(top_builddir)/libaal/libaal-static.la \
			       $(top_builddir)/libcomm/libcomm-static.la \
			       $(top_builddir)/plugin/libreiser4-plugin.la

libreiser4_static_la_SOURCES = $(libreiser4_sources)

INCLUDES = $(includedir)
