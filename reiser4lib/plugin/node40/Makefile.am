includedir              = -I$(top_srcdir)/include

libdir			= $(plugindir)

lib_LTLIBRARIES 	= libnode40.la
libnode40_la_LDFLAGS 	= -module -avoid-version

libnode40_la_LIBADD 	= $(top_builddir)/libaal/libaal.la $(top_builddir)/libmisc/libmisc.la

libnode40_la_SOURCES 	= node40.c node40.h

INCLUDES = $(includedir)

