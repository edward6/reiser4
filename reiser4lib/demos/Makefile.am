noinst_PROGRAMS  = 	traverse lookup ls cat

traverse_SOURCES = 	traverse.c
traverse_LDADD 	 = 	$(top_builddir)/libreiserfs/libreiserfs.la \
					$(top_builddir)/libdal/libdal.la
   
traverse_LDFLAGS = 	@DEMOS_LDFLAGS@

lookup_SOURCES 	 = 	lookup.c
lookup_LDADD 	 = 	$(top_builddir)/libreiserfs/libreiserfs.la \
					$(top_builddir)/libdal/libdal.la 
   
lookup_LDFLAGS 	 = 	@DEMOS_LDFLAGS@

ls_SOURCES 	 	 = 	ls.c
ls_LDADD 	 	 = 	$(top_builddir)/libreiserfs/libreiserfs.la \
					$(top_builddir)/libdal/libdal.la 
   
ls_LDFLAGS 	 	 = 	@DEMOS_LDFLAGS@
  
cat_SOURCES 	 = 	cat.c
cat_LDADD 	 	 = 	$(top_builddir)/libreiserfs/libreiserfs.la \
					$(top_builddir)/libdal/libdal.la 
   
cat_LDFLAGS 	 = 	@DEMOS_LDFLAGS@
  
INCLUDES	 	 = 	-I$(top_srcdir)/include

