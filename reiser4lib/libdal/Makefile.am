LIBS 				= @LIBS@

includedir 			= -I$(top_srcdir)/include
lib_LTLIBRARIES	    = libdal.la
libdal_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
					  -release $(LT_RELEASE)

libdal_la_SOURCES  	= dal.c file_dal.c

INCLUDES = $(includedir)

