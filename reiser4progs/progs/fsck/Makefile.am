sbin_PROGRAMS 		= fsck.reiser4
fsck_reiser4_SOURCES 	= fsck.c

fsck_reiser4_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la \
			  $(top_builddir)/progs/libmisc/libmisc.a


fsck_reiser4_LDFLAGS 	= $(PROGS_LDFLAGS)

INCLUDES		= -I$(top_srcdir)/include -I$(top_srcdir)/progs/include

