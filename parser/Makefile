#    CONFIG_REISER4_FS_SYSCALL_BISON

obj-$(CONFIG_REISER4_FS_SYSCALL) += sys_reiser4.o

#sys_reiser4-y := sys_reiser4.o

YFLAGS= -S yacc_reiser4.c  -r all  -b $(obj)/parser
#YFLAGS= -S yacc.c -d -r all -b $(obj)/parser
#YFLAGS= -y -d -r all -b $(obj)/parser

YACC= bison

$(obj)/sys_reiser4.o: $(obj)/parser.tab.c

$(obj)/parser.tab.c: $(src)/parser.y
	(BISON_PKGDATADIR=$(obj)/bison-1.875 ;\
	export BISON_PKGDATADIR ;\
	$(YACC) $(YFLAGS) $(src)/parser.y )









