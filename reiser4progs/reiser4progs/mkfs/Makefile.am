sbin_PROGRAMS 		= mkfs.reiser4
mkfs_reiser4_SOURCES 	= mkfs.c

mkfs_reiser4_LDADD 	= @LIBS@ $(top_builddir)/libreiser4/libreiser4.la \
			  $(top_builddir)/reiser4progs/libmisc/libmisc.la

mkfs_reiser4_LDFLAGS 	= @PROGS_LDFLAGS@

INCLUDES		= -I$(top_srcdir)/include

