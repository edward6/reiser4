includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

if ENABLE_COMPACT
internal40_sources		= internal40.c internal40.h
else
internal40_sources              = internal40.c internal40_repair.c internal40.h
endif

lib_LTLIBRARIES			= libinternal40.la
noinst_LTLIBRARIES		= libinternal40-static.la

libinternal40_la_LIBADD		= $(top_builddir)/libaal/libaal.la
libinternal40_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libinternal40_la_SOURCES	= $(internal40_sources)

libinternal40_static_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libinternal40_static_la_SOURCES	= $(internal40_sources)

INCLUDES = $(includedir)

