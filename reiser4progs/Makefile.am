SUBDIRS		= libaal libmisc plugin libreiser4 progs demos include doc debug

EXTRA_DIST	= ABOUT-NLS AUTHORS BUGS COPYING TODO NEWS THANKS README \
		  reiser4progs.spec.in libreiser4.m4
	
aclocaldir 	= $(datadir)/aclocal
aclocal_DATA	= libreiser4.m4

dist-hook: reiser4progs.spec
	cp reiser4progs.spec $(distdir)

