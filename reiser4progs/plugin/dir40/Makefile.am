includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

dir40_sources		    = dir40.c dir40.h

lib_LTLIBRARIES		    = libdir40.la
noinst_LTLIBRARIES	    = libdir40-static.la

libdir40_la_LIBADD	    = $(top_builddir)/libaal/libaal.la
libdir40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libdir40_la_SOURCES	    = $(dir40_sources)

libdir40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libdir40_static_la_SOURCES = $(dir40_sources)

INCLUDES = $(includedir)

