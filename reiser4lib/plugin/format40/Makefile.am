includedir              = -I$(top_srcdir)/include
format40_sources	= format40.c format40.h

format40_lib		= libformat40.la
format40_lib_static	= libformat40-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(format40_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(format40_lib_static)
libformat40_la_LIBADD   = $(top_builddir)/libaal/libaal.la
endif

libformat40_la_LDFLAGS  = $(PLUGIN_LDFLAGS)
libformat40_la_SOURCES  = $(format40_sources)

INCLUDES = $(includedir)

