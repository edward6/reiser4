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
	echo "Level $l"
	cd $l
	print_files "...."
	cd ..
done