#! /bin/sh

printf "REISER4_TRACE_FLAGS:     %x\n" $REISER4_TRACE_FLAGS
printf "REISER4_BLOCK_SIZE:      %i\n" ${REISER4_BLOCK_SIZE:-256}
printf "REISER4_UL_DURABLE_MMAP: %s\n" $REISER4_UL_DURABLE_MMAP

ROUNDS=${1:-1}

function run()
{
	echo -n $* "..."
	/usr/bin/time -f " T: %e/%S/%U F: %F/%R" $*
}

export REISER4_PRINT_STATS=1
export REISER4_CRASH_MODE=debugger

rm -f gmon.out.*

for r in `seq 1 $ROUNDS` 
do
echo Round $r
# "non-persistent" tests
if [ x$REISER4_UL_DURABLE_MMAP = x ]
then
	run ./a.out nikita ibk 30000 || exit 1
	mv gmon.out gmon.out.ibk.30000.$r 2>/dev/null

#	run ./a.out jmacd build 3 1000 1000 || exit 4
#	mv gmon.out gmon.out.build.3.1000.$r 2>/dev/null
fi

run ./a.out nikita dir 1 100000 0 || exit 2
mv gmon.out gmon.out.dir.1.100000.0.$r 2>/dev/null

run ./a.out nikita dir 1 100000 1 || exit 2
mv gmon.out gmon.out.dir.1.100000.1.$r 2>/dev/null

run ./a.out nikita dir 4 7000 0 || exit 3
mv gmon.out gmon.out.dir.7.7000.0.$r 2>/dev/null

run ./a.out nikita dir 4 7000 1 || exit 3
mv gmon.out gmon.out.dir.7.7000.1.$r 2>/dev/null

run ./a.out nikita mongo 3 20000 0 || exit 5
mv gmon.out gmon.out.mongo.3.20000.0.$r 2>/dev/null

run ./a.out nikita mongo 3 20000 1 || exit 5
mv gmon.out gmon.out.mongo.3.20000.1.$r 2>/dev/null

run ./a.out nikita rm 6 10000 0 || exit 6
mv gmon.out gmon.out.rm.6.10000.0.$r 2>/dev/null

run ./a.out nikita rm 6 10000 1 || exit 6
mv gmon.out gmon.out.rm.6.10000.1.$r 2>/dev/null

run ./a.out nikita unlink 15000 || exit 7
mv gmon.out gmon.out.unlink.15000.$r 2>/dev/null

#run ./a.out nikita queue 30 10000 10000  || exit 6
#mv gmon.out gmon.out.rm.30.10000.10000.$r 2>/dev/null

run ./a.out nikita mongo 30 1500 0 || exit 6
mv gmon.out gmon.out.rm.30.1500.0.$r 2>/dev/null

run ./a.out nikita mongo 30 1500 1 || exit 6
mv gmon.out gmon.out.rm.30.1500.1.$r 2>/dev/null

#( find /tmp | ./a.out vs copydir ) || exit 8
#mv gmon.out gmon.out.vs.copydir.tmp.$r
echo Round $r done.
done
