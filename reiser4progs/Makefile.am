if ENABLE_COMPACT
SUBDIRS         = libaal libaux plugin libreiser4 include doc debug
else
SUBDIRS         = libaal libaux plugin libreiser4 librepair progs demos include doc debug
endif

EXTRA_DIST	= ABOUT-NLS AUTHORS BUGS COPYING TODO NEWS THANKS README CREDITS \
		  reiser4progs.spec.in libreiser4.m4
	
aclocaldir 	= $(datadir)/aclocal
aclocal_DATA	= libreiser4.m4

dist-hook: reiser4progs.spec
	cp reiser4progs.spec $(distdir)

