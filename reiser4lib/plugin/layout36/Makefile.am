includedir              = -I$(top_srcdir)/include

lib_LTLIBRARIES         = liblayout36.la
liblayout36_la_LDFLAGS  = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	                      -release $(LT_RELEASE)

liblayout36_la_LIBADD   = $(top_builddir)/libaal/libaal.la

liblayout36_la_SOURCES  = layout36.c layout36.h

INCLUDES = $(includedir)


