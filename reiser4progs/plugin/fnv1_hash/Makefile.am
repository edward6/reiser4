includedir			= -I$(top_srcdir)/include
libdir				= $(plugindir)

fnv1_hash_sources		= fnv1_hash.c

lib_LTLIBRARIES			= libfnv1_hash.la
noinst_LTLIBRARIES		= libfnv1_hash-static.la

libfnv1_hash_la_LDFLAGS		= $(PLUGIN_LDFLAGS)
libfnv1_hash_la_SOURCES		= $(fnv1_hash_sources)

libfnv1_hash_static_la_LDFLAGS	= $(PLUGIN_LDFLAGS)
libfnv1_hash_static_la_SOURCES	= $(fnv1_hash_sources)

INCLUDES = $(includedir)

