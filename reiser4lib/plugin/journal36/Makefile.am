includedir              = -I$(top_srcdir)/include
journal36_sources	= journal36.c journal36.h

journal36_lib		= libjournal36.la
journal36_lib_static	= libjournal36-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(journal36_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(journal36_lib_static)
libjournal36_la_LIBADD   = $(top_builddir)/libaal/libaal.la
endif

libjournal36_la_LDFLAGS  = $(PLUGIN_LDFLAGS)
libjournal36_la_SOURCES  = $(journal36_sources)

INCLUDES = $(includedir)

