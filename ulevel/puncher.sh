#! /bin/sh

OF=${1:-punched.file}
SZ=${2:-1000}
DD="dd if=/dev/zero of=$OF bs=4096 count=1 conv=notrunc"

echo -n '->'

rm -f $OF || exit 1
$DD seek=$SZ 2>/dev/null || exit 2
seq 0 2 $SZ | while read ;do
        $DD seek=$REPLY 2>/dev/null || exit 3
        echo -n .
done

echo
echo -n '<-'

rm -f $OF || exit 1
seq $SZ -2 0 | while read ;do
        $DD seek=$REPLY 2>/dev/null || exit 3
        echo -n .
done
