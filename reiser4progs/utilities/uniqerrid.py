# This is a script for detecting non-unique error ids in Reiser4 code.
# Josh MacDonald is to blame for any trouble this causes you.
#
#
# The syntax is:
#
#   python $SCRIPTS_DIR/uniqerrid.py $USER_NAME [LIST OF FILES]
#
# For example:
#
#   python ../../../scripts/uniqerrid.py jmacd *.c
#
# If everything is in order the command produces just a summary.
#
# For each error method that matches the user name (e.g., "jmacd") but
# does not match the numeric ID pattern (e.g., "jmacd-13") the command
# will print a NO MATCH error.
#
# If any duplicate error IDs are found the file:line will be printed
# along with the location of the original definition.

import string
import sys
import os
import popen2
import re

# Regexp string of all error method
meth = "assert|rpanic|rlog|impossible|not_implemented|wrong_return_value|warning|not_yet"

cmd  = "egrep -n \"" + meth + "\" ";

user = sys.argv[1]

lexp = re.compile ("(\S+:\S+):")
aexp = re.compile ("(" + meth + ")\s*\(\s*")
iexp = re.compile (user + "-(\d+)")
uexp = re.compile (user)

for i in sys.argv[2:] :
    cmd = cmd + i + " "

output,input = popen2.popen2 (cmd);

chunk = output.read ();

input.close ();

lines = string.split (chunk, "\n")

ids = { }

count = 0
dups  = 0
noids = 0
maxid = 0

for line in lines:

    # Ignore blank lines
    l = string.strip (line)
    if l == "":
        continue

    # See if it is really an function call
    amatch = aexp.search (l)
    if not amatch:
        #print "NO PATTERN match : " + l
        continue

    # If the user name does not match, ignore it
    umatch = uexp.search (l)
    if not umatch:
        #print "NO USER match : " + l
        continue

    # Try matching the user-ID pattern
    match = iexp.search (l)

    if not match:
        noids = noids + 1
        print "NO ERROR ID for " + user + ": " + l
        continue

    # Get the line:number
    lmatch = lexp.search (l)
    where  = l[lmatch.start(1):lmatch.end(1)]

    # Get the match string, just the ID number
    errid = string.atoi (l[match.start(1):match.end(1)])

    if ids.has_key (errid):
        print where + " first defined: " + ids[errid]
        dups = dups + 1
        continue

    if errid > maxid:
        maxid = errid

    ids[errid] = where

    count = count + 1

print "Summary for user " + user + ":"
print "Found %d unique error IDs" % count
print "Found %d duplicate error IDs" % dups
print "Found %d errors without error IDs" % noids
print "Max error ID is %d" % maxid
