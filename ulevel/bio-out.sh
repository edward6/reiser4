#! /bin/sh

egrep '\.\.\.\.\.\.bio' | \
awk '{split($9, pair, "[(,)]"); for(i=0 ; i<pair[3] ; ++i) print pair[2]+i}' 
