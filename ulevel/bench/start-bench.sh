#! /bin/sh

touch do-bench

. functions.sh

systemrestart "$(cat kernel)"
