includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

tail40_sources		    = tail40.c tail40.h

lib_LTLIBRARIES		    = libtail40.la
noinst_LTLIBRARIES	    = libtail40-static.la

libtail40_la_LIBADD	    = 
libtail40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libtail40_la_SOURCES	    = $(tail40_sources)

libtail40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libtail40_static_la_SOURCES = $(tail40_sources)

INCLUDES = $(includedir)

