includedir              = -I$(top_srcdir)/include
node40_sources		= 

node40_lib		= libnode40.la
node40_lib_static	= libnode40-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(node40_lib)
libnode40_la_LIBADD	= $(top_builddir)/libmisc/libmisc.la
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(node40_lib_static)
libnode40_la_LIBADD	= $(top_builddir)/libaal/libaal.la $(top_builddir)/libmisc/libmisc.la
endif

libnode40_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libnode40_la_SOURCES	= $(node40_sources)

INCLUDES = $(includedir)
