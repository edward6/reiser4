sbin_PROGRAMS 		= mkfs.reiser4
mkfs_reiser4_SOURCES 	= mkfs.c

mkfs_reiser4_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la \
			  $(top_builddir)/progs/libmisc/libmisc.la \
			  $(PROGS_LDADD)
			  
mkfs_reiser4_LDFLAGS 	= $(PROGS_LDFLAGS)

INCLUDES		= -I$(top_srcdir)/include -I$(top_srcdir)/progs/include
