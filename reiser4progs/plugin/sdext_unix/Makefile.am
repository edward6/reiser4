includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

sdext_unix_sources		= sdext_unix.c sdext_unix.h

lib_LTLIBRARIES			= libsdext_unix.la
noinst_LTLIBRARIES		= libsdext_unix-static.la

libsdext_unix_la_LIBADD		= $(top_builddir)/libaal/libaal-static.la
libsdext_unix_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libsdext_unix_la_SOURCES	= $(sdext_unix_sources)

libsdext_unix_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libsdext_unix_static_la_SOURCES = $(sdext_unix_sources)

INCLUDES = $(includedir)

