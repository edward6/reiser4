SUBDIRS 			= format40 journal40 alloc40 node40 stat40 direntry40 internal40 \
				  oid40 key40 dir40 format36 journal36 alloc36 r5_hash tea_hash \
				  rupasov_hash fnv1_hash sdext_unix reg40

noinst_LTLIBRARIES	     	= libheader.la libfooter.la libreiser4-plugin.la

libheader_la_SOURCES 		= header.c
libfooter_la_SOURCES 		= footer.c

libreiser4_plugin_la_LIBADD  	= libheader.la \
			          format40/libformat40-static.la \
			          journal40/libjournal40-static.la \
			          alloc40/liballoc40-static.la \
			          oid40/liboid40-static.la \
			          node40/libnode40-static.la \
			          internal40/libinternal40-static.la \
			          direntry40/libdirentry40-static.la \
			          stat40/libstat40-static.la \
			          key40/libkey40-static.la \
			          dir40/libdir40-static.la \
			          reg40/libreg40-static.la \
			          r5_hash/libr5_hash-static.la \
			          tea_hash/libtea_hash-static.la \
			          rupasov_hash/librupasov_hash-static.la \
			          fnv1_hash/libfnv1_hash-static.la \
			          sdext_unix/libsdext_unix-static.la \
			          libfooter.la

libreiser4_plugin_la_SOURCES	=
