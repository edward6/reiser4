#! /bin/sh

printf "REISER4_TRACE_FLAGS:     %x\n" $REISER4_TRACE_FLAGS
printf "REISER4_BLOCK_SIZE:      %i\n" ${REISER4_BLOCK_SIZE:-256}
printf "REISER4_UL_DURABLE_MMAP: %s\n" $REISER4_UL_DURABLE_MMAP

export REISER4_PRINT_STATS=1

./a.out nikita ibk 10000 || exit 1
./a.out nikita dir 1 100 || exit 2
./a.out nikita dir 3 1000 || exit 3
./a.out jmacd build 3 1000 1000 || exit 4
( find /tmp | ./a.out vs copydir ) || exit 5
./a.out nikita mongo 3 1000 1000 || exit 6
./a.out nikita rm 3 1000 1000 || exit 7


