includedir              = -I$(top_srcdir)/include
alloc40_sources		= alloc40.c alloc40.h

alloc40_lib		= liballoc40.la
alloc40_lib_static	= liballoc40-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(alloc40_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(alloc40_lib_static)
liballoc40_la_LIBADD	= $(top_builddir)/libaal/libaal.la
endif

liballoc40_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
liballoc40_la_SOURCES	= $(alloc40_sources)

INCLUDES = $(includedir)

