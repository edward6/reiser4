man_MANS   = mkfs.reiser4.8

EXTRA_DIST = $(man_MANS)

uninstall-make_reiser4_8:
	if test -f $(mandir)/man8/make_reiser4.8; then \
	    rm -f $(mandir)/man8/make_reiser4.8; \
	fi;

install-make_reiser4_8: mkfs.reiser4.8 uninstall-make_reiser4_8
	ln $(mandir)/man8/mkfs.reiser4.8 $(mandir)/man8/make_reiser4.8

install-data-local: install-make_reiser4_8
uninstall-local: uninstall-make_reiser4_8

