includedir              = -I$(top_srcdir)/include

libdir					= $(plugindir)

lib_LTLIBRARIES         = liballoc40.la
liballoc40_la_LDFLAGS  = -module -avoid-version

liballoc40_la_LIBADD   = $(top_builddir)/libaal/libaal.la

liballoc40_la_SOURCES  = alloc40.c alloc40.h

INCLUDES = $(includedir)

