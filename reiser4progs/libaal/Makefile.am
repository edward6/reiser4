includedir 		 = -I$(top_srcdir)/include

libaal_sources  	 = device.c file.c exception.c list.c malloc.c \
			   print.c string.c math.c debug.c bitops.c gauge.c

lib_LTLIBRARIES		 = libaal.la
noinst_LTLIBRARIES	 = libaal-static.la

libaal_la_LDFLAGS 	 = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
			   -release $(LT_RELEASE)

libaal_la_SOURCES  	 = $(libaal_sources)

libaal_static_la_SOURCES = $(libaal_sources)

INCLUDES = $(includedir)

