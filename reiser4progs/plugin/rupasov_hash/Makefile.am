includedir			  = -I$(top_srcdir)/include
libdir				  = $(plugindir)

rupasov_hash_sources		  = rupasov_hash.c

lib_LTLIBRARIES			  = librupasov_hash.la
noinst_LTLIBRARIES		  = librupasov_hash-static.la

librupasov_hash_la_LDFLAGS	  = $(PLUGIN_LDFLAGS)
librupasov_hash_la_SOURCES	  = $(rupasov_hash_sources)

librupasov_hash_static_la_LDFLAGS = $(PLUGIN_LDFLAGS)
librupasov_hash_static_la_SOURCES = $(rupasov_hash_sources)

INCLUDES = $(includedir)

