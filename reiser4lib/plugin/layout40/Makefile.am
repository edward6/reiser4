includedir              = -I$(top_srcdir)/include

lib_LTLIBRARIES         = liblayout40.la
liblayout40_la_LDFLAGS  = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	                      -release $(LT_RELEASE)

liblayout40_la_LIBADD   = $(top_builddir)/libaal/libaal.la

liblayout40_la_SOURCES  = layout40.c layout40.h

INCLUDES = $(includedir)


