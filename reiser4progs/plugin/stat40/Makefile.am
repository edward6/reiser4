includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

stat40_sources		    = stat40.c stat40.h

lib_LTLIBRARIES		    = libstat40.la
noinst_LTLIBRARIES	    = libstat40-static.la

libstat40_la_LIBADD	    = $(top_builddir)/libaal/libaal-static.la
libstat40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libstat40_la_SOURCES	    = $(stat40_sources)

libstat40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libstat40_static_la_SOURCES = $(stat40_sources)

INCLUDES = $(includedir)

