includedir		     = -I$(top_srcdir)/include
libdir			     = $(plugindir)

alloc36_sources		     = alloc36.c alloc36.h

lib_LTLIBRARIES		     = liballoc36.la
noinst_LTLIBRARIES	     = liballoc36-static.la

liballoc36_la_LIBADD	     = $(top_builddir)/libaal/libaal.la	\
			       $(top_builddir)/libcomm/libcomm-static.la
	
liballoc36_la_LDFLAGS	     = $(PLUGIN_LDFLAGS)
liballoc36_la_SOURCES	     = $(alloc36_sources)

liballoc36_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
liballoc36_static_la_SOURCES = $(alloc36_sources)

INCLUDES = $(includedir)

