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
file_name_base="tmpfile"

mark_cp = re.compile("mark=")
bio_cp = re.compile("\.\.\.\.\.\.bio")

files={}

def get_file_name(rw, mark):
    global file_name_base
    if mark != "":
        return file_name_base + "[" + rw + "," + mark + "]"
    else:
        return file_name_base + "[" + rw + "]"

def get_file(rw, mark):
    global files
    name = get_file_name(rw, mark)
    if files.has_key(name):
        return files[name]
    else:
        file = open(name, "w")
        files[name] = file
        return file

def close_all_files():
    global files
    for name in files.keys():
        files[name].close()

def dispatch_bio(n, block, rw, mark=""):
    file = get_file(rw, mark)
    file.write(str(n) + " " + str(block) + "\n")

def out_bio(first, len):
    global n
    block = first
    for i in xrange(1, len + 1):
        if marks.has_key(pid):
            # print str(n) + " " + str(block) + " " + rw + " " + marks[pid]
            dispatch_bio(n, block, rw, marks[pid])
        else:
            # print str(n) + " " + str(block) + " " + rw
            dispatch_bio(n, block, rw)
        block = block + 1    
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

close_all_files()


