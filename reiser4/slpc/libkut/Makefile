
SOURCES		= kutlock.h kutslab.h atomic.h spinlock.h rwlock.h spinlock.c

TARGETS		= libkut-no.a libkut-ex.a libkut-px.a

OBJECTS	 	= kutlock.o kutslab-no.o kutslab-ex.o kutslab-px.o


GCC		= gcc
#GCC		= /home/jmacd/GCC301/bin/gcc

CFLAGS		= -O3 -fomit-frame-pointer -Wall -I..

#CFLAGS		= -g -Wall -O -fno-inline

all: $(OBJECTS) $(TARGETS)

clean:
	rm -rf $(TARGETS) $(OBJECTS)

libkut-no.a: $(OBJECTS)
	ar cru libkut-no.a kutslab-no.o

libkut-ex.a: $(OBJECTS)
	ar cru libkut-ex.a kutlock.o kutslab-ex.o

libkut-px.a: $(OBJECTS)
	ar cru libkut-px.a kutslab-px.o

kutlock.o: kutlock.h kutlock.c Makefile
	$(GCC) $(CFLAGS) -c kutlock.c -o kutlock.o

kutslab-no.o: kutlock.h kutslab.h kutslab.c Makefile
	$(GCC) $(CFLAGS) -DKUT_USE_LOCKS=0 -c kutslab.c -o kutslab-no.o

kutslab-ex.o: kutlock.h kutslab.h kutslab.c Makefile
	$(GCC) $(CFLAGS) -DKUT_USE_LOCKS=1 -DKUT_LOCK_SPINLOCK=1 -c kutslab.c -o kutslab-ex.o

kutslab-px.o: kutlock.h kutslab.h kutslab.c Makefile
	$(GCC) $(CFLAGS) -DKUT_USE_LOCKS=1 -DKUT_LOCK_POSIX=1 -c kutslab.c -o kutslab-px.o
