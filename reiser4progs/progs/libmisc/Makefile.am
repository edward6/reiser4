noinst_LTLIBRARIES	= libmisc.la
libmisc_la_SOURCES	= misc.c profile.c exception.c

libmisc_la_LIBADD 	= $(top_builddir)/libcomm/libcomm-static.la \
			  $(PROGS_LDADD)

INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/progs/include
