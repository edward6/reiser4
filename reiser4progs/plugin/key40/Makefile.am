includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

key40_sources			= key40.c key40.h

lib_LTLIBRARIES			= libkey40.la
noinst_LTLIBRARIES		= libkey40-static.la

libkey40_la_LIBADD		= $(top_builddir)/libaal/libaal.la
libkey40_la_LDFLAGS		= $(PLUGIN_LDFLAGS)
libkey40_la_SOURCES		= $(key40_sources)

libkey40_static_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libkey40_static_la_SOURCES	= $(key40_sources)

INCLUDES = $(includedir)

