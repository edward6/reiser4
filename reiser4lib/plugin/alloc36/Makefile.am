includedir              = -I$(top_srcdir)/include

libdir					= $(plugindir)

lib_LTLIBRARIES         = liballoc36.la
liballoc36_la_LDFLAGS  = -module -avoid-version

liballoc36_la_LIBADD   = $(top_builddir)/libaal/libaal.la

liballoc36_la_SOURCES  = alloc36.c alloc36.h

INCLUDES = $(includedir)

