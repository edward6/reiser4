noinst_PROGRAMS = list meat

list_SOURCES	= list.c

list_LDFLAGS	= @PROGS_LDFLAGS@
list_LDADD	= $(top_builddir)/libreiserfs/libreiserfs.la \
		  $(top_builddir)/libaal/libaal.la

meat_SOURCES	= meat.c

meat_LDFLAGS	= @PROGS_LDFLAGS@
meat_LDADD	= $(top_builddir)/libreiserfs/libreiserfs.la \
		  $(top_builddir)/libaal/libaal.la

INCLUDES	= -I$(top_srcdir)/include

