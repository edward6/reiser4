noinst_PROGRAMS = list meat

list_SOURCES	= list.c

list_LDFLAGS	= $(PROGS_LDFLAGS)

if STATIC
list_LDADD 	= $(top_builddir)/libreiser4/libreiser4-static.la
else
list_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la
endif

meat_SOURCES	= meat.c

meat_LDFLAGS	= $(PROGS_LDFLAGS)

if STATIC
meat_LDADD 	= $(top_builddir)/libreiser4/libreiser4-static.la
else
meat_LDADD 	= $(top_builddir)/libreiser4/libreiser4.la
endif

INCLUDES	= -I$(top_srcdir)/include

