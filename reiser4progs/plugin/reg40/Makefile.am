includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

file40_sources		    = reg40.c reg40.h

lib_LTLIBRARIES		    = libreg40.la
noinst_LTLIBRARIES	    = libreg40-static.la

libreg40_la_LIBADD	    = $(top_builddir)/libaal/libaal.la
libreg40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libreg40_la_SOURCES	    = $(file40_sources)

libreg40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libreg40_static_la_SOURCES = $(file40_sources)

INCLUDES = $(includedir)

