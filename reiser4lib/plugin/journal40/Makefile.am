includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

journal40_sources		= journal40.c journal40.h

lib_LTLIBRARIES			= libjournal40.la
noinst_LTLIBRARIES		= libjournal40-static.la

libjournal40_la_LIBADD		= $(top_builddir)/libaal/libaal.la
libjournal40_la_LDFLAGS		= $(PLUGIN_LDFLAGS)
libjournal40_la_SOURCES		= $(journal40_sources)

libjournal40_static_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libjournal40_static_la_SOURCES	= $(journal40_sources)

INCLUDES = $(includedir)

