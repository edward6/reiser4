librepair_sources            = filesystem.c format.c node.c 

lib_LTLIBRARIES		     = librepair.la

noinst_LTLIBRARIES	     = librepair-static.la

librepair_la_LDFLAGS	     = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

librepair_la_LIBADD	     = $(top_builddir)/libreiser4/libreiser4.la

librepair_la_SOURCES	     = $(librepair_sources)

librepair_static_la_LIBADD   = $(top_builddir)/libreiser4/libreiser4-static.la

librepair_static_la_SOURCES  = $(librepair_sources)

INCLUDES = -I$(top_srcdir)/include

