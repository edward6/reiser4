#! /usr/bin/python

import sys
import re

marks = {}
n = 0
mark_cp = re.compile("mark=")
bio_cp = re.compile("\.\.\.\.\.\.bio")

def out_bio(first, len, rw, n, pid):
    block = first
    mrk = ""
    if marks.has_key(pid):
        mrk = " " + marks[pid]
    for i in xrange(1, len + 1):
        print str(n) + " " + str(block) + " " + rw + mrk

for line in sys.stdin:
    n = n + 1
    try:
        (pid, comm, dev, jiffies, rest) = re.split(" +", line, 4)
        if mark_cp.match(rest):
            mark = re.match("mark=(\w+)", rest).group(1)
            marks[pid] = mark
            continue
        if bio_cp.match(rest):
            m = re.split(" +", rest)
            rw = m[2]
            blocks=m[4]
            (first, len) = re.match("\((\d+),(\d+)\)", blocks).groups()
            out_bio(long(first), long(len), rw, n, pid)
    except ValueError:
        pass



