#! /bin/sh

PROGRAM=./a.out

printf "REISER4_TRACE_FLAGS:     %x\n" $REISER4_TRACE_FLAGS
printf "REISER4_BLOCK_SIZE:      %i\n" ${REISER4_BLOCK_SIZE:-256}
echo "REISER4_MOUNT:         " $REISER4_MOUNT
echo "REISER4_MOUNT_OPTS:    " $REISER4_MOUNT_OPTS

ROUNDS=${1:-1}

function run()
{
#   do_mkfs
	echo -n $* "..."
	/usr/bin/time -f " T: %e/%S/%U F: %F/%R" $*
}

function do_mkfs()
{
	echo "mkfs $REISER4_MOUNT" | ${PROGRAM} sh
}

export REISER4_PRINT_STATS=1
export REISER4_CRASH_MODE=debugger
export REISER4_TRAP=1

rm -f gmon.out.*

#ORDER='000'
ORDER=${2:-''}

do_mkfs

for r in `seq 1 $ROUNDS` 
do
echo Round $r
run ${PROGRAM} nikita ibk 30${ORDER} || exit 1
mv gmon.out gmon.out.ibk.30${ORDER}.$r 2>/dev/null


#	run ${PROGRAM} jmacd build 3 1000 1000 || exit 4
#	mv gmon.out gmon.out.build.3.1000.$r 2>/dev/null

run ${PROGRAM} nikita dir 1 100${ORDER} 0 || exit 2
mv gmon.out gmon.out.dir.1.100${ORDER}.0.$r 2>/dev/null

run ${PROGRAM} nikita dir 1 100${ORDER} 1 || exit 2
mv gmon.out gmon.out.dir.1.100${ORDER}.1.$r 2>/dev/null

run ${PROGRAM} nikita dir 4 7${ORDER} 0 || exit 3
mv gmon.out gmon.out.dir.7.7${ORDER}.0.$r 2>/dev/null

run ${PROGRAM} nikita dir 4 7${ORDER} 1 || exit 3
mv gmon.out gmon.out.dir.7.7${ORDER}.1.$r 2>/dev/null

run ${PROGRAM} nikita mongo 3 20${ORDER} 0 || exit 5
mv gmon.out gmon.out.mongo.3.20${ORDER}.0.$r 2>/dev/null

run ${PROGRAM} nikita mongo 3 20${ORDER} 1 || exit 5
mv gmon.out gmon.out.mongo.3.20${ORDER}.1.$r 2>/dev/null

run ${PROGRAM} nikita rm 6 10${ORDER} 0 || exit 6
mv gmon.out gmon.out.rm.6.10${ORDER}.0.$r 2>/dev/null

run ${PROGRAM} nikita rm 6 10${ORDER} 1 || exit 6
mv gmon.out gmon.out.rm.6.10${ORDER}.1.$r 2>/dev/null

run ${PROGRAM} nikita unlink 15${ORDER} || exit 7
mv gmon.out gmon.out.unlink.15${ORDER}.$r 2>/dev/null

#run ${PROGRAM} nikita queue 30 10${ORDER} 10000  || exit 6
#mv gmon.out gmon.out.rm.30.10${ORDER}.10000.$r 2>/dev/null

run ${PROGRAM} nikita mongo 30 1${ORDER} 0 || exit 6
mv gmon.out gmon.out.rm.30.1${ORDER}.0.$r 2>/dev/null

run ${PROGRAM} nikita mongo 30 1${ORDER} 1 || exit 6
mv gmon.out gmon.out.rm.30.1${ORDER}.1.$r 2>/dev/null

run ulevel/cp-r plugin || exit 7
mv gmon.out gmon.out.cp-r.plugin.$r 2>/dev/null

#( find /tmp | ${PROGRAM} vs copydir ) || exit 8
#mv gmon.out gmon.out.vs.copydir.tmp.$r
echo Round $r done.
done
