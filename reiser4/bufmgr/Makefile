
CFILES   = bufmgr.c txnmgr.c logmgr.c freemap.c iosched.c zipf.c
HFILES   = bufmgr.h txnmgr.h logmgr.h freemap.h iosched.h tslist.h tshash.h zipf.h

SOURCES  = $(CFILES) $(HFILES)

TARGETS  = testbm1-prime testbm1-pow2 testzipf1 testts1 testts2

INCLUDES = -I../../../slpc -I../../..

LIBS     = -L../../../slpc/libkut -lkut-px -lm -lpthread

CFLAGS	 = -Wall -g $(INCLUDES) -DBM_DEBUG=1

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

testbm1-prime: $(SOURCES) testbm1.c
	gcc $(CFLAGS) $(CFILES) testbm1.c $(LIBS) -o testbm1-prime -DBM_HASH_PRIME=1

testbm1-pow2: $(SOURCES) testbm1.c
	gcc $(CFLAGS) $(CFILES) testbm1.c $(LIBS) -o testbm1-pow2 -DBM_HASH_PRIME=0

testzipf1: zipf.c zipf.h testzipf1.c
	gcc $(CFLAGS) zipf.c testzipf1.c $(LIBS) -o testzipf1

testts1: tslist.h testts1.c
	gcc $(CFLAGS) testts1.c -o testts1

testts2: tshash.h testts2.c
	gcc $(CFLAGS) testts2.c -o testts2
