includedir              = -I$(top_srcdir)/include

libdir					= $(plugindir)

lib_LTLIBRARIES         = libjournal40.la
libjournal40_la_LDFLAGS  = -module -avoid-version

libjournal40_la_LIBADD   = $(top_builddir)/libaal/libaal.la

libjournal40_la_SOURCES  = journal40.c journal40.h

INCLUDES = $(includedir)


