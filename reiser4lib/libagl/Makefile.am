LIBS 				= @LIBS@

includedir 			= -I$(top_srcdir)/include
lib_LTLIBRARIES	    = libagl.la
libagl_la_LDFLAGS 	= -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
					  -release $(LT_RELEASE)

libagl_la_SOURCES  	= device.c file_device.c alloc.c exception.c print.c string.c

INCLUDES = $(includedir)

