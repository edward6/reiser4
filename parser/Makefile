PROG=	tmp

.SUFFIXES= .l .y .c

CSRCS=	tmp.c
GENSRCS= parser.c
GENHDRS= y.t.h

SRCS=	${GENSRCS} ${CSRCS}
CLEANFILES= ${GENSRCS} ${GENHDRS} y.output

# Override default kernel CFLAGS.  This is a userland app.
# PARS_CFLAGS:= -I/usr/include -I. -ldb
YFLAGS= -d

#NOMAN=	noman

#ifdef DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS= -d
#endif

$(PROG):  $(SRCS) $(GENHDRS)
	$(CC) $(PARS_CFLAGS) $(CFLAGS)  $(CSRCS) -o $(PROG)
#	$(CC) $(PARS_CFLAGS) $(CFLAGS)  $(SRCS) -o $(PROG)

clean:
	rm -f $(CLEANFILES) $(PROG)

y.t.h parser.c: parser.y
	./yacc $(YFLAGS) parser.y
#	$(YACC) $(YFLAGS) parser.y
	mv y.t.c parser.c




