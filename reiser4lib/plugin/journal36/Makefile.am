includedir              = -I$(top_srcdir)/include

libdir					= $(plugindir)

lib_LTLIBRARIES         = libjournal36.la
libjournal36_la_LDFLAGS  = -module -avoid-version

libjournal36_la_LIBADD   = $(top_builddir)/libaal/libaal.la

libjournal36_la_SOURCES  = journal36.c journal36.h

INCLUDES = $(includedir)


