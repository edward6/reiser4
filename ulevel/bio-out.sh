#! /bin/sh

egrep '\.\.\.\.\.\.bio' | \
awk 'BEGIN {cnt=1} { split($9, pair, "[(,)]"); for(i=0 ; i<pair[3] ; ++i) { print cnt " " pair[2]+i " " $7; cnt +=1; } }' 
