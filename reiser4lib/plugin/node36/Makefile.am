includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

node36_sources		    = 

lib_LTLIBRARIES		    = libnode36.la
noinst_LTLIBRARIES	    = libnode36-static.la

libnode36_la_LIBADD	    = $(top_builddir)/libaal/libaal.la
libnode36_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libnode36_la_SOURCES	    = $(node36_sources)

libnode36_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libnode36_static_la_SOURCES = $(node36_sources)

INCLUDES = $(includedir)
