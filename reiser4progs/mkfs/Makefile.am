sbin_PROGRAMS 		= mkfs.reiser4
mkfs_reiser4_SOURCES 	= mkfs.c

mkfs_reiser4_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la \
			  $(top_builddir)/libaal/libaal.la \
			  $(top_builddir)/libmisc/libmisc.la \
			  $(top_builddir)/progsmisc/libprogsmisc.a\
			  /usr/lib/libuuid.a

mkfs_reiser4_LDFLAGS 	= $(PROGS_LDFLAGS)

INCLUDES		= -I$(top_srcdir)/include
