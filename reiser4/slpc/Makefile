
SOURCES		= slpc.h slph.h slpl.h

TARGETS		= 			\
					\
		testslpc1-64-no		\
		testslpc1-128-no	\
		testslpc1-256-no	\
		testslpc1-512-no	\
		testslpc1-1024-no	\
					\
		testslpc1-64-rw		\
		testslpc1-128-rw	\
		testslpc1-256-rw	\
		testslpc1-512-rw	\
		testslpc1-1024-rw	\
					\
		testslpc1-64-ex		\
		testslpc1-128-ex	\
		testslpc1-256-ex	\
		testslpc1-512-ex	\
		testslpc1-1024-ex	\
					\
		testslpc2-rw		\
		testslpc2-ex		\
					\
		testslpc3

GCC		= gcc
#GCC		= /home/jmacd/GCC301/bin/gcc

CFLAGS		= -O3 -fomit-frame-pointer -Wall -Wno-uninitialized -I. -L./libkut

#CFLAGS		= -O -g -fno-inline -Wall -I. -L./libkut

all:
	(cd libkut && $(MAKE) all)
	$(MAKE) targets

targets: $(TARGETS)

clean:
	(cd libkut && $(MAKE) clean)
	rm -rf $(TARGETS)

testslpc3: $(SOURCES) testslpc3.c Makefile
	$(GCC) $(CFLAGS) testslpc3.c -o testslpc3 -I/usr/src/linux-2.4.10/include

# TESTSLPC1 with read-write spinlocks

testslpc1-64-rw: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-64-rw -lkut-ex -DSLPC_MAX_COUNT=6 -DSLPC_TARGET_SIZE=64 -DSLPC_USE_SPINLOCK=2

testslpc1-128-rw: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-128-rw -lkut-ex -DSLPC_MAX_COUNT=14 -DSLPC_TARGET_SIZE=128 -DSLPC_USE_SPINLOCK=2

testslpc1-256-rw: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-256-rw -lkut-ex -DSLPC_MAX_COUNT=30 -DSLPC_TARGET_SIZE=256 -DSLPC_USE_SPINLOCK=2

testslpc1-512-rw: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-512-rw -lkut-ex -DSLPC_MAX_COUNT=62 -DSLPC_TARGET_SIZE=512 -DSLPC_USE_SPINLOCK=2

testslpc1-1024-rw: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-1024-rw -lkut-ex -DSLPC_MAX_COUNT=126 -DSLPC_TARGET_SIZE=1024 -DSLPC_USE_SPINLOCK=2

# TESTSLPC1 with exclusive spinlocks

testslpc1-64-ex: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-64-ex -lkut-ex -DSLPC_MAX_COUNT=6 -DSLPC_TARGET_SIZE=64 -DSLPC_USE_SPINLOCK=1

testslpc1-128-ex: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-128-ex -lkut-ex -DSLPC_MAX_COUNT=14 -DSLPC_TARGET_SIZE=128 -DSLPC_USE_SPINLOCK=1

testslpc1-256-ex: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-256-ex -lkut-ex -DSLPC_MAX_COUNT=30 -DSLPC_TARGET_SIZE=256 -DSLPC_USE_SPINLOCK=1

testslpc1-512-ex: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-512-ex -lkut-ex -DSLPC_MAX_COUNT=62 -DSLPC_TARGET_SIZE=512 -DSLPC_USE_SPINLOCK=1

testslpc1-1024-ex: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-1024-ex -lkut-ex -DSLPC_MAX_COUNT=126 -DSLPC_TARGET_SIZE=1024 -DSLPC_USE_SPINLOCK=1

# TESTSLPC1 without locks

testslpc1-64-no: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-64-no -lkut-no -DSLPC_MAX_COUNT=7 -DSLPC_TARGET_SIZE=64 -DSLPC_USE_SPINLOCK=0

testslpc1-128-no: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-128-no -lkut-no -DSLPC_MAX_COUNT=15 -DSLPC_TARGET_SIZE=128 -DSLPC_USE_SPINLOCK=0

testslpc1-256-no: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-256-no -lkut-no -DSLPC_MAX_COUNT=31 -DSLPC_TARGET_SIZE=256 -DSLPC_USE_SPINLOCK=0

testslpc1-512-no: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-512-no -lkut-no -DSLPC_MAX_COUNT=63 -DSLPC_TARGET_SIZE=512 -DSLPC_USE_SPINLOCK=0

testslpc1-1024-no: $(SOURCES) testslpc1.c Makefile
	$(GCC) $(CFLAGS) testslpc1.c -o testslpc1-1024-no -lkut-no -DSLPC_MAX_COUNT=127 -DSLPC_TARGET_SIZE=1024 -DSLPC_USE_SPINLOCK=0

# TESTSLPC2 with exclusive spinlocks (256 byte node)

testslpc2-ex: $(SOURCES) testslpc2.c Makefile
	$(GCC) $(CFLAGS) testslpc2.c -o testslpc2-ex -lpthread -lkut-ex -DSLPC_USE_SPINLOCK=1

# TESTSLPC2 with read-write spinlocks (256 byte node)

testslpc2-rw: $(SOURCES) testslpc2.c Makefile
	$(GCC) $(CFLAGS) testslpc2.c -o testslpc2-rw -lpthread -lkut-ex -DSLPC_USE_SPINLOCK=2
