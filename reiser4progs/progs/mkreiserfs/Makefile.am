sbin_PROGRAMS 		= mkreiserfs
mkreiserfs_SOURCES 	= mkreiserfs.c

mkreiserfs_LDADD 	= @LIBS@ $(top_builddir)/libreiserfs/libreiserfs.la

mkreiserfs_LDFLAGS 	= @PROGS_LDFLAGS@

INCLUDES		= -I$(top_srcdir)/include
