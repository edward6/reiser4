#! /usr/bin/python

# Parse a reiser4 even log, find all mark=* events, print
# all bios as "<seq.number> <block> <rw>". And, print
# optional <trace_mark> suffix by a trace_mark if
# mark=<trace_mark> event was found earlier for the same
# thread.

import sys
import re

n = 1
marks={}

mark_cp = re.compile("mark=")
bio_cp = re.compile("\.\.\.\.\.\.bio")

def out_bio(first, len):
    global n
    block = first
    mrk = ""
    if marks.has_key(pid):
        mrk = " " + marks[pid]
    for i in xrange(1, len + 1):
        print str(n) + " " + str(block) + " " + rw + mrk
        n = n + 1

for line in sys.stdin:
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
            out_bio(long(first), long(len))
    except ValueError:
        pass



