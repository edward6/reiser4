includedir		     = -I$(top_srcdir)/include

libreiser4_sources	     = libreiser4.c filesystem.c format.c journal.c \
			       alloc.c oid.c plugin.c node.c tree.c key.c \
			       object.c coord.c cache.c dir.c

libreiser4_plugins	     = libplugins_header.la \
			       $(top_builddir)/plugin/format40/libformat40-static.la \
			       $(top_builddir)/plugin/journal40/libjournal40-static.la \
			       $(top_builddir)/plugin/alloc40/liballoc40-static.la \
			       $(top_builddir)/plugin/oid40/liboid40-static.la \
			       $(top_builddir)/plugin/node40/libnode40-static.la \
			       $(top_builddir)/plugin/internal40/libinternal40-static.la \
			       $(top_builddir)/plugin/direntry40/libdirentry40-static.la \
			       $(top_builddir)/plugin/stat40/libstat40-static.la \
			       $(top_builddir)/plugin/key40/libkey40-static.la \
			       $(top_builddir)/plugin/dir40/libdir40-static.la \
			       $(top_builddir)/plugin/r5_hash/libr5_hash-static.la \
			       $(top_builddir)/plugin/tea_hash/libtea_hash-static.la \
			       $(top_builddir)/plugin/rupasov_hash/librupasov_hash-static.la \
			       $(top_builddir)/plugin/fnv1_hash/libfnv1_hash-static.la \
			       $(top_builddir)/plugin/sdext_unix/libsdext_unix-static.la \
			       libplugins_footer.la

lib_LTLIBRARIES		     = libreiser4.la

noinst_LTLIBRARIES	     = libreiser4-static.la libplugins_header.la libplugins_footer.la

libreiser4_la_LDFLAGS	     = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

if ENABLE_MONOLITHIC

libreiser4_la_LIBADD	     = $(top_builddir)/libaal/libaal.la \
			       $(top_builddir)/libmisc/libmisc.la \
			       $(libreiser4_plugins) 	
else

libreiser4_la_LIBADD	     = $(top_builddir)/libaal/libaal.la @LIBREISERFS_LIBS@

endif

libreiser4_la_SOURCES	     = $(libreiser4_sources)

libreiser4_static_la_LIBADD  = $(top_builddir)/libaal/libaal-static.la \
			       $(top_builddir)/libmisc/libmisc.la \
			       $(libreiser4_plugins) 	

libreiser4_static_la_SOURCES = $(libreiser4_sources)

libplugins_header_la_SOURCES = plugins_header.c
libplugins_footer_la_SOURCES = plugins_footer.c

INCLUDES = $(includedir)

