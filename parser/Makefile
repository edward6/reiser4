
%c:%l
	lex -v $< 

%c:%y
	yacc  $< 

tmp:  tmp.o
	cc tmp.o -o tmp

tmp.o: tmp.c  lex.yy.c y.tab.c
	cc -c -O tmp.c

y.tab.c: parser.y
	./yacc -v parser.y

lex.yy.c: lex.l
	lex -v lex.l

clear:
	rm -f tmp.o  lex.yy.? tmp