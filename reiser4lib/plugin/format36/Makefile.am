includedir              = -I$(top_srcdir)/include
format36_sources	= format36.c format36.h

format36_lib		= libformat36.la
format36_lib_static	= libformat36-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(format36_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(format36_lib_static)
libformat36_la_LIBADD   = $(top_builddir)/libaal/libaal.la
endif

libformat36_la_LDFLAGS  = $(PLUGIN_LDFLAGS)
libformat36_la_SOURCES  = $(format36_sources)

INCLUDES = $(includedir)

