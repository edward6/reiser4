#! /bin/sh

#set -x

TOPDIR=/root/bench
CURRENT=$TOPDIR/current
TODO=$TOPDIR/todo
DONE=$TOPDIR/done
KERNEL="$(cat $TOPDIR/kernel)"

export LANG=C

cd $TOPDIR || exit 1
. functions.sh                                                           || die

CONTEXT=driver
LOGFILE=$TOPDIR/logfile

mount -oremount,rw /                                                     || die
output ----BENCHMARKING-STARTS----

getfrom $CURRENT
if [ -z $filefound ] ;then
    getfrom $TODO                                                        || die
    if [ -z $filefound ] ;then
	output Nothing to do. Rebooting.
	# restart default kernel
	mv $TOPDIR/do-bench $TOPDIR/do-not-bench
	systemrestart
	exit 0
    else
	do_it mv $TODO/$filefound $CURRENT                               || die
    fi
fi

TEST=$CURRENT/$filefound
if [ ! -d $TEST ] ;then
    abort $TEST is not a directory.
fi

if [ ! -x $TEST/run.me ] ;then
    abort Cannot execute $TEST/run.me
fi

cd $TEST                                                                 || die
output Entering $TEST

CONTEXT=TEST:$filefound
. ./run.me                                                               || die
CONTEXT=driver

cd $TOPDIR                                                               || die

if [ x$OUTCOME = xDONE ] ;then
    output $filefound done.
    do_it mv $TEST $DONE                                                 || die
fi

systemrestart $KERNEL
