includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

format36_sources		= format36.c format36.h

lib_LTLIBRARIES			= libformat36.la
noinst_LTLIBRARIES		= libformat36-static.la

libformat36_la_LIBADD		= $(top_builddir)/libaal/libaal-static.la
libformat36_la_LDFLAGS		= $(PLUGIN_LDFLAGS)
libformat36_la_SOURCES		= $(format36_sources)

libformat36_static_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libformat36_static_la_SOURCES	= $(format36_sources)

INCLUDES = $(includedir)

