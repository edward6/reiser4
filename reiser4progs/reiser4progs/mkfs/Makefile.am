sbin_PROGRAMS 		= mkfs.reiser4
mkfs_reiser4_SOURCES 	= mkfs.c

if STATIC

mkfs_reiser4_LDADD 	= $(top_builddir)/libreiser4/libreiser4-static.la \
			  $(top_builddir)/reiser4progs/libmisc/libmisc.a
else

mkfs_reiser4_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la \
			  $(top_builddir)/reiser4progs/libmisc/libmisc.a
endif

mkfs_reiser4_LDFLAGS 	= $(PROGS_LDFLAGS)

INCLUDES		= -I$(top_srcdir)/include

