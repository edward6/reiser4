includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

journal36_sources		= journal36.c journal36.h

lib_LTLIBRARIES			= libjournal36.la
noinst_LTLIBRARIES		= libjournal36-static.la

libjournal36_la_LIBADD		= $(top_builddir)/libaal/libaal-static.la
libjournal36_la_LDFLAGS		= $(PLUGIN_LDFLAGS)
libjournal36_la_SOURCES		= $(journal36_sources)

libjournal36_static_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libjournal36_static_la_SOURCES	= $(journal36_sources)

INCLUDES = $(includedir)

