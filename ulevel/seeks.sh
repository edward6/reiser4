#! /bin/sh

START=$1
END=$2

if [ $START ] ;then
	XRANGE="[$START:$END]"
else
	XRANGE=""
fi

(
	echo 'set terminal postscript;'
#	echo 'clear;'
	echo 'set data style lines;'
	echo "plot $XRANGE '-'"
	bio-out.sh
) | gnuplot #| gv -landscape -
