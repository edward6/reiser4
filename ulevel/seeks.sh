#! /bin/sh

OPTVAL=`getopt -o d:e:s:o:t: -n 'seeks.sh' -- "$@"`

# Note the quotes around `$TEMP': they are essential!
eval set -- "$OPTVAL"

XSTYLE=dots

while true ;do
	case "$1" in
		-s)
			START=$2
			shift 2
		;;
		-e) 
			END=$2
			shift 2
		;;
		-t) 
			TITLE=$2
			shift 2
		;;
		-d) 
			XSTYLE=$2
			shift 2
		;;
		-o) 
			OUTFILE=$2
			shift 2
		;;
		--) 
			shift 
			break 
		;;
		*) 
			echo "Internal error!" 
			exit 1 
		;;
	esac
done

if [ $START ] ;then
	XRANGE="[$START:$END]"
else
	XRANGE=""
fi

if [ $OUTFILE ] ;then
	XOUT="set output '$OUTFILE';"
else
	XOUT=""
fi

FNAME=tmp.$$
cat > $FNAME
grep r $FNAME > $FNAME.r
grep w $FNAME > $FNAME.w

(
	echo "set terminal postscript color;"
	echo "set linestyle 1 lt 1;" # red line
	echo "set linestyle 2 lt 3;" # blue line
#	echo "clear;"
	echo "set data style $XSTYLE;"
	echo "set noborder;"
	echo $XOUT
#	echo "set offsets 0,0,0,1000;"
	echo "set label \"$TITLE: `date +%Y-%m-%d` by `whoami` at `uname -a`\" at graph -0.1,-0.07"
	echo "plot $XRANGE '$FNAME.r' title 'reads' lt 1, '$FNAME.w' title 'writes' lt 3;"
) | gnuplot #| gv -landscape -

rm -f $FNAME $FNAME.r $FNAME.w
