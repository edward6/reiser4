#! /bin/sh

printf "REISER4_TRACE_FLAGS:     %x\n" $REISER4_TRACE_FLAGS
printf "REISER4_BLOCK_SIZE:      %i\n" ${REISER4_BLOCK_SIZE:-256}
printf "REISER4_UL_DURABLE_MMAP: %s\n" $REISER4_UL_DURABLE_MMAP

ROUNDS=${1:-1}

export REISER4_PRINT_STATS=1
export REISER4_CRASH_MODE=debugger

rm -f gmon.out.*

for r in `seq 1 $ROUNDS` 
do
echo Round $r
./a.out nikita ibk 10000 || exit 1
mv gmon.out gmon.out.ibk.10000.$r
./a.out nikita dir 1 100 || exit 2
mv gmon.out gmon.out.dir.1.100.$r
./a.out nikita dir 3 1000 || exit 3
mv gmon.out gmon.out.dir.3.1000.$r
./a.out jmacd build 3 1000 1000 || exit 4
mv gmon.out gmon.out.build.3.1000.$r
./a.out nikita mongo 3 1000 1000 || exit 5
mv gmon.out gmon.out.mongo.3.1000.$r
./a.out nikita rm 3 1000 1000 || exit 6
mv gmon.out gmon.out.rm.3.1000.$r
./a.out nikita unlink 10000 || exit 7
mv gmon.out gmon.out.unlink.10000.$r
#( find /tmp | ./a.out vs copydir ) || exit 8
#mv gmon.out gmon.out.vs.copydir.tmp.$r
echo Round $r done.
done
