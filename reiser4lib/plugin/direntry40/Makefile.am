includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)
direntry40_sources	    = direntry40.c direntry40.h
lib_LTLIBRARIES		    = libdirentry40.la
noinst_LTLIBRARIES	    = libdirentry40-static.la
libdirentry40_la_LIBADD	    = $(top_builddir)/libaal/libaal.la $(top_builddir)/libmisc/libmisc.la
libdirentry40_la_LDFLAGS    = $(PLUGIN_LDFLAGS)
libdirentry40_la_SOURCES    = $(direntry40_sources)

libdirentry40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libdirentry40_static_la_SOURCES = $(direntry40_sources)
libdirentry40_static_la_LIBADD  = $(top_builddir)/libmisc/libmisc.la

INCLUDES = $(includedir)

