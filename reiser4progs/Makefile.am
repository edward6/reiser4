SUBDIRS		= libaal libmisc plugin libreiserfs demos include doc debug

EXTRA_DIST	= ABOUT-NLS AUTHORS BUGS COPYING TODO NEWS THANKS README \
		  libreiserfs.spec.in libreiserfs.m4
	
aclocaldir 	= $(datadir)/aclocal
aclocal_DATA	= libreiserfs.m4

dist-hook: libreiserfs.spec
	cp libreiserfs.spec $(distdir)

