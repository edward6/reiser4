includedir              = -I$(top_srcdir)/include
node36_sources		= 

node36_lib		= libnode36.la
node36_lib_static	= libnode36-static.la

if ENABLE_MONOLITIC
noinst_LTLIBRARIES      = $(node36_lib)
else
libdir			= $(plugindir)
lib_LTLIBRARIES		= $(node36_lib_static)
libnode36_la_LIBADD	= $(top_builddir)/libaal/libaal.la
endif

libnode36_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libnode36_la_SOURCES	= $(node36_sources)

INCLUDES = $(includedir)
