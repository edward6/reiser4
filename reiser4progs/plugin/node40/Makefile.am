includedir		    = -I$(top_srcdir)/include
libdir			    = $(plugindir)

if ENABLE_COMPACT
node40_sources		    = node40.c node40.h
else
node40_sources              = node40.c node40_repair.c node40.h
endif

lib_LTLIBRARIES		    = libnode40.la
noinst_LTLIBRARIES	    = libnode40-static.la

libnode40_la_LIBADD	    = $(top_builddir)/libaal/libaal.la \
			      $(top_builddir)/libcomm/libcomm-static.la
	
libnode40_la_LDFLAGS	    = $(PLUGIN_LDFLAGS)
libnode40_la_SOURCES	    = $(node40_sources)

libnode40_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libnode40_static_la_SOURCES = $(node40_sources)

INCLUDES = $(includedir)

