sbin_PROGRAMS 		= mkfs.reiser4
mkfs_reiser4_SOURCES 	= mkfs.c

mkfs_reiser4_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la \
			  $(top_builddir)/progs/libmisc/libmisc.la \
			  $(PROGS_LDADD)
			  
mkfs_reiser4_LDFLAGS 	= $(PROGS_LDFLAGS)

INCLUDES		= -I$(top_srcdir)/include -I$(top_srcdir)/progs/include

uninstall-make_reiser4:
	if test -f $(sbindir)/make_reiser4; then \
	    rm -f $(sbindir)/make_reiser4; \
	fi;

install-make_reiser4: mkfs.reiser4 uninstall-make_reiser4
	ln $(sbindir)/mkfs.reiser4 $(sbindir)/make_reiser4

install-data-local: install-make_reiser4
uninstall-local: uninstall-make_reiser4

