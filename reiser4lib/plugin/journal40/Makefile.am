includedir              = -I$(top_srcdir)/include
journal40_sources	= journal40.c journal40.h

journal40_lib		= libjournal40.la
journal40_lib_static	= libjournal40-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(journal40_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(journal40_lib_static)
libjournal40_la_LIBADD  = $(top_builddir)/libaal/libaal.la
endif

libjournal40_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libjournal40_la_SOURCES = $(journal40_sources)

INCLUDES = $(includedir)

