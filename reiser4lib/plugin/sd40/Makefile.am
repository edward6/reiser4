includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)
sd40_sources		    = sd40.c sd40.h
lib_LTLIBRARIES		    = libsd40.la
noinst_LTLIBRARIES	    = libsd40-static.la
libsd40_la_LIBADD	    = $(top_builddir)/libaal/libaal.la $(top_builddir)/libmisc/libmisc.la
libsd40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libsd40_la_SOURCES	    = $(sd40_sources)

libsd40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libsd40_static_la_SOURCES = $(sd40_sources)
libsd40_static_la_LIBADD  = $(top_builddir)/libmisc/libmisc.la

INCLUDES = $(includedir)

