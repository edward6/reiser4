#! /bin/sh

OPTVAL=`getopt -o d:s:e:t: -n 'seeks.sh' -- "$@"`

# Note the quotes around `$TEMP': they are essential!
eval set -- "$OPTVAL"

XSTYLE=lines

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

if [ $TITLE ] ;then
	XTITLE="title \"$TITLE\""
else
	XTITLE=""
fi

(
	echo "set terminal postscript;"
#	echo "clear;"
	echo "set data style $XSTYLE;"
	echo "set noborder;"
#	echo "set offsets 0,0,0,1000;"
	echo "set label \"generated on `date +%Y-%m-%d` by `whoami` at `uname -a`\" at graph -0.1,-0.07"
	echo "plot $XRANGE '-' $XTITLE"
	cat
) | gnuplot #| gv -landscape -
