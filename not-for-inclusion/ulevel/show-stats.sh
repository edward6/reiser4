#! /bin/sh

function print_files()
{
	local prefix

	prefix=$1
	for f in * ;do \
		if [ -f $f ] ;then
			echo -n $prefix$(basename $f) ": "
			cat $f
			echo
		fi
	done
}

print_files ""

for l in level* ;do \
	cd $l
	hits=$(cat total_hits_at_level)
	if [ $hits -gt 0 ] ;then
		echo "Level $l"
		print_files "...."
	fi
	cd ..
done