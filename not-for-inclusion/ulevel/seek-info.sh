#! /bin/sh

awk 'BEGIN {prev=0} {cur = $2 ; print (cur - prev - 1) " " prev " " cur ; prev = cur}'
