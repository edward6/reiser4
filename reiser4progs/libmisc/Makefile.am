includedir		= -I$(top_srcdir)/include

noinst_LTLIBRARIES	= libmisc.la
libmisc_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

libmisc_la_SOURCES      =  misc.c bitops.c bitmap.c 

INCLUDES = $(includedir)

