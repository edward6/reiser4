includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

oid40_sources		    = oid40.c oid40.h

lib_LTLIBRARIES		    = liboid40.la
noinst_LTLIBRARIES	    = liboid40-static.la

liboid40_la_LIBADD	    = $(top_builddir)/libaal/libaal-static.la
liboid40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
liboid40_la_SOURCES	    = $(oid40_sources)

liboid40_static_la_LDFLAGS  = $(PLUGIN_LDFLAGS)
liboid40_static_la_SOURCES  = $(oid40_sources)

INCLUDES = $(includedir)

