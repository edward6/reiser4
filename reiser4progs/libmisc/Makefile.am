includedir		  = -I$(top_srcdir)/include

noinst_LTLIBRARIES	  = libmisc-static.la
libmisc_static_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -release $(LT_RELEASE)

libmisc_static_la_SOURCES = misc.c bitmap.c 

INCLUDES = $(includedir)

