includedir              = -I$(top_srcdir)/include
alloc36_sources		= alloc36.c alloc36.h

alloc36_lib		= liballoc36.la
alloc36_lib_static	= liballoc36-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(alloc36_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(alloc36_lib_static)
liballoc36_la_LIBADD	= $(top_builddir)/libaal/libaal.la
endif

liballoc36_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
liballoc36_la_SOURCES	= $(alloc36_sources)

INCLUDES = $(includedir)

